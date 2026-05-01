Param(
  [Parameter(Mandatory = $true)]
  [string]$Edk2Root,
  [ValidateSet("baseline", "optimized")]
  [string]$Mode = "baseline",
  [ValidateSet("DEBUG", "RELEASE")]
  [string]$Target = "RELEASE",
  [string]$Toolchain = "VS2022",
  [object]$Runs = 10,
  [object]$BootTimeoutSec = 90,
  [switch]$BuildFirmware
)

$ErrorActionPreference = "Stop"

function Normalize-IntValue {
  param(
    [Parameter(Mandatory = $true)]
    [object]$Value,
    [Parameter(Mandatory = $true)]
    [string]$Name,
    [int]$Min = 1
  )

  $candidate = $Value
  if ($candidate -is [System.Array]) {
    if ($candidate.Count -eq 0) {
      throw "$Name is empty."
    }
    $candidate = $candidate[0]
  }

  $parsed = 0
  if (-not [int]::TryParse([string]$candidate, [ref]$parsed)) {
    throw "$Name must be an integer, got '$candidate'."
  }
  if ($parsed -lt $Min) {
    throw "$Name must be >= $Min, got $parsed."
  }
  return $parsed
}

function Resolve-PhysicalPath {
  param([string]$Path)
  $resolved = (Resolve-Path -Path $Path).Path
  foreach ($line in (cmd.exe /c subst)) {
    if ($line -match '^([A-Z]):\\: => (.+)$') {
      $drive = $matches[1] + ":\"
      $target = $matches[2]
      if ($resolved.StartsWith($drive, [System.StringComparison]::OrdinalIgnoreCase)) {
        return Join-Path $target $resolved.Substring($drive.Length)
      }
    }
  }
  return $resolved
}

function Clean-Line([string]$line) {
  $clean = $line -replace "`e\[[0-9;?=]*[A-Za-z]", ""
  $clean = $clean -replace "[\u0000-\u001F]", ""
  return $clean.Trim()
}

function Extract-CsvLinesFromSerial {
  param([string]$Path)
  $csv = New-Object System.Collections.Generic.List[string]
  foreach ($line in (Get-Content -Path $Path -ErrorAction SilentlyContinue)) {
    $clean = Clean-Line $line
    if ([string]::IsNullOrWhiteSpace($clean)) { continue }
    if (
      $clean -match '^section,metric,value$' -or
      $clean -match '^summary,[^,]+,[0-9]+$' -or
      $clean -match '^rank,source,duration_ns,module,token$' -or
      $clean -match '^[0-9]+,(gauge|phase),[0-9]+,.*$' -or
      $clean -match '^last_successful_bootoption,[0-9]+$'
    ) {
      $csv.Add($clean)
    }
  }
  return $csv
}

function Parse-RunCsv {
  param([string]$Path)
  $result = [ordered]@{
    EntryToEndOfDxeNs = 0
    EndOfDxeToReadyToBootNs = 0
    ReadyToBootToExitBootServicesNs = 0
    EntryToExitBootServicesNs = 0
    LastBootCurrent = 0
    HotspotRank1Source = ""
    HotspotRank1DurationNs = 0
    HotspotRank1Module = ""
    HotspotRank1Token = ""
  }
  foreach ($line in (Get-Content -Path $Path)) {
    if ($line -match '^summary,([^,]+),([0-9]+)$') {
      $k = $matches[1]; $v = [double]$matches[2]
      if ($result.Contains($k)) { $result[$k] = $v }
      continue
    }
    if ($line -match '^1,(gauge|phase),([0-9]+),([^,]*),(.*)$') {
      $result.HotspotRank1Source = $matches[1]
      $result.HotspotRank1DurationNs = [double]$matches[2]
      $result.HotspotRank1Module = $matches[3]
      $result.HotspotRank1Token = $matches[4]
    }
  }
  return [pscustomobject]$result
}

function Get-Stat([double[]]$Values) {
  $s = $Values | Sort-Object
  $n = $s.Count
  if ($n -eq 0) { return [pscustomobject]@{ Mean=0; Median=0; P95=0; Min=0; Max=0 } }
  $mean = ($Values | Measure-Object -Average).Average
  $median = if (($n % 2) -eq 0) { ($s[$n/2-1] + $s[$n/2]) / 2.0 } else { $s[[int]($n/2)] }
  $p95Idx = [Math]::Min($n-1, [Math]::Max(0, [Math]::Ceiling($n * 0.95)-1))
  [pscustomobject]@{
    Mean=[Math]::Round($mean,2); Median=[Math]::Round($median,2); P95=[Math]::Round($s[$p95Idx],2)
    Min=[Math]::Round($s[0],2); Max=[Math]::Round($s[$n-1],2)
  }
}

$Runs = Normalize-IntValue -Value $Runs -Name "Runs" -Min 1
$BootTimeoutSec = Normalize-IntValue -Value $BootTimeoutSec -Name "BootTimeoutSec" -Min 5

if (!(Test-Path -Path $Edk2Root -PathType Container)) { throw "Edk2Root not found: $Edk2Root" }
$edksetup = Join-Path $Edk2Root "edksetup.bat"
if (!(Test-Path -Path $edksetup -PathType Leaf)) { throw "Cannot find edksetup.bat under: $Edk2Root" }
$qemuExe = "C:\Program Files\qemu\qemu-system-x86_64.exe"
if (!(Test-Path -Path $qemuExe -PathType Leaf)) { throw "QEMU not found: $qemuExe" }

$buildRoot = Join-Path $Edk2Root "Build\OvmfX64\${Target}_${Toolchain}"
$codeFd = Join-Path $buildRoot "FV\OVMF_CODE.fd"
$varsFd = Join-Path $buildRoot "FV\OVMF_VARS.fd"
$dumpEfi = Join-Path $buildRoot "X64\BootTimeDump.efi"
$qemuFs = Join-Path $Edk2Root "QemuFs"
if (!(Test-Path $qemuFs)) { New-Item -Path $qemuFs -ItemType Directory | Out-Null }
$qemuFsForQemu = "QemuFs"

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outRoot = Join-Path $Edk2Root "BootTimeOptPkg\results\${Mode}-${stamp}"
$rawDir = Join-Path $outRoot "raw"
New-Item -Path $rawDir -ItemType Directory -Force | Out-Null

$policyValue = if ($Mode -eq "optimized") { "TRUE" } else { "FALSE" }
$pcd = "gBootTimeOptPkgTokenSpaceGuid.PcdBootTimeOptEnablePolicy=$policyValue"
$dsc = "OvmfPkg/OvmfPkgX64.dsc"

Write-Host "== BootTimeOpt benchmark runner =="
Write-Host "Edk2Root : $Edk2Root"
Write-Host "Mode     : $Mode"
Write-Host "Runs     : $Runs"
Write-Host "Output   : $outRoot"
Write-Host "QemuFs   : $qemuFsForQemu"

Push-Location $Edk2Root
try {
  if ($BuildFirmware) {
    $cmd = @(
      "call `"$edksetup`"",
      "set VSLANG=1033",
      "set CL=/utf-8",
      "build -a X64 -t $Toolchain -p $dsc -b $Target --pcd $pcd -n 1"
    ) -join " && "
    cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }
  }

  Copy-Item -Path $dumpEfi -Destination (Join-Path $qemuFs "BootTimeDump.efi") -Force
  $bootDir = Join-Path $qemuFs "EFI\BOOT"
  New-Item -Path $bootDir -ItemType Directory -Force | Out-Null
  Copy-Item -Path $dumpEfi -Destination (Join-Path $bootDir "BOOTX64.EFI") -Force

  $runRows = @()
  for ($i = 1; $i -le $Runs; $i++) {
    Write-Host "Run $i/$Runs ..."
    $id = "{0:D2}" -f $i
    $runVars = Join-Path $rawDir "OVMF_VARS_$id.fd"
    $runSerial = Join-Path $rawDir "serial_$id.log"
    $runErr = Join-Path $rawDir "stderr_$id.log"
    $runCsv = Join-Path $rawDir "run_$id.csv"
    Copy-Item -Path $varsFd -Destination $runVars -Force
    if (Test-Path $runSerial) { Remove-Item $runSerial -Force }
    if (Test-Path $runErr) { Remove-Item $runErr -Force }

    $args = @(
      "-machine","q35,accel=tcg",
      "-m","2048",
      "-boot","order=c",
      "-display","none",
      "-nographic",
      "-serial","stdio",
      "-monitor","none",
      "-drive","if=pflash,format=raw,readonly=on,file=$codeFd",
      "-drive","if=pflash,format=raw,file=$runVars",
      "-drive","if=virtio,format=raw,file=fat:rw:$qemuFsForQemu"
    )
    $p = Start-Process -FilePath $qemuExe -ArgumentList $args -PassThru -NoNewWindow -RedirectStandardOutput $runSerial -RedirectStandardError $runErr

    $deadline = (Get-Date).AddSeconds($BootTimeoutSec)
    $ok = $false
    while ((Get-Date) -lt $deadline) {
      if (Test-Path $runSerial) {
        $csvLines = Extract-CsvLinesFromSerial -Path $runSerial
        if ($csvLines.Count -ge 7) {
          $csvLines | Set-Content -Path $runCsv -Encoding ascii
          $ok = $true
          break
        }
      }
      if ($p.HasExited) { break }
      Start-Sleep -Milliseconds 500
      $p.Refresh()
    }
    if (!$p.HasExited) { Stop-Process -Id $p.Id -Force }

    if (!$ok) {
      $tail = if (Test-Path $runSerial) { (Get-Content $runSerial -Tail 40) -join [Environment]::NewLine } else { "" }
      $etail = if (Test-Path $runErr) { (Get-Content $runErr -Tail 40) -join [Environment]::NewLine } else { "" }
      throw "Run $i timed out after ${BootTimeoutSec}s without extracting CSV from serial.$([Environment]::NewLine)$tail$([Environment]::NewLine)$etail"
    }

    $r = Parse-RunCsv -Path $runCsv
    $runRows += [pscustomobject]@{
      Run=$i
      EntryToEndOfDxeNs=$r.EntryToEndOfDxeNs
      EndOfDxeToReadyToBootNs=$r.EndOfDxeToReadyToBootNs
      ReadyToBootToExitBootServicesNs=$r.ReadyToBootToExitBootServicesNs
      EntryToExitBootServicesNs=$r.EntryToExitBootServicesNs
      LastBootCurrent=$r.LastBootCurrent
      HotspotRank1Source=$r.HotspotRank1Source
      HotspotRank1DurationNs=$r.HotspotRank1DurationNs
      HotspotRank1Module=$r.HotspotRank1Module
      HotspotRank1Token=$r.HotspotRank1Token
    }
  }

  $runsCsv = Join-Path $outRoot "${Mode}_runs.csv"
  $runRows | Export-Csv -NoTypeInformation -Encoding utf8 -Path $runsCsv

  $stats = @()
  foreach ($m in @("EntryToEndOfDxeNs","EndOfDxeToReadyToBootNs","ReadyToBootToExitBootServicesNs","EntryToExitBootServicesNs","HotspotRank1DurationNs")) {
    $vals = $runRows | ForEach-Object { [double]($_.$m) }
    $s = Get-Stat $vals
    $stats += [pscustomobject]@{ Metric=$m; MeanNs=$s.Mean; MedianNs=$s.Median; P95Ns=$s.P95; MinNs=$s.Min; MaxNs=$s.Max }
  }
  $statsCsv = Join-Path $outRoot "${Mode}_stats.csv"
  $stats | Export-Csv -NoTypeInformation -Encoding utf8 -Path $statsCsv
  Write-Host "Benchmark complete."
  Write-Host $runsCsv
  Write-Host $statsCsv
}
finally {
  Pop-Location
}

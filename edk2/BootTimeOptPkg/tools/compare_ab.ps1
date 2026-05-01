Param(
  [Parameter(Mandatory = $true)]
  [string]$BaselineStatsCsv,
  [Parameter(Mandatory = $true)]
  [string]$OptimizedStatsCsv,
  [string]$OutputDir
)

$ErrorActionPreference = "Stop"

function To-Number {
  param([object]$Value)
  $n = 0.0
  if ([double]::TryParse([string]$Value, [ref]$n)) {
    return $n
  }
  return 0.0
}

function ImprovementPercent {
  param(
    [double]$Baseline,
    [double]$Optimized
  )

  if ($Baseline -le 0) {
    return 0.0
  }

  return (($Baseline - $Optimized) / $Baseline) * 100.0
}

if (!(Test-Path -Path $BaselineStatsCsv -PathType Leaf)) {
  throw "Baseline stats CSV not found: $BaselineStatsCsv"
}
if (!(Test-Path -Path $OptimizedStatsCsv -PathType Leaf)) {
  throw "Optimized stats CSV not found: $OptimizedStatsCsv"
}

$baselineRows = Import-Csv -Path $BaselineStatsCsv
$optimizedRows = Import-Csv -Path $OptimizedStatsCsv

$optimizedByMetric = @{}
foreach ($row in $optimizedRows) {
  $optimizedByMetric[$row.Metric] = $row
}

$abRows = @()
foreach ($b in $baselineRows) {
  if (-not $optimizedByMetric.ContainsKey($b.Metric)) {
    continue
  }

  $o = $optimizedByMetric[$b.Metric]
  $baselineMean = To-Number $b.MeanNs
  $optimizedMean = To-Number $o.MeanNs
  $delta = $optimizedMean - $baselineMean
  $improve = ImprovementPercent -Baseline $baselineMean -Optimized $optimizedMean

  $abRows += [pscustomobject]@{
    Metric             = $b.Metric
    BaselineMeanNs     = [Math]::Round($baselineMean, 2)
    OptimizedMeanNs    = [Math]::Round($optimizedMean, 2)
    DeltaNs            = [Math]::Round($delta, 2)
    ImprovementPercent = [Math]::Round($improve, 2)
  }
}

if ($abRows.Count -eq 0) {
  throw "No overlapping metrics found between baseline and optimized stats."
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $OutputDir = Join-Path (Split-Path -Parent $BaselineStatsCsv) "ab-$stamp"
}

New-Item -Path $OutputDir -ItemType Directory -Force | Out-Null

$abCsv = Join-Path $OutputDir "ab_comparison.csv"
$abRows | Export-Csv -Path $abCsv -NoTypeInformation -Encoding utf8

$md = Join-Path $OutputDir "ab_comparison.md"
$lines = @(
  "# BootTimeOpt A/B Comparison",
  "",
  "- Baseline: ``$BaselineStatsCsv``",
  "- Optimized: ``$OptimizedStatsCsv``",
  "- Generated: $(Get-Date -Format s)",
  "",
  "| Metric | Baseline Mean (ns) | Optimized Mean (ns) | Delta (ns) | Improvement (%) |",
  "|---|---:|---:|---:|---:|"
)

foreach ($r in $abRows) {
  $lines += "| $($r.Metric) | $($r.BaselineMeanNs) | $($r.OptimizedMeanNs) | $($r.DeltaNs) | $($r.ImprovementPercent) |"
}

$lines | Set-Content -Path $md -Encoding utf8

Write-Host "A/B comparison complete:"
Write-Host "  $abCsv"
Write-Host "  $md"

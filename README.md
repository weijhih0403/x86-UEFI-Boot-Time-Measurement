# x86 UEFI Boot-Time Optimizer

這是一個加在 EDK II / OVMF 上的開機時間量測專案。它先把 DXE 階段的時間記下來、重複跑、再比較，用來判斷某個改動有沒有真的變快。這個倉庫是要疊進完整 EDK II 的修改，不是一份能單獨編譯的韌體樹。

## 專案在做什麼

1. **量測**：`BootTimeProfilerDxe` 在 EndOfDxe、ReadyToBoot、ExitBootServices 記下時間，寫入 `BootTimeSummary`。每個階段有一個旗標。旗標是 0 代表這段沒有發生，時間 0 不能解讀成「這段很快」。
2. **定位**：`BootTimeHotspotDxe` 在 ReadyToBoot 讀取 `PerformanceEx` 的 gauge，依耗時留下前幾名。沒有 gauge 時，改記已經走到的階段，並把還沒走到的 `ExitBootServices` 標成 `NOT_REACHED`。
3. **驗證**：`BootTimeDump` 把變數印成 CSV。`run_benchmark.ps1` 用 QEMU 重複開機，計算 Mean、Median、P95。`compare_ab.ps1` 比較兩次實驗。

`PcdBootTimeOptEnablePolicy` 是 baseline / optimized 兩次建置的標籤，CSV 的 `PolicyEnabled` 會印出這次韌體是哪一種。它本身不改變開機路徑。要宣稱優化，必須另有一個會改開機行為的改動，而且差異要大於同一台機器的重複誤差。

## 怎麼接進 OVMF

- `BootTimeOptPkg`：上面三個模組與變數定義。
- `OvmfPkg/OvmfPkgX64.dsc` 與 `OvmfPkgX64.fdf`：把模組編進 OVMF。
- DSC 把 DXE 的 `PerformanceLib` 換成 `DxeCorePerformanceLib` / `DxePerformanceLib`，並打開 `PcdPerformanceLibraryPropertyMask`。PEI 仍用空實作。這份 OVMF 若在 PEI 啟用 `PeiPerformanceLib`，HOB 走訪會停在長度為 0 的項目，開機到不了 BDS。
- 熱點驅動讀的是 `PerformanceEx` 的 gauge。上述 DXE 效能庫把紀錄放在 FPDT，這次開機沒有回傳 gauge，所以熱點清單仍是階段時間。

## 怎麼跑

在完整 EDK II 樹裡：

```powershell
.\BootTimeOptPkg\tools\run_benchmark.ps1 -Edk2Root <EDK2路徑> -Mode baseline -BuildFirmware -Runs 10
.\BootTimeOptPkg\tools\run_benchmark.ps1 -Edk2Root <EDK2路徑> -Mode optimized -BuildFirmware -Runs 10
.\BootTimeOptPkg\tools\compare_ab.ps1 -BaselineStatsCsv <baseline_stats.csv> -OptimizedStatsCsv <optimized_stats.csv>
```

QEMU 使用 TCG。絕對時間跟著宿主機 CPU 和當下負載變，不同電腦的奈秒不能直接比較。比較同一台機器、同一次實驗設定下的分布。

這次自動測試把 `BootTimeDump.efi` 當成開機程式，印完 CSV 就回到開機選單，不會呼叫 `ExitBootServices`。因此 `ExitBootServicesReached` 預期是 0。

## 2026-04-26 的結果怎麼讀

日期標的是**這次 A/B 實驗當天**，不是文件更新日期。來源：`edk2/BootTimeOptPkg/docs/AB_Results_2026-04-26.md`（對應 `baseline-20260426-235512` 與 `optimized-20260426-235627`）。

| Metric | Baseline Mean (ns) | Optimized Mean (ns) | Delta (ns) | 差異 (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 224472970.3 | 226632574.5 | 2159604.2 | -0.96 |
| EndOfDxeToReadyToBootNs | 630669847.1 | 623204708.5 | -7465138.6 | 1.18 |
| ReadyToBootToExitBootServicesNs | 0 | 0 | 0 | 0 |
| EntryToExitBootServicesNs | 0 | 0 | 0 | 0 |
| HotspotRank1DurationNs | 842107809.2 | 837054289.9 | -5053519.3 | 0.60 |

數字已對過當日 CSV 的 Mean。`差異 (%)` 以 baseline 為分母；正數表示 optimized 平均較短，負數表示較長。這次不能當成「開機變快 1%」：

- 兩次建置只差政策 PCD 的值，當時沒有對應的開機路徑改動。
- 同一台電腦連續三次，EndOfDxe 到 ReadyToBoot 的差距約 7%，大於表上的 1.18%。
- 後兩段時間是 0，是因為測試沒有走到 `ExitBootServices`。
- 熱點第 1 名當時是整段 `PHASE / Entry->ReadyToBoot`，因為平台的 `PerformanceLib` 還是空實作，沒有逐模組 gauge。

這個結果的價值是量測流程能看出「差異在誤差裡」。後續若要做優化，先看階段旗標和 gauge 排名，再改一個具體路徑，並用 Median / P95 判斷。

## 2026-11-05 完整 A/B（各 10 次）

與 2026-04-26 相同次數。來源：`edk2/BootTimeOptPkg/docs/AB_Results_2026-11-05.md`。

| Metric | Baseline Mean (ns) | Optimized Mean (ns) | Delta (ns) | 差異 (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 271844465.8 | 263590847 | -8253618.8 | 3.04 |
| EndOfDxeToReadyToBootNs | 648064376.4 | 646222773.6 | -1841602.8 | 0.28 |
| ReadyToBootToExitBootServicesNs | 0 | 0 | 0 | 0 |
| EntryToExitBootServicesNs | 0 | 0 | 0 | 0 |
| HotspotRank1DurationNs | 636880692.5 | 634286424.3 | -2594268.2 | 0.41 |

同樣不能當成優化成功：兩次建置仍只差政策 PCD；baseline 組內 EndOfDxe→ReadyToBoot 約 610–695 ms（約 14%），大於表上 0.28%。20 次旗標都是 EndOfDxe=1、ReadyToBoot=1、ExitBootServices=0；熱點第 1 名是 `EndOfDxe->ReadyToBoot`。絕對時間比 4 月高，是因為 TCG 跟著宿主機負載變，跨月的奈秒不要直接比。

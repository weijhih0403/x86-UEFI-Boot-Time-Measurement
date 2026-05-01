# x86 UEFI Boot-Time Optimizer

這是一個基於 EDK2/OVMF 的 UEFI 開機時間優化專案，目標是把「韌體開機效能」做成可量測、可比較、可重現的工程流程，而不是只靠體感或單次測試。

## 專案在做什麼

本專案主要解決三件事：

1. **量測**：在 DXE 開機路徑中記錄關鍵階段時間（EndOfDxe、ReadyToBoot、ExitBootServices）。
2. **定位**：收集開機熱點（hotspot）並排序，找出最耗時的模組/Token。
3. **驗證**：提供 A/B 基準測試腳本，自動輸出 CSV 與比較報表，量化「優化前 vs 優化後」差異。

## 系統架構

### 1) BootTimeOptPkg（核心套件）

`edk2/BootTimeOptPkg` 內包含三個主要模組：

- `BootTimeProfilerDxe`
  - 在開機事件點紀錄時間戳，寫入 `BootTimeSummary` 變數。
  - 提供 shell-only 路徑 fallback，避免沒有進入 OS 時資料遺失。
- `BootTimeHotspotDxe`
  - 優先使用 `PerformanceEx` gauge 資料收集 Top-N 熱點。
  - 若 gauge 不可用，退回 phase fallback，仍可產出可讀資料。
  - 輸出到 `BootTimeHotspotLog` 變數。
- `BootTimeDump`（UEFI App）
  - 從 NVRAM 讀取 `BootTimeSummary` / `BootTimeHotspotLog` / `LastSuccessfulBootOption`。
  - 預設輸出 CSV，方便自動化 benchmark 管線使用。

### 2) OVMF 整合層

- `edk2/OvmfPkg/OvmfPkgX64.dsc`
- `edk2/OvmfPkg/OvmfPkgX64.fdf`

這兩個檔案負責把上述模組真正編進 OVMF 韌體映像，並宣告對應 PCD 參數（baseline/optimized 模式切換）。

### 3) 自動化與報告

- `edk2/BootTimeOptPkg/tools/run_benchmark.ps1`
  - 自動執行多次 QEMU 測試，收集 serial log，抽取 CSV，計算統計量（Mean/Median/P95）。
- `edk2/BootTimeOptPkg/tools/compare_ab.ps1`
  - 比較 baseline 與 optimized 的 `_stats.csv`，輸出 A/B 差異報告。
- `edk2/BootTimeOptPkg/docs/AB_Results_2026-04-26.md`
  - 已完成的實驗結果範例。

## 驗證方法（Validation Flow）

1. 用 `PcdBootTimeOptEnablePolicy=FALSE` 建 baseline 韌體並跑多次測試。  
2. 用 `PcdBootTimeOptEnablePolicy=TRUE` 建 optimized 韌體並跑多次測試。  
3. 透過 `BootTimeDump` 產生每次開機 CSV。  
4. 使用 `compare_ab.ps1` 產生 A/B 差異表，確認改善是否成立。  

## 已驗證結果（2026-04-26）

來源：`edk2/BootTimeOptPkg/docs/AB_Results_2026-04-26.md`

| Metric | Baseline Mean (ns) | Optimized Mean (ns) | Delta (ns) | Improvement (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 224472970.3 | 226632574.5 | 2159604.2 | -0.96 |
| EndOfDxeToReadyToBootNs | 630669847.1 | 623204708.5 | -7465138.6 | 1.18 |
| ReadyToBootToExitBootServicesNs | 0 | 0 | 0 | 0 |
| EntryToExitBootServicesNs | 0 | 0 | 0 | 0 |
| HotspotRank1DurationNs | 842107809.2 | 837054289.9 | -5053519.3 | 0.60 |

重點結論：

- `EndOfDxeToReadyToBootNs` 改善 **+1.18%**（本次最佳指標）。
- `HotspotRank1DurationNs` 改善 **+0.60%**。
- `EntryToEndOfDxeNs` 退步 **-0.96%**，下一步應優先針對 early DXE 路徑優化。

## 專案價值

- 建立了 **可重現** 的韌體效能驗證方法（不是一次性數字）。
- 將「開機慢」問題拆成可追蹤的 phase 與 hotspot 指標。
- 可直接延伸到更多平台或更多優化策略，形成持續迭代的 firmware performance workflow。


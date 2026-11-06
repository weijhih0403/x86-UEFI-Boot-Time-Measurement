# x86 UEFI Boot-Time Optimizer

加在 EDK II / OVMF 上的**開機時間量測與驗證框架**（overlay，需疊進完整 EDK II 才能編譯）。  
目標是先能量測、能量複、能判斷差異是否超過誤差；**不是**已證明開機加速百分之幾的優化專案。

## 架構（三個模組）

1. **`BootTimeProfilerDxe`**（DXE driver）：在 EndOfDxe / ReadyToBoot / ExitBootServices 打點，寫入 `BootTimeSummary`；用階段旗標區分「沒走到」與「時間為 0」。
2. **`BootTimeHotspotDxe`**（DXE driver）：ReadyToBoot 時收集熱點（有 `PerformanceEx` gauge 用 gauge，否則用階段備援），寫入 `BootTimeHotspotLog`。
3. **`BootTimeDump`**（UEFI app）：`GetVariable` 後印 CSV，供 QEMU 腳本擷取。

建置開關：`PcdBootTimeOptEnablePolicy` 與 `PcdBootTimeOptEnableHotspot` **同時為 TRUE** 才會做熱點收集。  
`baseline` = 兩者 FALSE；`optimized` = 兩者 TRUE。CSV 欄位 `PolicyEnabled` / `HotspotEnabled` 對應這兩個 PCD。

## 怎麼跑

在完整 EDK II 樹：

```powershell
.\BootTimeOptPkg\tools\run_benchmark.ps1 -Edk2Root <EDK2路徑> -Mode baseline -BuildFirmware -Runs 10
.\BootTimeOptPkg\tools\run_benchmark.ps1 -Edk2Root <EDK2路徑> -Mode optimized -BuildFirmware -Runs 10
.\BootTimeOptPkg\tools\compare_ab.ps1 -BaselineStatsCsv <baseline_stats.csv> -OptimizedStatsCsv <optimized_stats.csv>
```

- QEMU 使用 TCG：絕對時間跟宿主機有關，只比**同一台、同一次實驗**的分布。  
- 測試把 `BootTimeDump.efi` 當開機程式，不會呼叫 `ExitBootServices`，故相關段為 0、`ExitBootServicesReached=0` 是預期。  
- 目前熱點多為階段備援（`PHASE / EndOfDxe->ReadyToBoot`），不是逐模組 gauge。

## 驗證結果（僅此一組）

日期 **2026-11-06**，各 10 次。詳見 [`edk2/BootTimeOptPkg/docs/AB_Results.md`](edk2/BootTimeOptPkg/docs/AB_Results.md)。  
資料：`baseline-20261106-230720`、`optimized-20261106-230757`、`ab-20261106-policy-hotspot`。

| Metric | Baseline Mean (ns) | Optimized Mean (ns) | Delta (ns) | 差異 (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 268235263.9 | 250411574.1 | -17823689.8 | 6.64 |
| EndOfDxeToReadyToBootNs | 668041552.2 | 684719063 | 16677510.8 | -2.5 |
| ReadyToBootToExitBootServicesNs | 0 | 0 | 0 | 0 |
| EntryToExitBootServicesNs | 0 | 0 | 0 | 0 |

- 旗標：baseline 皆 `PolicyEnabled=0, HotspotEnabled=0`；optimized 皆 `1,1`。  
- 跑前規則：主指標 Median 差須大於 baseline 組內 (Max−Min)/2 才稱可量測。本次約 31 ms < 約 39 ms → **未通過**。  
- 結論：量測鏈路與 A/B 流程可運作；在此 TCG 環境下，熱點收集開銷**無法**與重複誤差清楚分開，因此**不能**說成開機優化成功。

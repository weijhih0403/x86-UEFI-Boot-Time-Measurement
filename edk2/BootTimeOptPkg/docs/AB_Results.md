# 開機時間比較結果（2026-11-07）

本專案**唯一**對外報告的測試紀錄。用語與根目錄 `README.md` 一致。

## 摘要

- 兩組 PCD：baseline `Policy=0, Hotspot=0`；optimized `Policy=1, Hotspot=1`。  
- 全部 20 次開機均 `ExitBootServicesReached=1`；ExitBootServices 相關時間為非 0。  
- 下列為平均／中位數等數值，僅供閱讀，不另行標示通過與否。  
- 兩組差異主要是是否啟用額外效能紀錄，**不是**改過開機流程。

---

## 設定

| 組別 | Policy | Hotspot | 資料夾 |
|---|---|---|---|
| baseline | 0 | 0 | `baseline-20261107-214115` |
| optimized | 1 | 1 | `optimized-20261107-214243` |

- 每組 10 次，同一台主機，QEMU TCG，OVMF RELEASE VS2022  
- 比較輸出：`ab-20261107-ebs-policy-hotspot`  
- 只有兩個 PCD 都為 TRUE 時，`BootTimeHotspotDxe` 才會收集  
- Dump 先印 CSV，再呼叫 `ExitBootServices`；Profiler 用序列埠補上最終欄位  

## 平均結果

| 測量區間 | Baseline 平均 (ns) | Optimized 平均 (ns) | 差值 (ns) | 差異 (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 255428357.1 | 247158535 | -8269822.1 | 3.24 |
| EndOfDxeToReadyToBootNs | 718433375.7 | 676939191.5 | -41494184.2 | 5.78 |
| ReadyToBootToExitBootServicesNs | 553985743.6 | 568442441.1 | 14456697.5 | -2.61 |
| EntryToExitBootServicesNs | 1527847477.3 | 1492540168.6 | -35307308.7 | 2.31 |
| HotspotRank1DurationNs | 0（無 log） | 661342041.5 | n/a | n/a |

換算後：EndOfDxe → ReadyToBoot 平均約 718.43 ms → 676.94 ms（約減少 41.49 ms）；  
Driver 入口 → ExitBootServices 平均約 1,527.85 ms → 1,492.54 ms（約減少 35.31 ms）。

在本次各 10 次的 QEMU 測試中，啟用額外效能紀錄的設定，測得 EndOfDxe 到 ReadyToBoot 的平均時間約減少 41.49 毫秒。不過，這兩組設定並沒有直接修改開機執行流程，因此結果只能說明此次測試觀察到的時間差異，不能直接證明已完成開機加速。

## EndOfDxeToReadyToBootNs（補充）

| | Baseline (ns) | Optimized (ns) |
|---|---:|---:|
| Median | 713717385 | 677491831.5 |
| Min | 671932047 | 645221110 |
| Max | 765796490 | 707728496 |
| Max−Min | 93864443 | 62507386 |

中位數差（絕對值）：約 36.2 ms。

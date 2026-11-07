# Boot-Time Measurement A/B 結果（2026-11-07）

本專案**唯一**對外報告的實驗紀錄。用語與根目錄 `README.md` 一致。

## 摘要

- 兩臂 PCD：baseline `Policy=0, Hotspot=0`；optimized `Policy=1, Hotspot=1`。  
- 全部 20 次開機均 `ExitBootServicesReached=1`；EBS 相關時間為非 0。  
- 下列為 Mean／Median 等統計數值，供直接閱讀，不另行標示通過與否。

---

## Setup

| Arm | Policy | Hotspot | Folder |
|---|---|---|---|
| baseline | 0 | 0 | `baseline-20261107-214115` |
| optimized | 1 | 1 | `optimized-20261107-214243` |

- N = 10 per arm, same host, QEMU TCG, OVMF RELEASE VS2022  
- Comparison output: `ab-20261107-ebs-policy-hotspot`  
- Collection runs only when **both** PCDs are TRUE (`BootTimeHotspotDxe`)  
- Dump prints CSV, then calls `ExitBootServices`; Profiler appends final EBS summary lines on serial  

## Results (means)

| Metric | Baseline Mean (ns) | Optimized Mean (ns) | Delta (ns) | Diff (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 255428357.1 | 247158535 | -8269822.1 | 3.24 |
| EndOfDxeToReadyToBootNs | 718433375.7 | 676939191.5 | -41494184.2 | 5.78 |
| ReadyToBootToExitBootServicesNs | 553985743.6 | 568442441.1 | 14456697.5 | -2.61 |
| EntryToExitBootServicesNs | 1527847477.3 | 1492540168.6 | -35307308.7 | 2.31 |
| HotspotRank1DurationNs | 0 (no log) | 661342041.5 | n/a | n/a |

## EndOfDxeToReadyToBootNs（補充）

| | Baseline (ns) | Optimized (ns) |
|---|---:|---:|
| Median | 713717385 | 677491831.5 |
| Min | 671932047 | 645221110 |
| Max | 765796490 | 707728496 |
| Max−Min | 93864443 | 62507386 |

Median 差（絕對值）：`36225553.5` ns（約 36.2 ms）

# 開機時間比較結果（2026-11-17）

本專案**唯一**對外報告的測試紀錄。用語與根目錄 `README.md` 一致。  
本次在既有量測之上新增 `BootTimeMeasurement` Protocol（同開機 Publish／Get；NVRAM 仍為快照）。

## 摘要

- 兩組 PCD：baseline `Policy=0, Hotspot=0`；optimized `Policy=1, Hotspot=1`。  
- 全部 20 次開機均成功取得完整 Summary／BDS 細分；optimized 組有 Hotspot 排行。  
- 下列為平均／中位數等數值，僅供閱讀，不另行標示通過與否。  
- 兩組差異主要是是否啟用額外效能紀錄，**不是**改過開機流程；Protocol 是量測資料路徑改進，不是加速。

---

## 設定

| 組別 | Policy | Hotspot | 資料夾 |
|---|---|---|---|
| baseline | 0 | 0 | `baseline-20261117-233118` |
| optimized | 1 | 1 | `optimized-20261117-233009` |

- 每組 10 次，同一台主機，QEMU TCG，OVMF RELEASE VS2022  
- 比較輸出：`ab-20261117-protocol`  
- Dump 優先 `LocateProtocol`，找不到再讀 Variable；再呼叫 `ExitBootServices`  

## 平均結果

| 測量區間 | Baseline 平均 (ns) | Optimized 平均 (ns) | 差值 (ns) | 差異 (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 236343501.2 | 242997279.9 | 6653778.7 | -2.82 |
| EndOfDxeToReadyToBootNs | 639793241.4 | 656883933.1 | 17090691.7 | -2.67 |
| ReadyToBootToExitBootServicesNs | 555875508.9 | 573516270.5 | 17640761.6 | -3.17 |
| EntryToExitBootServicesNs | 1432012252.5 | 1473397484.5 | 41385232 | -2.89 |
| HotspotRank1DurationNs | 0（無 log） | 643206664 | n/a | n/a |

換算後：EndOfDxe → ReadyToBoot 平均約 639.79 ms → 656.88 ms（約增加 17.09 ms）；  
Driver 入口 → ExitBootServices 平均約 1,432.01 ms → 1,473.40 ms（約增加 41.39 ms）。

在本次各 10 次的 QEMU 測試中，啟用額外效能紀錄的設定，測得時間略高於 baseline。這符合「多做紀錄會有額外成本」的預期，**不能**解讀成開機加速；Protocol 的用意是同開機內可即時查詢量測服務，不是縮短開機。

## BDS 細分（平均）

| 桶 | Baseline 平均 (ns) | Optimized 平均 (ns) |
|---|---:|---:|
| PreConnectNs | 398554815.8 | 401160398.5 |
| ConnectAllNs | 198112552.9 | 198609711.1 |
| RefreshBootOptionsNs | 9655919.6 | 9755177.8 |
| PostRefreshToReadyToBootNs | 21820091.2 | 35381786.7 |
| LoadPathNs | 155604217.3 | 159191768.3 |

PreConnect 仍是 EndOfDxe→ReadyToBoot 內最大的一段。

## EndOfDxeToReadyToBootNs（補充）

| | Baseline (ns) | Optimized (ns) |
|---|---:|---:|
| Median | 635571559.5 | 658810407 |
| Min | 627792638 | 632123915 |
| Max | 663580985 | 674873203 |

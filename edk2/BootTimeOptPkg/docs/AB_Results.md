# Boot-Time Measurement A/B 結果（2026-11-07）

本倉庫**唯一**對外報告的實驗紀錄。用語與根目錄 `README.md` 一致：這是量測／A/B 流程驗證，不是「開機已變快」的宣稱。

## 結論摘要

- 兩臂 PCD 正確：baseline `Policy=0, Hotspot=0`；optimized `Policy=1, Hotspot=1`。  
- 全部 20 次開機均 `ExitBootServicesReached=1`；EBS 相關時間為非 0（Dump 呼叫 `ExitBootServices`，Profiler 以序列埠補上最終 CSV）。  
- 主指標 `EndOfDxeToReadyToBootNs` 的 Median 差約 36.2 ms，雜訊門檻約 46.9 ms → **未通過**預先登記的判定規則。  
- 因此：開啟耗時項目收集的額外影響在此 TCG 環境下仍無法與重複變異清楚分開；可確認量測鏈路（含 EBS）可用，不可據此宣稱開機優化成功。

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

## Decision rule (pre-registered)

Claim a measurable `EndOfDxeToReadyToBootNs` effect only if:

`|Median_opt - Median_base| > (Max_base - Min_base) / 2`

## Results (means)

| Metric | Baseline Mean (ns) | Optimized Mean (ns) | Delta (ns) | Diff (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 255428357.1 | 247158535 | -8269822.1 | 3.24 |
| EndOfDxeToReadyToBootNs | 718433375.7 | 676939191.5 | -41494184.2 | 5.78 |
| ReadyToBootToExitBootServicesNs | 553985743.6 | 568442441.1 | 14456697.5 | -2.61 |
| EntryToExitBootServicesNs | 1527847477.3 | 1492540168.6 | -35307308.7 | 2.31 |
| HotspotRank1DurationNs | 0 (no log) | 661342041.5 | n/a | n/a |

- Primary medians: baseline `713717385`, optimized `677491831.5`  
- Baseline range (Max−Min): `93864443`; threshold: `46932221.5`  
- Median gap: `36225553.5` → **below threshold**  

## Conclusion

The harness reaches ExitBootServices, labels arms correctly, and records phase/hotspot CSV. Under this TCG host load, the Policy+Hotspot collection effect on `EndOfDxeToReadyToBootNs` is not separable from run-to-run noise by the rule above. Treat this as a successful measurement validation (including EBS), not a boot-time optimization claim.

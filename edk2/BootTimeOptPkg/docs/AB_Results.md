# BootTimeOpt A/B Results (2026-11-06)

Single reported experiment for this repository.

## Setup

| Arm | Policy | Hotspot | Folder |
|---|---|---|---|
| baseline | 0 | 0 | `baseline-20261106-230720` |
| optimized | 1 | 1 | `optimized-20261106-230757` |

- N = 10 per arm, same host, QEMU TCG, OVMF RELEASE VS2022  
- Comparison output: `ab-20261106-policy-hotspot`  
- Collection runs only when **both** PCDs are TRUE (`BootTimeHotspotDxe`)  
- Dump does not call `ExitBootServices`; those deltas stay 0 by design  

## Decision rule (pre-registered)

Claim a measurable `EndOfDxeToReadyToBootNs` effect only if:

`|Median_opt - Median_base| > (Max_base - Min_base) / 2`

## Results (means)

| Metric | Baseline Mean (ns) | Optimized Mean (ns) | Delta (ns) | Diff (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 268235263.9 | 250411574.1 | -17823689.8 | 6.64 |
| EndOfDxeToReadyToBootNs | 668041552.2 | 684719063 | 16677510.8 | -2.5 |
| ReadyToBootToExitBootServicesNs | 0 | 0 | 0 | 0 |
| EntryToExitBootServicesNs | 0 | 0 | 0 | 0 |
| HotspotRank1DurationNs | 0 (no log) | 668452721.9 | n/a | n/a |

- Primary medians: baseline `662928248.5`, optimized `693624049.5`  
- Baseline range (Max−Min): `78206308`; threshold: `39103154`  
- Median gap: `30695801` → **below threshold**  

## Conclusion

The harness labels arms correctly and records phase/hotspot CSV. Under this TCG host load, ReadyToBoot collection cost is not separable from run-to-run noise by the rule above. Treat this as a successful measurement validation, not a boot-time optimization claim.

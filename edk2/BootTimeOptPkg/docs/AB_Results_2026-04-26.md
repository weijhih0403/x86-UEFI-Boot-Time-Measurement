# BootTimeOpt A/B Results (2026-04-26)

## Source Files

- Baseline stats: `edk2/BootTimeOptPkg/results/baseline-20260426-235512/baseline_stats.csv`
- Optimized stats: `edk2/BootTimeOptPkg/results/optimized-20260426-235627/optimized_stats.csv`

## Comparison Table

| Metric | Baseline Mean (ns) | Optimized Mean (ns) | Delta (ns) | Improvement (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 224472970.3 | 226632574.5 | 2159604.2 | -0.96 |
| EndOfDxeToReadyToBootNs | 630669847.1 | 623204708.5 | -7465138.6 | 1.18 |
| ReadyToBootToExitBootServicesNs | 0 | 0 | 0 | 0 |
| EntryToExitBootServicesNs | 0 | 0 | 0 | 0 |
| HotspotRank1DurationNs | 842107809.2 | 837054289.9 | -5053519.3 | 0.60 |

## Key Notes

- These two builds differed only by `PcdBootTimeOptEnablePolicy`. No boot-path behavior was gated on that PCD, so the table is a repeatability check, not evidence of a faster firmware.
- The largest mean shift, `EndOfDxeToReadyToBootNs` at 1.18%, is smaller than the roughly 7% spread seen across three consecutive runs on the same machine.
- `ReadyToBootToExitBootServicesNs` and `EntryToExitBootServicesNs` are 0 because `BootTimeDump` returns before `ExitBootServices`.
- Rank 1 was the single phase `Entry->ReadyToBoot`. `PerformanceLib` was the null instance, so no per-module gauge existed.

# BootTimeOpt A/B Results (2026-11-05)

## Source Files

- Baseline stats: `baseline-20261105-151822/baseline_stats.csv` (10 runs, `PolicyEnabled=0`)
- Optimized stats: `optimized-20261105-151904/optimized_stats.csv` (10 runs, `PolicyEnabled=1`)
- Comparison: `ab-20261105-1520/ab_comparison.csv`

Date is the experiment day. Runs used the updated phase-flag CSV format.

## Comparison Table

| Metric | Baseline Mean (ns) | Optimized Mean (ns) | Delta (ns) | Diff (%) |
|---|---:|---:|---:|---:|
| EntryToEndOfDxeNs | 271844465.8 | 263590847 | -8253618.8 | 3.04 |
| EndOfDxeToReadyToBootNs | 648064376.4 | 646222773.6 | -1841602.8 | 0.28 |
| ReadyToBootToExitBootServicesNs | 0 | 0 | 0 | 0 |
| EntryToExitBootServicesNs | 0 | 0 | 0 | 0 |
| HotspotRank1DurationNs | 636880692.5 | 634286424.3 | -2594268.2 | 0.41 |

Means match the stats CSVs. Diff (%) is relative to baseline; positive means optimized was shorter on average.

## Key Notes

- Builds still differed only by `PcdBootTimeOptEnablePolicy`. No boot-path behavior was gated on that PCD.
- Within the baseline set alone, `EndOfDxeToReadyToBootNs` ranged about 610 ms to 695 ms (~14% spread), larger than the 0.28% mean shift.
- All 20 runs reported EndOfDxe=1, ReadyToBoot=1, ExitBootServices=0. Hotspot rank 1 was `PHASE / EndOfDxe->ReadyToBoot`.
- Absolute means are higher than 2026-04-26 because QEMU TCG time tracks host load; compare distributions on the same day, not across months.

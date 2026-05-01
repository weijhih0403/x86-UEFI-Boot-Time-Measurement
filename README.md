# x86 UEFI Boot-Time Optimizer

This repository contains a focused EDK2/OVMF boot-time profiling and optimization project.

## What is included

- `edk2/BootTimeOptPkg`: custom package with
  - `BootTimeProfilerDxe`: records boot phase timing summary into UEFI variables.
  - `BootTimeHotspotDxe`: collects top hotspots (PerformanceEx gauge + fallback) into UEFI variables.
  - `BootTimeDump`: UEFI app that prints timing/hotspot data in CSV format.
  - benchmark scripts for repeated QEMU runs and A/B comparison.
- `edk2/OvmfPkg/OvmfPkgX64.dsc` and `edk2/OvmfPkg/OvmfPkgX64.fdf`: integration points that include BootTime modules in OVMF.
- `edk2/BaseTools/Source/Python/Trim/Trim.py`: encoding fallback improvement for localized Windows build environments.

## Goal

Build a reproducible baseline-vs-optimized firmware benchmark flow for UEFI boot path analysis.


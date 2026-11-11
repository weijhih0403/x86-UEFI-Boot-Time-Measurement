#ifndef BOOT_TIME_OPT_PKG_VARIABLE_H_
#define BOOT_TIME_OPT_PKG_VARIABLE_H_

#include <Uefi.h>
#include <Base.h>

#define BOOT_TIME_SUMMARY_VAR_NAME            L"BootTimeSummary"
#define BOOT_TIME_LAST_BOOT_OPTION_VAR_NAME   L"LastSuccessfulBootOption"
#define BOOT_TIME_HOTSPOT_LOG_VAR_NAME        L"BootTimeHotspotLog"
#define BOOT_TIME_BDS_BREAKDOWN_VAR_NAME      L"BootTimeBdsBreakdown"

#define BOOT_TIME_SUMMARY_SIGNATURE  SIGNATURE_32('B','T','O','1')
#define BOOT_TIME_SUMMARY_VERSION    2
#define BOOT_TIME_HOTSPOT_SIGNATURE  SIGNATURE_32('B','T','H','S')
#define BOOT_TIME_HOTSPOT_VERSION    1
#define BOOT_TIME_HOTSPOT_MAX_RANK   5
#define BOOT_TIME_BDS_BREAKDOWN_SIGNATURE  SIGNATURE_32('B','T','B','D')
#define BOOT_TIME_BDS_BREAKDOWN_VERSION    1

//
// Stored in BOOT_TIME_SUMMARY.Reserved0. A set bit means that phase was
// actually signaled. A clear bit means the following delta is absent, not a
// measured duration of zero.
//
#define BOOT_TIME_PHASE_END_OF_DXE          0x0001
#define BOOT_TIME_PHASE_READY_TO_BOOT       0x0002
#define BOOT_TIME_PHASE_EXIT_BOOT_SERVICES  0x0004

#define BOOT_TIME_HOTSPOT_SOURCE_PHASE_FALLBACK  1
#define BOOT_TIME_HOTSPOT_SOURCE_GAUGE           2

typedef struct {
  UINT32  Signature;
  UINT16  Version;
  UINT16  Reserved0;   // Phase mask. See BOOT_TIME_PHASE_* above.

  UINT64  EntryTimeNs;
  UINT64  EndOfDxeTimeNs;
  UINT64  ReadyToBootTimeNs;
  UINT64  ExitBootServicesTimeNs;

  UINT64  DeltaEntryToEndOfDxeNs;
  UINT64  DeltaEndOfDxeToReadyToBootNs;
  UINT64  DeltaReadyToBootToExitBootServicesNs;
  UINT64  DeltaEntryToExitBootServicesNs;

  UINT16  LastBootCurrent;
  UINT16  Reserved1;
} BOOT_TIME_SUMMARY;

typedef struct {
  UINT32  Rank;
  UINT32  Reserved;
  UINT64  DurationNs;
  CHAR8   Module[32];
  CHAR8   Token[32];
} BOOT_TIME_HOTSPOT_ENTRY;

typedef struct {
  UINT32                  Signature;
  UINT16                  Version;
  UINT8                   Source;
  UINT8                   Count;
  UINT32                  GaugeEntryCount;
  UINT32                  Reserved;
  BOOT_TIME_HOTSPOT_ENTRY Hotspot[BOOT_TIME_HOTSPOT_MAX_RANK];
} BOOT_TIME_HOTSPOT_LOG;

//
// BDS sub-intervals inside EndOfDxe -> ReadyToBoot (+ optional LoadPath).
// Flags: a clear bit means that bucket was not measured (absent), not zero work.
//
#define BOOT_TIME_BDS_FLAG_END_OF_DXE              0x0001
#define BOOT_TIME_BDS_FLAG_CONNECT                 0x0002
#define BOOT_TIME_BDS_FLAG_REFRESH                 0x0004
#define BOOT_TIME_BDS_FLAG_READY_TO_BOOT           0x0008
#define BOOT_TIME_BDS_FLAG_LOAD_PATH               0x0010

typedef struct {
  UINT32  Signature;
  UINT16  Version;
  UINT16  Flags;

  UINT64  PreConnectNs;                 // EndOfDxe -> Connect begin
  UINT64  ConnectAllNs;                 // PlatformBdsConnectSequence
  UINT64  RefreshBootOptionsNs;         // EfiBootManagerRefreshAllBootOption
  UINT64  PostRefreshToReadyToBootNs;   // Refresh end -> ReadyToBoot signal
  UINT64  LoadPathNs;                   // ReadyToBoot -> Dump app entry

  // Raw counters so ReadyToBoot / Dump images can finish deltas.
  UINT64  EndOfDxePerfTick;
  UINT64  ConnectStartPerfTick;
  UINT64  ConnectEndPerfTick;
  UINT64  RefreshEndPerfTick;
  UINT64  ReadyToBootPerfTick;
} BOOT_TIME_BDS_BREAKDOWN;

extern EFI_GUID  gBootTimeOptPkgVariableGuid;

#endif

#ifndef BOOT_TIME_OPT_PKG_VARIABLE_H_
#define BOOT_TIME_OPT_PKG_VARIABLE_H_

#include <Uefi.h>
#include <Base.h>

#define BOOT_TIME_SUMMARY_VAR_NAME            L"BootTimeSummary"
#define BOOT_TIME_LAST_BOOT_OPTION_VAR_NAME   L"LastSuccessfulBootOption"
#define BOOT_TIME_HOTSPOT_LOG_VAR_NAME        L"BootTimeHotspotLog"

#define BOOT_TIME_SUMMARY_SIGNATURE  SIGNATURE_32('B','T','O','1')
#define BOOT_TIME_SUMMARY_VERSION    1
#define BOOT_TIME_HOTSPOT_SIGNATURE  SIGNATURE_32('B','T','H','S')
#define BOOT_TIME_HOTSPOT_VERSION    1
#define BOOT_TIME_HOTSPOT_MAX_RANK   5

#define BOOT_TIME_HOTSPOT_SOURCE_PHASE_FALLBACK  1
#define BOOT_TIME_HOTSPOT_SOURCE_GAUGE           2

typedef struct {
  UINT32  Signature;
  UINT16  Version;
  UINT16  Reserved0;

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

extern EFI_GUID  gBootTimeOptPkgVariableGuid;

#endif

/** @file
  Collects PerformanceEx gauge data and persists Top-N hotspots.
  Falls back to phase timing when gauge data is unavailable.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/EventGroup.h>
#include <Guid/BootTimeOptPkgVariable.h>
#include <Guid/Performance.h>
#include <Guid/GlobalVariable.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define HOTSPOT_POOL_MAX  800

typedef struct {
  UINT64  DurationNs;
  CHAR8   Module[DXE_PERFORMANCE_STRING_SIZE];
  CHAR8   Token[DXE_PERFORMANCE_STRING_SIZE];
} HOTSPOT_CANDIDATE;

STATIC EFI_EVENT  mReadyToBootEvent = NULL;
STATIC UINT64     mEntryTick         = 0;

STATIC
UINT64
GetDurationNsSafe (
  IN UINT64  StartTick,
  IN UINT64  EndTick
  )
{
  if ((StartTick == 0) || (EndTick == 0) || (EndTick < StartTick)) {
    return 0;
  }

  return GetTimeInNanoSecond (EndTick - StartTick);
}

STATIC
VOID
FillPhaseFallback (
  OUT BOOT_TIME_HOTSPOT_LOG  *Log
  )
{
  UINT64  NowTick;

  NowTick = GetPerformanceCounter ();
  ZeroMem (Log, sizeof (*Log));
  Log->Signature       = BOOT_TIME_HOTSPOT_SIGNATURE;
  Log->Version         = BOOT_TIME_HOTSPOT_VERSION;
  Log->Source          = BOOT_TIME_HOTSPOT_SOURCE_PHASE_FALLBACK;
  Log->Count           = 1;
  Log->GaugeEntryCount = 0;
  Log->Hotspot[0].Rank       = 1;
  Log->Hotspot[0].DurationNs = GetDurationNsSafe (mEntryTick, NowTick);
  AsciiStrCpyS (Log->Hotspot[0].Module, sizeof (Log->Hotspot[0].Module), "PHASE");
  AsciiStrCpyS (Log->Hotspot[0].Token, sizeof (Log->Hotspot[0].Token), "Entry->ReadyToBoot");
}

STATIC
UINTN
CollectGauge (
  IN  PERFORMANCE_EX_PROTOCOL  *Perf,
  OUT HOTSPOT_CANDIDATE        *Pool,
  IN  UINTN                    Cap
  )
{
  UINTN  Key;
  UINTN  Count;

  Count = 0;
  for (Key = 0; Count < Cap; Key++) {
    EFI_STATUS           Status;
    GAUGE_DATA_ENTRY_EX  *Entry;
    UINT64               DurationNs;

    Status = Perf->GetGaugeEx (Key, &Entry);
    if (EFI_ERROR (Status)) {
      break;
    }

    DurationNs = GetDurationNsSafe (Entry->StartTimeStamp, Entry->EndTimeStamp);
    if (DurationNs == 0) {
      continue;
    }

    ZeroMem (&Pool[Count], sizeof (Pool[Count]));
    Pool[Count].DurationNs = DurationNs;
    CopyMem (Pool[Count].Module, Entry->Module, sizeof (Entry->Module));
    CopyMem (Pool[Count].Token, Entry->Token, sizeof (Entry->Token));
    Pool[Count].Module[DXE_PERFORMANCE_STRING_LENGTH] = '\0';
    Pool[Count].Token[DXE_PERFORMANCE_STRING_LENGTH]  = '\0';

    if (Pool[Count].Module[0] == '\0') {
      AsciiStrCpyS (Pool[Count].Module, sizeof (Pool[Count].Module), "UNKNOWN");
    }

    if (Pool[Count].Token[0] == '\0') {
      AsciiStrCpyS (Pool[Count].Token, sizeof (Pool[Count].Token), "UNKNOWN");
    }

    Count++;
  }

  return Count;
}

STATIC
VOID
SortByDurationDesc (
  IN OUT HOTSPOT_CANDIDATE  *Pool,
  IN UINTN                  Count
  )
{
  UINTN  I;
  UINTN  J;

  for (I = 0; I < Count; I++) {
    for (J = I + 1; J < Count; J++) {
      if (Pool[J].DurationNs > Pool[I].DurationNs) {
        HOTSPOT_CANDIDATE  Tmp;
        CopyMem (&Tmp, &Pool[I], sizeof (Tmp));
        CopyMem (&Pool[I], &Pool[J], sizeof (Tmp));
        CopyMem (&Pool[J], &Tmp, sizeof (Tmp));
      }
    }
  }
}

STATIC
VOID
FillFromGaugeOrFallback (
  OUT BOOT_TIME_HOTSPOT_LOG  *Log
  )
{
  EFI_STATUS               Status;
  PERFORMANCE_EX_PROTOCOL  *Perf;
  HOTSPOT_CANDIDATE        *Pool;
  UINTN                    Count;
  UINTN                    RankCount;
  UINTN                    Idx;

  Status = gBS->LocateProtocol (
                  &gPerformanceExProtocolGuid,
                  NULL,
                  (VOID **)&Perf
                  );
  if (EFI_ERROR (Status) || (Perf == NULL)) {
    FillPhaseFallback (Log);
    return;
  }

  Pool = AllocateZeroPool (sizeof (*Pool) * HOTSPOT_POOL_MAX);
  if (Pool == NULL) {
    FillPhaseFallback (Log);
    return;
  }

  Count = CollectGauge (Perf, Pool, HOTSPOT_POOL_MAX);
  if (Count == 0) {
    FreePool (Pool);
    FillPhaseFallback (Log);
    return;
  }

  SortByDurationDesc (Pool, Count);

  ZeroMem (Log, sizeof (*Log));
  Log->Signature       = BOOT_TIME_HOTSPOT_SIGNATURE;
  Log->Version         = BOOT_TIME_HOTSPOT_VERSION;
  Log->Source          = BOOT_TIME_HOTSPOT_SOURCE_GAUGE;
  Log->GaugeEntryCount = (UINT32)Count;

  RankCount = (Count < BOOT_TIME_HOTSPOT_MAX_RANK) ? Count : BOOT_TIME_HOTSPOT_MAX_RANK;
  Log->Count = (UINT8)RankCount;
  for (Idx = 0; Idx < RankCount; Idx++) {
    Log->Hotspot[Idx].Rank       = (UINT32)(Idx + 1);
    Log->Hotspot[Idx].DurationNs = Pool[Idx].DurationNs;
    CopyMem (Log->Hotspot[Idx].Module, Pool[Idx].Module, sizeof (Pool[Idx].Module));
    CopyMem (Log->Hotspot[Idx].Token, Pool[Idx].Token, sizeof (Pool[Idx].Token));
  }

  FreePool (Pool);
}

STATIC
VOID
EnsureSummaryFallback (
  VOID
  )
{
  EFI_STATUS         Status;
  BOOT_TIME_SUMMARY  Summary;
  UINTN              Size;
  UINT32             Attr;
  UINT16             BootCurrent;

  Size = sizeof (Summary);
  Status = gRT->GetVariable (
                  BOOT_TIME_SUMMARY_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  &Attr,
                  &Size,
                  &Summary
                  );
  if (!EFI_ERROR (Status)) {
    return;
  }

  ZeroMem (&Summary, sizeof (Summary));
  Summary.Signature              = BOOT_TIME_SUMMARY_SIGNATURE;
  Summary.Version                = BOOT_TIME_SUMMARY_VERSION;
  Summary.EntryTimeNs            = 0;
  Summary.EndOfDxeTimeNs         = 0;
  Summary.ReadyToBootTimeNs      = GetDurationNsSafe (mEntryTick, GetPerformanceCounter ());
  Summary.ExitBootServicesTimeNs = Summary.ReadyToBootTimeNs;
  Summary.DeltaEntryToEndOfDxeNs = 0;
  Summary.DeltaEndOfDxeToReadyToBootNs = Summary.ReadyToBootTimeNs;
  Summary.DeltaReadyToBootToExitBootServicesNs = 0;
  Summary.DeltaEntryToExitBootServicesNs = Summary.ExitBootServicesTimeNs;

  Size = sizeof (BootCurrent);
  Status = gRT->GetVariable (
                  L"BootCurrent",
                  &gEfiGlobalVariableGuid,
                  &Attr,
                  &Size,
                  &BootCurrent
                  );
  if (!EFI_ERROR (Status) && (Size == sizeof (BootCurrent))) {
    Summary.LastBootCurrent = BootCurrent;
  }

  Status = gRT->SetVariable (
                  BOOT_TIME_SUMMARY_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE |
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (Summary),
                  &Summary
                  );
  DEBUG ((DEBUG_INFO, "BootTimeHotspotDxe: Summary fallback SetVariable(%s) -> %r\n", BOOT_TIME_SUMMARY_VAR_NAME, Status));
}

STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS             Status;
  BOOT_TIME_HOTSPOT_LOG  Log;

  if (!FixedPcdGetBool (PcdBootTimeOptEnableHotspot)) {
    return;
  }

  EnsureSummaryFallback ();
  FillFromGaugeOrFallback (&Log);
  Status = gRT->SetVariable (
                  BOOT_TIME_HOTSPOT_LOG_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE |
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (Log),
                  &Log
                  );
  DEBUG ((DEBUG_INFO, "BootTimeHotspotDxe: SetVariable(%s) -> %r\n", BOOT_TIME_HOTSPOT_LOG_VAR_NAME, Status));
}

EFI_STATUS
EFIAPI
BootTimeHotspotDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  mEntryTick = GetPerformanceCounter ();
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );
  return Status;
}

/** @file
  Collects PerformanceEx gauge data and persists the Top-N hotspots.
  When gauge data is unavailable, records the DXE phases that actually ran.

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

STATIC EFI_EVENT  mEndOfDxeEvent     = NULL;
STATIC EFI_EVENT  mReadyToBootEvent  = NULL;
STATIC UINT64     mEntryTick         = 0;
STATIC UINT64     mEndOfDxeTick      = 0;
STATIC UINT64     mReadyToBootTick   = 0;

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
AddPhase (
  IN OUT BOOT_TIME_HOTSPOT_LOG  *Log,
  IN CONST CHAR8                *Module,
  IN CONST CHAR8                *Token,
  IN UINT64                     DurationNs
  )
{
  UINTN  Index;

  if (Log->Count >= BOOT_TIME_HOTSPOT_MAX_RANK) {
    return;
  }

  Index = Log->Count;
  Log->Hotspot[Index].Rank       = (UINT32)(Index + 1);
  Log->Hotspot[Index].DurationNs = DurationNs;
  AsciiStrCpyS (Log->Hotspot[Index].Module, sizeof (Log->Hotspot[Index].Module), Module);
  AsciiStrCpyS (Log->Hotspot[Index].Token, sizeof (Log->Hotspot[Index].Token), Token);
  Log->Count++;
}

STATIC
VOID
SortPhaseDesc (
  IN OUT BOOT_TIME_HOTSPOT_LOG  *Log
  )
{
  UINTN  Index;
  UINTN  Other;

  for (Index = 0; Index < Log->Count; Index++) {
    for (Other = Index + 1; Other < Log->Count; Other++) {
      if (Log->Hotspot[Other].DurationNs > Log->Hotspot[Index].DurationNs) {
        BOOT_TIME_HOTSPOT_ENTRY  Swap;
        CopyMem (&Swap, &Log->Hotspot[Index], sizeof (Swap));
        CopyMem (&Log->Hotspot[Index], &Log->Hotspot[Other], sizeof (Swap));
        CopyMem (&Log->Hotspot[Other], &Swap, sizeof (Swap));
      }
    }
  }

  for (Index = 0; Index < Log->Count; Index++) {
    Log->Hotspot[Index].Rank = (UINT32)(Index + 1);
  }
}

STATIC
VOID
FillPhaseFallback (
  OUT BOOT_TIME_HOTSPOT_LOG  *Log
  )
{
  UINT64  ReadyTick;

  ReadyTick = (mReadyToBootTick != 0) ? mReadyToBootTick : GetPerformanceCounter ();
  ZeroMem (Log, sizeof (*Log));
  Log->Signature       = BOOT_TIME_HOTSPOT_SIGNATURE;
  Log->Version         = BOOT_TIME_HOTSPOT_VERSION;
  Log->Source          = BOOT_TIME_HOTSPOT_SOURCE_PHASE_FALLBACK;
  Log->GaugeEntryCount = 0;

  if (mEndOfDxeTick != 0) {
    AddPhase (Log, "PHASE", "Entry->EndOfDxe", GetDurationNsSafe (mEntryTick, mEndOfDxeTick));
    AddPhase (Log, "PHASE", "EndOfDxe->ReadyToBoot", GetDurationNsSafe (mEndOfDxeTick, ReadyTick));
  } else {
    AddPhase (Log, "NOT_REACHED", "Entry->EndOfDxe", 0);
    AddPhase (Log, "PHASE", "Entry->ReadyToBoot", GetDurationNsSafe (mEntryTick, ReadyTick));
  }

  //
  // This log is written at ReadyToBoot. ExitBootServices has not run yet,
  // so record that explicitly instead of inventing a duration.
  //
  AddPhase (Log, "NOT_REACHED", "ExitBootServices", 0);
  SortPhaseDesc (Log);
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
  UINTN  Index;
  UINTN  Other;

  for (Index = 0; Index < Count; Index++) {
    for (Other = Index + 1; Other < Count; Other++) {
      if (Pool[Other].DurationNs > Pool[Index].DurationNs) {
        HOTSPOT_CANDIDATE  Swap;
        CopyMem (&Swap, &Pool[Index], sizeof (Swap));
        CopyMem (&Pool[Index], &Pool[Other], sizeof (Swap));
        CopyMem (&Pool[Other], &Swap, sizeof (Swap));
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
  UINTN                    Index;

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

  RankCount  = (Count < BOOT_TIME_HOTSPOT_MAX_RANK) ? Count : BOOT_TIME_HOTSPOT_MAX_RANK;
  Log->Count = (UINT8)RankCount;
  for (Index = 0; Index < RankCount; Index++) {
    Log->Hotspot[Index].Rank       = (UINT32)(Index + 1);
    Log->Hotspot[Index].DurationNs = Pool[Index].DurationNs;
    CopyMem (Log->Hotspot[Index].Module, Pool[Index].Module, sizeof (Pool[Index].Module));
    CopyMem (Log->Hotspot[Index].Token, Pool[Index].Token, sizeof (Pool[Index].Token));
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
  UINT64             NowTick;

  Size   = sizeof (Summary);
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

  NowTick = GetPerformanceCounter ();
  ZeroMem (&Summary, sizeof (Summary));
  Summary.Signature = BOOT_TIME_SUMMARY_SIGNATURE;
  Summary.Version   = BOOT_TIME_SUMMARY_VERSION;
  Summary.Reserved0 = BOOT_TIME_PHASE_READY_TO_BOOT;
  if (mEndOfDxeTick != 0) {
    Summary.Reserved0 |= BOOT_TIME_PHASE_END_OF_DXE;
  }

  Summary.ReadyToBootTimeNs            = GetDurationNsSafe (mEntryTick, NowTick);
  Summary.DeltaEntryToEndOfDxeNs       = GetDurationNsSafe (mEntryTick, mEndOfDxeTick);
  Summary.DeltaEndOfDxeToReadyToBootNs = (mEndOfDxeTick != 0) ?
                                         GetDurationNsSafe (mEndOfDxeTick, NowTick) :
                                         Summary.ReadyToBootTimeNs;

  Size   = sizeof (BootCurrent);
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
OnEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mEndOfDxeTick = GetPerformanceCounter ();
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

  mReadyToBootTick = GetPerformanceCounter ();
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
  Status     = gBS->CreateEventEx (
                      EVT_NOTIFY_SIGNAL,
                      TPL_CALLBACK,
                      OnEndOfDxe,
                      NULL,
                      &gEfiEndOfDxeEventGroupGuid,
                      &mEndOfDxeEvent
                      );
  if (EFI_ERROR (Status)) {
    return Status;
  }

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

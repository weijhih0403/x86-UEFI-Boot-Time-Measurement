#include <Uefi.h>

#include <Guid/EventGroup.h>
#include <Guid/GlobalVariable.h>
#include <Guid/BootTimeOptPkgVariable.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

STATIC EFI_EVENT  mEndOfDxeEvent          = NULL;
STATIC EFI_EVENT  mReadyToBootEvent       = NULL;
STATIC EFI_EVENT  mExitBootServicesEvent  = NULL;

STATIC UINT64     mEntryTick              = 0;
STATIC UINT64     mEndOfDxeTick           = 0;
STATIC UINT64     mReadyToBootTick        = 0;
STATIC UINT64     mExitBootServicesTick   = 0;
STATIC UINT16     mPhaseMask              = 0;

STATIC
UINT64
TicksToNanoSeconds (
  IN UINT64  DeltaTicks
  )
{
  return GetTimeInNanoSecond (DeltaTicks);
}

STATIC
UINT64
GetDeltaNsSafe (
  IN UINT64  StartTick,
  IN UINT64  EndTick
  )
{
  if ((StartTick == 0) || (EndTick == 0) || (EndTick < StartTick)) {
    return 0;
  }

  return TicksToNanoSeconds (EndTick - StartTick);
}

STATIC
VOID
ReadBootCurrent (
  OUT UINT16  *BootCurrent,
  OUT BOOLEAN *Valid
  )
{
  EFI_STATUS  Status;
  UINTN       DataSize;
  UINT32      Attributes;
  UINT16      Value;

  *Valid       = FALSE;
  *BootCurrent = 0;

  DataSize = sizeof (Value);
  Status   = gRT->GetVariable (
                    L"BootCurrent",
                    &gEfiGlobalVariableGuid,
                    &Attributes,
                    &DataSize,
                    &Value
                    );
  if (!EFI_ERROR (Status) && (DataSize == sizeof (Value))) {
    *BootCurrent = Value;
    *Valid       = TRUE;
  }
}

STATIC
VOID
PersistSummary (
  VOID
  )
{
  EFI_STATUS          Status;
  BOOT_TIME_SUMMARY   Summary;
  UINT16              BootCurrent;
  BOOLEAN             BootCurrentValid;

  ZeroMem (&Summary, sizeof (Summary));

  Summary.Signature              = BOOT_TIME_SUMMARY_SIGNATURE;
  Summary.Version                = BOOT_TIME_SUMMARY_VERSION;
  Summary.Reserved0              = mPhaseMask;
  Summary.EntryTimeNs            = 0;
  Summary.EndOfDxeTimeNs         = GetDeltaNsSafe (mEntryTick, mEndOfDxeTick);
  Summary.ReadyToBootTimeNs      = GetDeltaNsSafe (mEntryTick, mReadyToBootTick);
  Summary.ExitBootServicesTimeNs = GetDeltaNsSafe (mEntryTick, mExitBootServicesTick);

  Summary.DeltaEntryToEndOfDxeNs = Summary.EndOfDxeTimeNs;
  Summary.DeltaEndOfDxeToReadyToBootNs =
    GetDeltaNsSafe (mEndOfDxeTick, mReadyToBootTick);
  Summary.DeltaReadyToBootToExitBootServicesNs =
    GetDeltaNsSafe (mReadyToBootTick, mExitBootServicesTick);
  Summary.DeltaEntryToExitBootServicesNs =
    Summary.ExitBootServicesTimeNs;

  ReadBootCurrent (&BootCurrent, &BootCurrentValid);
  if (BootCurrentValid) {
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
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: SetVariable(%s) -> %r\n", BOOT_TIME_SUMMARY_VAR_NAME, Status));
}

STATIC
VOID
SaveLastSuccessfulBootOption (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       DataSize;
  UINT32      Attributes;
  UINT16      BootCurrent;

  DataSize = sizeof (BootCurrent);
  Status   = gRT->GetVariable (
                    L"BootCurrent",
                    &gEfiGlobalVariableGuid,
                    &Attributes,
                    &DataSize,
                    &BootCurrent
                    );
  if (EFI_ERROR (Status) || (DataSize != sizeof (BootCurrent))) {
    DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: BootCurrent unavailable, skip save (%r)\n", Status));
    return;
  }

  Status = gRT->SetVariable (
                  BOOT_TIME_LAST_BOOT_OPTION_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE |
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (BootCurrent),
                  &BootCurrent
                  );
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: Saved %s=Boot%04x (%r)\n", BOOT_TIME_LAST_BOOT_OPTION_VAR_NAME, BootCurrent, Status));
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
  mPhaseMask   |= BOOT_TIME_PHASE_END_OF_DXE;
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: EndOfDxe at %Lu ns from entry\n", TicksToNanoSeconds (mEndOfDxeTick - mEntryTick)));
  PersistSummary ();
}

STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mReadyToBootTick = GetPerformanceCounter ();
  mPhaseMask      |= BOOT_TIME_PHASE_READY_TO_BOOT;
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: ReadyToBoot at %Lu ns from entry\n", TicksToNanoSeconds (mReadyToBootTick - mEntryTick)));

  //
  // Leave ExitBootServices at 0 when it has not happened. A later callback
  // fills it in. Dump can tell "not reached" from a real zero-length phase
  // by reading the phase mask.
  //
  SaveLastSuccessfulBootOption ();
  PersistSummary ();
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  CHAR8   Line[128];
  UINTN   Len;
  UINT64  ReadyToEbsNs;
  UINT64  EntryToEbsNs;

  mExitBootServicesTick = GetPerformanceCounter ();
  mPhaseMask           |= BOOT_TIME_PHASE_EXIT_BOOT_SERVICES;

  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: ExitBootServices at %Lu ns from entry\n", TicksToNanoSeconds (mExitBootServicesTick - mEntryTick)));

  SaveLastSuccessfulBootOption ();
  PersistSummary ();

  //
  // ConOut/Print is unsafe around ExitBootServices. Append final EBS CSV lines
  // via SerialPortLib so the host benchmark can require ExitBootServicesReached=1.
  //
  ReadyToEbsNs = GetDeltaNsSafe (mReadyToBootTick, mExitBootServicesTick);
  EntryToEbsNs = GetDeltaNsSafe (mEntryTick, mExitBootServicesTick);

  Len = AsciiSPrint (Line, sizeof (Line), "summary,ReadyToBootToExitBootServicesNs,%Lu\n", ReadyToEbsNs);
  SerialPortWrite ((UINT8 *)Line, Len);
  Len = AsciiSPrint (Line, sizeof (Line), "summary,EntryToExitBootServicesNs,%Lu\n", EntryToEbsNs);
  SerialPortWrite ((UINT8 *)Line, Len);
  Len = AsciiSPrint (Line, sizeof (Line), "summary,ExitBootServicesReached,1\n");
  SerialPortWrite ((UINT8 *)Line, Len);

  DEBUG ((
    DEBUG_INFO,
    "BootTimeProfilerDxe: Summary(ns): E->EndDxe=%Lu, EndDxe->Ready=%Lu, Ready->EBS=%Lu, E->EBS=%Lu\n",
    TicksToNanoSeconds (mEndOfDxeTick - mEntryTick),
    TicksToNanoSeconds (mReadyToBootTick - mEndOfDxeTick),
    ReadyToEbsNs,
    EntryToEbsNs
    ));
}

EFI_STATUS
EFIAPI
BootTimeProfilerDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  mEntryTick            = GetPerformanceCounter ();
  mEndOfDxeTick         = 0;
  mReadyToBootTick      = 0;
  mExitBootServicesTick = 0;
  mPhaseMask            = 0;
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: Entry\n"));

  //
  // Persist an initial summary immediately so Shell-only paths can always
  // validate driver presence via dmpstore/BootTimeDump output.
  //
  SaveLastSuccessfulBootOption ();
  PersistSummary ();

  Status = gBS->CreateEventEx (
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
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

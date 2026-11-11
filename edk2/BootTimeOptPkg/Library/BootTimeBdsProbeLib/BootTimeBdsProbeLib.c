/** @file
  Record BDS Connect / Refresh / ReadyToBoot / LoadPath checkpoints.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/BootTimeOptPkgVariable.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BootTimeBdsProbeLib.h>
#include <Library/DebugLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

STATIC UINT64  mEndOfDxeTick    = 0;
STATIC UINT64  mConnectStart    = 0;
STATIC UINT64  mConnectEnd      = 0;
STATIC UINT64  mRefreshStart    = 0;
STATIC UINT64  mRefreshEnd      = 0;
STATIC UINT64  mReadyToBootTick = 0;

STATIC
UINT64
DeltaNsSafe (
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
PersistBreakdown (
  IN BOOT_TIME_BDS_BREAKDOWN  *Breakdown
  )
{
  EFI_STATUS  Status;

  if ((gRT == NULL) || (Breakdown == NULL)) {
    return;
  }

  Status = gRT->SetVariable (
                  BOOT_TIME_BDS_BREAKDOWN_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE |
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (*Breakdown),
                  Breakdown
                  );
  DEBUG ((DEBUG_INFO, "BootTimeBdsProbe: SetVariable(%s) -> %r\n", BOOT_TIME_BDS_BREAKDOWN_VAR_NAME, Status));
}

STATIC
BOOLEAN
LoadExisting (
  OUT BOOT_TIME_BDS_BREAKDOWN  *Breakdown
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  UINT32      Attr;

  if ((gRT == NULL) || (Breakdown == NULL)) {
    return FALSE;
  }

  Size   = sizeof (*Breakdown);
  Status = gRT->GetVariable (
                  BOOT_TIME_BDS_BREAKDOWN_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  &Attr,
                  &Size,
                  Breakdown
                  );
  if (EFI_ERROR (Status) ||
      (Size != sizeof (*Breakdown)) ||
      (Breakdown->Signature != BOOT_TIME_BDS_BREAKDOWN_SIGNATURE) ||
      (Breakdown->Version != BOOT_TIME_BDS_BREAKDOWN_VERSION))
  {
    return FALSE;
  }

  return TRUE;
}

VOID
EFIAPI
BootTimeBdsMarkEndOfDxe (
  VOID
  )
{
  BOOT_TIME_BDS_BREAKDOWN  B;

  mEndOfDxeTick    = GetPerformanceCounter ();
  mConnectStart    = 0;
  mConnectEnd      = 0;
  mRefreshStart    = 0;
  mRefreshEnd      = 0;
  mReadyToBootTick = 0;

  //
  // Drop any previous-boot / older-revision variable so GetVariable size matches.
  //
  if (gRT != NULL) {
    gRT->SetVariable (
           BOOT_TIME_BDS_BREAKDOWN_VAR_NAME,
           &gBootTimeOptPkgVariableGuid,
           0,
           0,
           NULL
           );
  }

  ZeroMem (&B, sizeof (B));
  B.Signature        = BOOT_TIME_BDS_BREAKDOWN_SIGNATURE;
  B.Version          = BOOT_TIME_BDS_BREAKDOWN_VERSION;
  B.Flags            = BOOT_TIME_BDS_FLAG_END_OF_DXE;
  B.EndOfDxePerfTick = mEndOfDxeTick;
  PersistBreakdown (&B);
}

VOID
EFIAPI
BootTimeBdsBeginConnect (
  VOID
  )
{
  mConnectStart = GetPerformanceCounter ();
}

VOID
EFIAPI
BootTimeBdsEndConnect (
  VOID
  )
{
  BOOT_TIME_BDS_BREAKDOWN  B;

  mConnectEnd = GetPerformanceCounter ();

  if (!LoadExisting (&B)) {
    ZeroMem (&B, sizeof (B));
    B.Signature = BOOT_TIME_BDS_BREAKDOWN_SIGNATURE;
    B.Version   = BOOT_TIME_BDS_BREAKDOWN_VERSION;
  }

  B.Flags                 |= BOOT_TIME_BDS_FLAG_END_OF_DXE | BOOT_TIME_BDS_FLAG_CONNECT;
  B.EndOfDxePerfTick       = (B.EndOfDxePerfTick != 0) ? B.EndOfDxePerfTick : mEndOfDxeTick;
  B.ConnectStartPerfTick   = mConnectStart;
  B.ConnectEndPerfTick     = mConnectEnd;
  B.PreConnectNs           = DeltaNsSafe (B.EndOfDxePerfTick, mConnectStart);
  B.ConnectAllNs           = DeltaNsSafe (mConnectStart, mConnectEnd);
  PersistBreakdown (&B);
}

VOID
EFIAPI
BootTimeBdsBeginRefresh (
  VOID
  )
{
  mRefreshStart = GetPerformanceCounter ();
}

VOID
EFIAPI
BootTimeBdsEndRefresh (
  VOID
  )
{
  BOOT_TIME_BDS_BREAKDOWN  B;

  mRefreshEnd = GetPerformanceCounter ();

  if (!LoadExisting (&B)) {
    ZeroMem (&B, sizeof (B));
    B.Signature = BOOT_TIME_BDS_BREAKDOWN_SIGNATURE;
    B.Version   = BOOT_TIME_BDS_BREAKDOWN_VERSION;
  }

  B.Flags                |= BOOT_TIME_BDS_FLAG_REFRESH;
  B.RefreshBootOptionsNs  = DeltaNsSafe (mRefreshStart, mRefreshEnd);
  B.RefreshEndPerfTick    = mRefreshEnd;
  PersistBreakdown (&B);
}

VOID
EFIAPI
BootTimeBdsOnReadyToBoot (
  VOID
  )
{
  BOOT_TIME_BDS_BREAKDOWN  B;

  mReadyToBootTick = GetPerformanceCounter ();

  if (!LoadExisting (&B)) {
    ZeroMem (&B, sizeof (B));
    B.Signature = BOOT_TIME_BDS_BREAKDOWN_SIGNATURE;
    B.Version   = BOOT_TIME_BDS_BREAKDOWN_VERSION;
  }

  B.Flags              |= BOOT_TIME_BDS_FLAG_READY_TO_BOOT;
  B.ReadyToBootPerfTick = mReadyToBootTick;
  if (B.RefreshEndPerfTick != 0) {
    B.PostRefreshToReadyToBootNs = DeltaNsSafe (B.RefreshEndPerfTick, mReadyToBootTick);
  } else if (B.ConnectEndPerfTick != 0) {
    B.PostRefreshToReadyToBootNs = DeltaNsSafe (B.ConnectEndPerfTick, mReadyToBootTick);
  } else {
    B.PostRefreshToReadyToBootNs = DeltaNsSafe (B.EndOfDxePerfTick, mReadyToBootTick);
  }

  PersistBreakdown (&B);
}

VOID
EFIAPI
BootTimeBdsMarkLoadPathAtAppEntry (
  VOID
  )
{
  BOOT_TIME_BDS_BREAKDOWN  Existing;
  UINT64                   ReadyTick;
  UINT64                   LoadPathNs;

  if (!LoadExisting (&Existing) ||
      ((Existing.Flags & BOOT_TIME_BDS_FLAG_READY_TO_BOOT) == 0) ||
      (Existing.ReadyToBootPerfTick == 0))
  {
    return;
  }

  ReadyTick  = Existing.ReadyToBootPerfTick;
  LoadPathNs = DeltaNsSafe (ReadyTick, GetPerformanceCounter ());

  Existing.Flags      |= BOOT_TIME_BDS_FLAG_LOAD_PATH;
  Existing.LoadPathNs  = LoadPathNs;
  PersistBreakdown (&Existing);
}

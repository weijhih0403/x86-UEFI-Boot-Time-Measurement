/** @file
  BDS 細分量測的實作（給只會 C 的讀者）

  這支 .c 像一本「碼表工具書」：
    - 別人在開機流程的關鍵點呼叫我們（開始／結束）
    - 我們讀硬體計時器（tick）
    - 算出各段花多少奈秒
    - 優先交給 BootTimeMeasurement Protocol（同開機即時服務）
    - Protocol 內部會再寫 NVRAM 快照；若 Protocol 尚未安裝才直接 SetVariable

  誰會呼叫？
    BdsPlatform.c     → MarkEndOfDxe / Begin&End Connect / Begin&End Refresh
    Profiler 驅動     → OnReadyToBoot
    BootTimeDump.efi  → MarkLoadPathAtAppEntry

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/BootTimeOptPkgVariable.h>
#include <Protocol/BootTimeMeasurement.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BootTimeBdsProbeLib.h>
#include <Library/DebugLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

/*
  STATIC = 只在本 .c 看得見（類似「檔案內私有全域變數」）。
  注意：不同 .efi 各自有一份，不會共用記憶體。
  所以跨程式要靠 Protocol 即時副本，或 NVRAM 裡的 *PerfTick 欄位。
*/
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

/*
  取得量測 Protocol（找不到回 NULL）。
  這是同開機跨模組「打電話」的入口。
*/
STATIC
EFI_BOOT_TIME_MEASUREMENT_PROTOCOL *
LocateMeasurement (
  VOID
  )
{
  EFI_STATUS                          Status;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;

  if (gBS == NULL) {
    return NULL;
  }

  Status = gBS->LocateProtocol (
                  &gEfiBootTimeMeasurementProtocolGuid,
                  NULL,
                  (VOID **)&Meas
                  );
  if (EFI_ERROR (Status) || (Meas == NULL)) {
    return NULL;
  }

  return Meas;
}

/*
  把一整包 BOOT_TIME_BDS_BREAKDOWN 交給量測服務（或直接寫 NVRAM）。
*/
STATIC
VOID
PersistBreakdown (
  IN BOOT_TIME_BDS_BREAKDOWN  *Breakdown
  )
{
  EFI_STATUS                          Status;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;

  if (Breakdown == NULL) {
    return;
  }

  Meas = LocateMeasurement ();
  if ((Meas != NULL) && (Meas->PublishBdsBreakdown != NULL)) {
    Status = Meas->PublishBdsBreakdown (Meas, Breakdown);
    DEBUG ((DEBUG_INFO, "BootTimeBdsProbe: PublishBds -> %r\n", Status));
    if (!EFI_ERROR (Status)) {
      return;
    }
  }

  /* Protocol 尚未就緒：退回直接寫 NVRAM（行為與舊版相同） */
  if (gRT == NULL) {
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

/*
  讀目前最新 BDS 細分：先問 Protocol，再問 NVRAM。
*/
STATIC
BOOLEAN
LoadExisting (
  OUT BOOT_TIME_BDS_BREAKDOWN  *Breakdown
  )
{
  EFI_STATUS                          Status;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;
  UINTN                               Size;
  UINT32                              Attr;

  if (Breakdown == NULL) {
    return FALSE;
  }

  Meas = LocateMeasurement ();
  if ((Meas != NULL) && (Meas->GetBdsBreakdown != NULL)) {
    Status = Meas->GetBdsBreakdown (Meas, Breakdown);
    if (!EFI_ERROR (Status) &&
        (Breakdown->Signature == BOOT_TIME_BDS_BREAKDOWN_SIGNATURE) &&
        (Breakdown->Version == BOOT_TIME_BDS_BREAKDOWN_VERSION))
    {
      return TRUE;
    }
  }

  if (gRT == NULL) {
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

  /*
    先刪掉舊的同名變數（DataSize=0）。
    避免上次開機存的「比較小的舊結構」害這次讀取 Buffer Too Small。
  */
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

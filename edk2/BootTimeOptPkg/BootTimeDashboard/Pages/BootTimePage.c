/** @file
  Read BootTimeSummary / BootTimeHotspotLog via existing package GUID/structs.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "BootTimePage.h"

#include <Guid/BootTimeOptPkgVariable.h>

#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "Ui/TextUi.h"

STATIC
VOID
BdAddMsOrNotReached (
  IN OUT BD_PAGE_BUFFER  *Buf,
  IN     CONST CHAR16    *Label,
  IN     UINT64           Ns,
  IN     BOOLEAN          Reached
  )
{
  if (!Reached) {
    BdPageBufferAdd (Buf, L"  %-36s Not reached", Label);
  } else {
    BdPageBufferAdd (
      Buf,
      L"  %-36s %u.%03u ms",
      Label,
      (UINT32)(Ns / 1000000ULL),
      (UINT32)((Ns / 1000ULL) % 1000ULL)
      );
  }
}

STATIC
EFI_STATUS
BdLoadSummary (
  OUT BOOT_TIME_SUMMARY  *Summary,
  OUT CHAR16             *ErrMsg,
  IN  UINTN               ErrMsgChars
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  UINT32      Attr;

  if ((Summary == NULL) || (ErrMsg == NULL) || (ErrMsgChars == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  ErrMsg[0] = L'\0';
  Size      = sizeof (*Summary);
  Status    = gRT->GetVariable (
                     BOOT_TIME_SUMMARY_VAR_NAME,
                     &gBootTimeOptPkgVariableGuid,
                     &Attr,
                     &Size,
                     Summary
                     );
  if (Status == EFI_NOT_FOUND) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"BootTimeSummary variable not found.");
    return Status;
  }

  if (EFI_ERROR (Status)) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"GetVariable(BootTimeSummary) failed: %r", Status);
    return Status;
  }

  if (Size < sizeof (*Summary)) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"BootTimeSummary size too small (%u).", (UINT32)Size);
    return EFI_INVALID_PARAMETER;
  }

  if (Summary->Signature != BOOT_TIME_SUMMARY_SIGNATURE) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"BootTimeSummary bad Signature.");
    return EFI_INVALID_PARAMETER;
  }

  if (Summary->Version != BOOT_TIME_SUMMARY_VERSION) {
    UnicodeSPrint (
      ErrMsg,
      ErrMsgChars * sizeof (CHAR16),
      L"BootTimeSummary unsupported Version %u (need %u).",
      Summary->Version,
      BOOT_TIME_SUMMARY_VERSION
      );
    return EFI_INCOMPATIBLE_VERSION;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
BdLoadHotspot (
  OUT BOOT_TIME_HOTSPOT_LOG  *Log,
  OUT CHAR16                 *ErrMsg,
  IN  UINTN                   ErrMsgChars,
  OUT BOOLEAN                *Present
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  UINT32      Attr;

  *Present  = FALSE;
  ErrMsg[0] = L'\0';
  Size      = sizeof (*Log);
  Status    = gRT->GetVariable (
                     BOOT_TIME_HOTSPOT_LOG_VAR_NAME,
                     &gBootTimeOptPkgVariableGuid,
                     &Attr,
                     &Size,
                     Log
                     );
  if (Status == EFI_NOT_FOUND) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"Hotspot log not present (collector off or never written).");
    return Status;
  }

  if (EFI_ERROR (Status)) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"GetVariable(HotspotLog) failed: %r", Status);
    return Status;
  }

  if (Size < OFFSET_OF (BOOT_TIME_HOTSPOT_LOG, Hotspot)) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"Hotspot log size too small (%u).", (UINT32)Size);
    return EFI_INVALID_PARAMETER;
  }

  if (Log->Signature != BOOT_TIME_HOTSPOT_SIGNATURE) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"Hotspot log bad Signature.");
    return EFI_INVALID_PARAMETER;
  }

  if (Log->Version != BOOT_TIME_HOTSPOT_VERSION) {
    UnicodeSPrint (
      ErrMsg,
      ErrMsgChars * sizeof (CHAR16),
      L"Hotspot log unsupported Version %u (need %u).",
      Log->Version,
      BOOT_TIME_HOTSPOT_VERSION
      );
    return EFI_INCOMPATIBLE_VERSION;
  }

  if (Log->Count > BOOT_TIME_HOTSPOT_MAX_RANK) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"Hotspot Count %u exceeds max.", Log->Count);
    return EFI_INVALID_PARAMETER;
  }

  *Present = TRUE;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdBootTimePage (
  VOID
  )
{
  BD_PAGE_BUFFER         Buf;
  BOOT_TIME_SUMMARY      Summary;
  BOOT_TIME_HOTSPOT_LOG  Hotspot;
  CHAR16                 Err[96];
  EFI_STATUS             Status;
  BOOLEAN                HotspotPresent;
  UINTN                  Index;
  CONST CHAR16          *SourceLabel;
  BOOLEAN                EndReached;
  BOOLEAN                ReadyReached;
  BOOLEAN                EbsReached;

  BdPageBufferReset (&Buf);
  BdPageBufferAdd (&Buf, L"  NOTE: Values are stored NVRAM records.");
  BdPageBufferAdd (&Buf, L"  They may be from a previous boot if Drivers did not");
  BdPageBufferAdd (&Buf, L"  overwrite them this boot. Do not treat existence alone");
  BdPageBufferAdd (&Buf, L"  as proof of a fresh measurement.");
  BdPageBufferAdd (&Buf, L"");
  BdPageBufferAdd (&Buf, L"  Profiler Entry = BootTimeProfilerDxe start (NOT power-on,");
  BdPageBufferAdd (&Buf, L"  NOT DXE phase start).");
  BdPageBufferAdd (&Buf, L"  Interactive mode (this app) does not call ExitBootServices,");
  BdPageBufferAdd (&Buf, L"  so EBS intervals stay Not reached here. Use BootTimeDump");
  BdPageBufferAdd (&Buf, L"  automated CSV runs if you need EBS timings.");
  BdPageBufferAdd (&Buf, L"");

  Status = BdLoadSummary (&Summary, Err, ARRAY_SIZE (Err));
  if (EFI_ERROR (Status)) {
    BdPageBufferAdd (&Buf, L"[ BootTimeSummary ]");
    BdPageBufferAdd (&Buf, L"  %s", Err);
  } else {
    EndReached   = (BOOLEAN)((Summary.Reserved0 & BOOT_TIME_PHASE_END_OF_DXE) != 0);
    ReadyReached = (BOOLEAN)((Summary.Reserved0 & BOOT_TIME_PHASE_READY_TO_BOOT) != 0);
    EbsReached   = (BOOLEAN)((Summary.Reserved0 & BOOT_TIME_PHASE_EXIT_BOOT_SERVICES) != 0);

    BdPageBufferAdd (&Buf, L"[ Phase intervals ]");
    BdAddMsOrNotReached (
      &Buf,
      L"Profiler Entry -> EndOfDxe",
      Summary.DeltaEntryToEndOfDxeNs,
      EndReached
      );
    BdAddMsOrNotReached (
      &Buf,
      L"EndOfDxe -> ReadyToBoot",
      Summary.DeltaEndOfDxeToReadyToBootNs,
      (BOOLEAN)(EndReached && ReadyReached)
      );
    BdAddMsOrNotReached (
      &Buf,
      L"ReadyToBoot -> ExitBootServices",
      Summary.DeltaReadyToBootToExitBootServicesNs,
      (BOOLEAN)(ReadyReached && EbsReached)
      );
    BdAddMsOrNotReached (
      &Buf,
      L"Profiler Entry -> ExitBootServices",
      Summary.DeltaEntryToExitBootServicesNs,
      EbsReached
      );
    BdPageBufferAdd (
      &Buf,
      L"  Flags  EndOfDxe=%u ReadyToBoot=%u ExitBootServices=%u",
      EndReached ? 1 : 0,
      ReadyReached ? 1 : 0,
      EbsReached ? 1 : 0
      );
    BdPageBufferAdd (&Buf, L"  LastBootCurrent          Boot%04x", Summary.LastBootCurrent);
  }

  BdPageBufferAdd (&Buf, L"");
  BdPageBufferAdd (&Buf, L"[ BDS breakdown  EndOfDxe->ReadyToBoot ]");
  {
    BOOT_TIME_BDS_BREAKDOWN  Bds;
    UINTN                    BdsSize;
    UINT32                   BdsAttr;

    BdsSize = sizeof (Bds);
    Status  = gRT->GetVariable (
                     BOOT_TIME_BDS_BREAKDOWN_VAR_NAME,
                     &gBootTimeOptPkgVariableGuid,
                     &BdsAttr,
                     &BdsSize,
                     &Bds
                     );
    if (EFI_ERROR (Status) ||
        (BdsSize != sizeof (Bds)) ||
        (Bds.Signature != BOOT_TIME_BDS_BREAKDOWN_SIGNATURE))
    {
      BdPageBufferAdd (&Buf, L"  unavailable (%r)", Status);
    } else {
      BdAddMsOrNotReached (
        &Buf,
        L"PreConnect (EoD->Connect)",
        Bds.PreConnectNs,
        (BOOLEAN)((Bds.Flags & BOOT_TIME_BDS_FLAG_CONNECT) != 0)
        );
      BdAddMsOrNotReached (
        &Buf,
        L"ConnectAll",
        Bds.ConnectAllNs,
        (BOOLEAN)((Bds.Flags & BOOT_TIME_BDS_FLAG_CONNECT) != 0)
        );
      BdAddMsOrNotReached (
        &Buf,
        L"RefreshBootOptions",
        Bds.RefreshBootOptionsNs,
        (BOOLEAN)((Bds.Flags & BOOT_TIME_BDS_FLAG_REFRESH) != 0)
        );
      BdAddMsOrNotReached (
        &Buf,
        L"PostRefresh -> ReadyToBoot",
        Bds.PostRefreshToReadyToBootNs,
        (BOOLEAN)((Bds.Flags & BOOT_TIME_BDS_FLAG_READY_TO_BOOT) != 0)
        );
      BdAddMsOrNotReached (
        &Buf,
        L"LoadPath (RTB->Dump entry)",
        Bds.LoadPathNs,
        (BOOLEAN)((Bds.Flags & BOOT_TIME_BDS_FLAG_LOAD_PATH) != 0)
        );
      BdPageBufferAdd (&Buf, L"  Flags                    0x%x", (UINT32)Bds.Flags);
    }
  }

  BdPageBufferAdd (&Buf, L"");
  BdPageBufferAdd (&Buf, L"[ Hotspot / duration items ]");
  Status = BdLoadHotspot (&Hotspot, Err, ARRAY_SIZE (Err), &HotspotPresent);
  if (!HotspotPresent) {
    BdPageBufferAdd (&Buf, L"  %s", Err);
  } else {
    if (Hotspot.Source == BOOT_TIME_HOTSPOT_SOURCE_GAUGE) {
      SourceLabel = L"Performance gauge records (not phase fallback)";
    } else if (Hotspot.Source == BOOT_TIME_HOTSPOT_SOURCE_PHASE_FALLBACK) {
      SourceLabel = L"Phase fallback intervals (NOT per-driver ranking)";
    } else {
      SourceLabel = L"Unknown source";
    }

    BdPageBufferAdd (&Buf, L"  Source: %s", SourceLabel);
    BdPageBufferAdd (&Buf, L"  GaugeEntryCount=%u  Count=%u", Hotspot.GaugeEntryCount, Hotspot.Count);

    if (Hotspot.Count == 0) {
      BdPageBufferAdd (&Buf, L"  (no ranked entries)");
    } else {
      for (Index = 0; Index < Hotspot.Count; Index++) {
        BdPageBufferAdd (
          &Buf,
          L"  #%u  %a / %a",
          Hotspot.Hotspot[Index].Rank,
          Hotspot.Hotspot[Index].Module,
          Hotspot.Hotspot[Index].Token
          );
        if (AsciiStrCmp (Hotspot.Hotspot[Index].Module, "NOT_REACHED") == 0) {
          BdPageBufferAdd (&Buf, L"      Not reached");
        } else {
          BdPageBufferAdd (
            &Buf,
            L"      %u.%03u ms",
            (UINT32)(Hotspot.Hotspot[Index].DurationNs / 1000000ULL),
            (UINT32)((Hotspot.Hotspot[Index].DurationNs / 1000ULL) % 1000ULL)
            );
        }
      }
    }
  }

  return BdShowScrollablePage (L"Boot-Time Measurements", &Buf);
}

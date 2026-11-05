/** @file
  Print BootTimeSummary / BootTimeHotspotLog / LastSuccessfulBootOption.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/BootTimeOptPkgVariable.h>

#include <Library/PcdLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

STATIC
VOID
DumpSummary (
  IN BOOLEAN  Csv
  )
{
  EFI_STATUS         Status;
  BOOT_TIME_SUMMARY  S;
  UINTN              Size;
  UINT32             Attr;

  Size = sizeof (S);
  Status = gRT->GetVariable (
                  BOOT_TIME_SUMMARY_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  &Attr,
                  &Size,
                  &S
                  );
  if (EFI_ERROR (Status)) {
    if (Csv) {
      Print (L"summary,error,%r\n", Status);
    } else {
      Print (L"BootTimeSummary: not found (%r)\n", Status);
    }
    return;
  }

  if (Csv) {
    Print (L"section,metric,value\n");
    Print (L"summary,EntryToEndOfDxeNs,%Lu\n", S.DeltaEntryToEndOfDxeNs);
    Print (L"summary,EndOfDxeToReadyToBootNs,%Lu\n", S.DeltaEndOfDxeToReadyToBootNs);
    Print (L"summary,ReadyToBootToExitBootServicesNs,%Lu\n", S.DeltaReadyToBootToExitBootServicesNs);
    Print (L"summary,EntryToExitBootServicesNs,%Lu\n", S.DeltaEntryToExitBootServicesNs);
    Print (L"summary,EndOfDxeReached,%u\n", (UINT32)((S.Reserved0 & BOOT_TIME_PHASE_END_OF_DXE) != 0));
    Print (L"summary,ReadyToBootReached,%u\n", (UINT32)((S.Reserved0 & BOOT_TIME_PHASE_READY_TO_BOOT) != 0));
    Print (L"summary,ExitBootServicesReached,%u\n", (UINT32)((S.Reserved0 & BOOT_TIME_PHASE_EXIT_BOOT_SERVICES) != 0));
    Print (L"summary,PolicyEnabled,%u\n", (UINT32)FixedPcdGetBool (PcdBootTimeOptEnablePolicy));
    Print (L"summary,LastBootCurrent,%u\n", (UINT32)S.LastBootCurrent);
  } else {
    Print (L"=== BootTimeSummary ===\n");
    Print (L"Entry->EndOfDxe:               %Lu ns\n", S.DeltaEntryToEndOfDxeNs);
    Print (L"EndOfDxe->ReadyToBoot:         %Lu ns\n", S.DeltaEndOfDxeToReadyToBootNs);
    Print (L"ReadyToBoot->ExitBootServices: %Lu ns\n", S.DeltaReadyToBootToExitBootServicesNs);
    Print (L"Entry->ExitBootServices:       %Lu ns\n", S.DeltaEntryToExitBootServicesNs);
    Print (L"EndOfDxe reached:              %a\n", (S.Reserved0 & BOOT_TIME_PHASE_END_OF_DXE) ? "yes" : "no");
    Print (L"ReadyToBoot reached:           %a\n", (S.Reserved0 & BOOT_TIME_PHASE_READY_TO_BOOT) ? "yes" : "no");
    Print (L"ExitBootServices reached:      %a\n", (S.Reserved0 & BOOT_TIME_PHASE_EXIT_BOOT_SERVICES) ? "yes" : "no");
    Print (L"Policy enabled:                %a\n", FixedPcdGetBool (PcdBootTimeOptEnablePolicy) ? "yes" : "no");
    Print (L"LastBootCurrent:               Boot%04x\n", S.LastBootCurrent);
  }
}

STATIC
VOID
DumpHotspot (
  IN BOOLEAN  Csv
  )
{
  EFI_STATUS              Status;
  BOOT_TIME_HOTSPOT_LOG   H;
  UINTN                   Size;
  UINT32                  Attr;
  UINTN                   Idx;

  Size = sizeof (H);
  Status = gRT->GetVariable (
                  BOOT_TIME_HOTSPOT_LOG_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  &Attr,
                  &Size,
                  &H
                  );
  if (EFI_ERROR (Status)) {
    if (Csv) {
      Print (L"hotspot,error,%r\n", Status);
    } else {
      Print (L"BootTimeHotspotLog: not found (%r)\n", Status);
    }
    return;
  }

  if (Csv) {
    Print (L"rank,source,duration_ns,module,token\n");
  } else {
    Print (L"=== BootTimeHotspotLog ===\n");
  }

  for (Idx = 0; Idx < H.Count && Idx < BOOT_TIME_HOTSPOT_MAX_RANK; Idx++) {
    if (Csv) {
      Print (
        L"%u,%a,%Lu,%a,%a\n",
        (UINT32)H.Hotspot[Idx].Rank,
        (H.Source == BOOT_TIME_HOTSPOT_SOURCE_GAUGE) ? "gauge" : "phase",
        H.Hotspot[Idx].DurationNs,
        H.Hotspot[Idx].Module,
        H.Hotspot[Idx].Token
        );
    } else {
      Print (
        L"#%u %Lu ns [%a] %a\n",
        (UINT32)H.Hotspot[Idx].Rank,
        H.Hotspot[Idx].DurationNs,
        H.Hotspot[Idx].Module,
        H.Hotspot[Idx].Token
        );
    }
  }
}

STATIC
VOID
DumpLastBootOption (
  IN BOOLEAN  Csv
  )
{
  EFI_STATUS  Status;
  UINT16      BootCurrent;
  UINTN       Size;
  UINT32      Attr;

  Size = sizeof (BootCurrent);
  Status = gRT->GetVariable (
                  BOOT_TIME_LAST_BOOT_OPTION_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  &Attr,
                  &Size,
                  &BootCurrent
                  );
  if (EFI_ERROR (Status)) {
    if (Csv) {
      Print (L"last_boot,error,%r\n", Status);
    } else {
      Print (L"LastSuccessfulBootOption: not found (%r)\n", Status);
    }
    return;
  }

  if (Csv) {
    Print (L"last_successful_bootoption,%u\n", (UINT32)BootCurrent);
  } else {
    Print (L"LastSuccessfulBootOption: Boot%04x\n", BootCurrent);
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  BOOLEAN  Csv;

  Csv = TRUE;
  DumpSummary (Csv);
  DumpHotspot (Csv);
  DumpLastBootOption (Csv);
  return EFI_SUCCESS;
}

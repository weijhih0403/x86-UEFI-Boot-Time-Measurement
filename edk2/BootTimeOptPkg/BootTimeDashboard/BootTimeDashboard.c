/** @file
  BootTimeDashboard — interactive viewer for boot-time measurements and
  system/memory diagnostics. Does NOT call ExitBootServices.

  UI / SMBIOS / memory-map logic adapted from:
  https://github.com/weijhih0403/FirmwareDashboardPkg

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "Common/BdTypes.h"
#include "Pages/BootTimePage.h"
#include "Pages/MemoryMapPage.h"
#include "Pages/SystemInfoPage.h"
#include "Ui/TextUi.h"

#define BOOT_TIME_DASHBOARD_NAME  L"BootTimeDashboard"

STATIC
EFI_STATUS
EFIAPI
BdExitPage (
  VOID
  )
{
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS          Status;
  UINTN               Selected;
  BOOLEAN             Escaped;
  BOOLEAN             Running;
  CONST BD_MENU_ITEM  Menu[] = {
    { L"Boot-Time Measurements", BdBootTimePage,   FALSE },
    { L"System Information",     BdSystemInfoPage, FALSE },
    { L"Memory Map",             BdMemoryMapPage,  FALSE },
    { L"Exit",                   BdExitPage,       TRUE  },
  };

  (VOID)ImageHandle;
  (VOID)SystemTable;

  if ((gST == NULL) || (gST->ConOut == NULL) || (gST->ConIn == NULL)) {
    return EFI_UNSUPPORTED;
  }

  BdUiSaveConsoleState ();

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"%s\n", BOOT_TIME_DASHBOARD_NAME);
  Print (L"Interactive viewer. Does not accelerate boot. Does not call ExitBootServices.\n");

  Running = TRUE;
  while (Running) {
    Status = BdRunTextMenu (
               BOOT_TIME_DASHBOARD_NAME,
               Menu,
               ARRAY_SIZE (Menu),
               &Selected,
               &Escaped
               );
    if (EFI_ERROR (Status)) {
      Print (L"Menu error: %r\n", Status);
      BdUiRestoreConsoleState ();
      return Status;
    }

    if (Escaped) {
      break;
    }

    if (Selected >= ARRAY_SIZE (Menu)) {
      continue;
    }

    Status = Menu[Selected].Handler ();
    if (EFI_ERROR (Status)) {
      Print (L"Page error: %r\n", Status);
    }

    if (Menu[Selected].ExitAfter) {
      Running = FALSE;
    }
  }

  BdUiRestoreConsoleState ();
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->ClearScreen (gST->ConOut);
    Print (L"%s: bye.\n", BOOT_TIME_DASHBOARD_NAME);
  }

  return EFI_SUCCESS;
}

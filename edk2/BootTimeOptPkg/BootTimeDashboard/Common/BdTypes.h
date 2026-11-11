/** @file
  Shared types for BootTimeDashboard UI.

  This header only defines the menu contract used by BootTimeDashboard.c
  and Ui/TextUi.c. Page content buffers (BD_PAGE_BUFFER) live in TextUi.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef BD_TYPES_H_
#define BD_TYPES_H_

#include <Uefi.h>

/**
  Callback for one menu entry.

  Each page (Boot-Time / System Info / Memory Map / Exit) implements this
  signature. The menu loop calls Handler after the user selects the item.

  @retval EFI_SUCCESS  Page finished normally (or Exit stub returned).
  @retval Others       Propagated if a page wants to abort the app.
**/
typedef
EFI_STATUS
(EFIAPI *BD_PAGE_HANDLER) (
  VOID
  );

/**
  One row in the text-mode main menu.

  Example in BootTimeDashboard.c:
    { L"Boot-Time Measurements", BdBootTimePage, FALSE }
    { L"Exit",                   BdExitPage,     TRUE  }

  Title      Display string in the menu list (Unicode).
  Handler    Function invoked when this row is selected.
  ExitAfter  TRUE  -> after Handler returns, leave the menu loop (quit app).
             FALSE -> after Handler returns, redraw the menu and continue.
**/
typedef struct {
  CONST CHAR16       *Title;
  BD_PAGE_HANDLER     Handler;
  BOOLEAN             ExitAfter;
} BD_MENU_ITEM;

#endif

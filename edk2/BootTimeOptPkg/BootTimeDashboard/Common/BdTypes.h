/** @file
  Shared types for BootTimeDashboard UI.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef BD_TYPES_H_
#define BD_TYPES_H_

#include <Uefi.h>

typedef
EFI_STATUS
(EFIAPI *BD_PAGE_HANDLER) (
  VOID
  );

typedef struct {
  CONST CHAR16      *Title;
  BD_PAGE_HANDLER    Handler;
  BOOLEAN            ExitAfter;
} BD_MENU_ITEM;

#endif

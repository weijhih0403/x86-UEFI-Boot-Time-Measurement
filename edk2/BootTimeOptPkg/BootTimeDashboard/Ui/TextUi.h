/** @file
  Text-mode menu and page helpers.

  Adapted from FirmwareDashboardPkg.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef BD_TEXT_UI_H_
#define BD_TEXT_UI_H_

#include "Common/BdTypes.h"

#define BD_MAX_PAGE_LINES  96
#define BD_LINE_CHARS      120

typedef struct {
  CHAR16  Lines[BD_MAX_PAGE_LINES][BD_LINE_CHARS];
  UINTN   Count;
} BD_PAGE_BUFFER;

VOID
EFIAPI
BdUiSaveConsoleState (
  VOID
  );

VOID
EFIAPI
BdUiRestoreConsoleState (
  VOID
  );

VOID
EFIAPI
BdSetNormalAttr (
  VOID
  );

VOID
EFIAPI
BdDrawPageFrame (
  IN CONST CHAR16  *Title
  );

EFI_STATUS
EFIAPI
BdRunTextMenu (
  IN  CONST CHAR16        *Title,
  IN  CONST BD_MENU_ITEM  *Items,
  IN  UINTN                ItemCount,
  OUT UINTN               *Selected,
  OUT BOOLEAN             *Escaped
  );

VOID
EFIAPI
BdPageBufferReset (
  IN OUT BD_PAGE_BUFFER  *Buf
  );

VOID
EFIAPI
BdPageBufferAdd (
  IN OUT BD_PAGE_BUFFER  *Buf,
  IN     CONST CHAR16    *Format,
  ...
  );

/**
  Show buffered lines with Up/Down/PgUp/PgDn scroll; Enter/Esc dismiss.
**/
EFI_STATUS
EFIAPI
BdShowScrollablePage (
  IN CONST CHAR16        *Title,
  IN CONST BD_PAGE_BUFFER  *Buf
  );

#endif

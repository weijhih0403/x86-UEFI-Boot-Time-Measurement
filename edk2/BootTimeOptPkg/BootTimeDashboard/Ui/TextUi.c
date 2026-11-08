/** @file
  Text-mode menu UI and scrollable page viewer.

  Adapted from FirmwareDashboardPkg; scroll support added for long pages.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "TextUi.h"
#include "Input.h"

#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

// UnicodeVSPrint is provided by PrintLib.

STATIC INT32    mSavedAttribute = -1;
STATIC BOOLEAN  mSavedCursorVisible = TRUE;
STATIC INT32    mSavedCursorCol = 0;
STATIC INT32    mSavedCursorRow = 0;

VOID
EFIAPI
BdSetNormalAttr (
  VOID
  )
{
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->SetAttribute (
                   gST->ConOut,
                   EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK)
                   );
  }
}

STATIC
VOID
BdSetBannerAttr (
  VOID
  )
{
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->SetAttribute (
                   gST->ConOut,
                   EFI_TEXT_ATTR (EFI_WHITE, EFI_BLUE)
                   );
  }
}

STATIC
VOID
BdSetAccentAttr (
  VOID
  )
{
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->SetAttribute (
                   gST->ConOut,
                   EFI_TEXT_ATTR (EFI_LIGHTCYAN, EFI_BLACK)
                   );
  }
}

STATIC
VOID
BdSetHighlightAttr (
  VOID
  )
{
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->SetAttribute (
                   gST->ConOut,
                   EFI_TEXT_ATTR (EFI_BLACK, EFI_LIGHTCYAN)
                   );
  }
}

VOID
EFIAPI
BdUiSaveConsoleState (
  VOID
  )
{
  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return;
  }

  mSavedAttribute     = (INT32)gST->ConOut->Mode->Attribute;
  mSavedCursorVisible = (BOOLEAN)gST->ConOut->Mode->CursorVisible;
  mSavedCursorCol     = gST->ConOut->Mode->CursorColumn;
  mSavedCursorRow     = gST->ConOut->Mode->CursorRow;
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
}

VOID
EFIAPI
BdUiRestoreConsoleState (
  VOID
  )
{
  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return;
  }

  if (mSavedAttribute >= 0) {
    gST->ConOut->SetAttribute (gST->ConOut, (UINTN)mSavedAttribute);
  } else {
    BdSetNormalAttr ();
  }

  gST->ConOut->EnableCursor (gST->ConOut, mSavedCursorVisible);
  gST->ConOut->SetCursorPosition (
                 gST->ConOut,
                 (UINTN)mSavedCursorCol,
                 (UINTN)mSavedCursorRow
                 );
}

VOID
EFIAPI
BdDrawPageFrame (
  IN CONST CHAR16  *Title
  )
{
  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return;
  }

  gST->ConOut->ClearScreen (gST->ConOut);
  BdSetBannerAttr ();
  Print (L"                                                                                \n");
  Print (L"  %-76s\n", (Title != NULL) ? Title : L"BootTimeDashboard");
  Print (L"                                                                                \n");
  BdSetNormalAttr ();
  BdSetAccentAttr ();
  Print (L"  +--------------------------------------------------------------------------+\n");
  BdSetNormalAttr ();
  Print (L"\n");
}

STATIC
VOID
BdDrawMenu (
  IN CONST CHAR16       *Title,
  IN CONST BD_MENU_ITEM *Items,
  IN UINTN               ItemCount,
  IN UINTN               Cursor
  )
{
  UINTN  Index;

  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return;
  }

  gST->ConOut->ClearScreen (gST->ConOut);

  BdSetBannerAttr ();
  Print (L"                                                                                \n");
  Print (L"  %-40s  text UI                                 \n",
         (Title != NULL) ? Title : L"BootTimeDashboard");
  Print (L"  Boot-time measurement viewer (not an optimizer)                               \n");
  Print (L"                                                                                \n");
  BdSetNormalAttr ();

  BdSetAccentAttr ();
  Print (L"\n  +--------------------------- Main Menu ------------------------------------+\n");
  BdSetNormalAttr ();
  Print (L"  |  Up/Down move   Enter select   Esc quit                                  |\n");
  BdSetAccentAttr ();
  Print (L"  +--------------------------------------------------------------------------+\n\n");
  BdSetNormalAttr ();

  for (Index = 0; Index < ItemCount; Index++) {
    if (Index == Cursor) {
      BdSetHighlightAttr ();
      Print (L"    >> %-42s <<\n", Items[Index].Title);
      BdSetNormalAttr ();
    } else {
      Print (L"       %-42s\n", Items[Index].Title);
    }
  }
}

EFI_STATUS
EFIAPI
BdRunTextMenu (
  IN  CONST CHAR16       *Title,
  IN  CONST BD_MENU_ITEM *Items,
  IN  UINTN               ItemCount,
  OUT UINTN              *Selected,
  OUT BOOLEAN            *Escaped
  )
{
  EFI_STATUS     Status;
  EFI_INPUT_KEY  Key;
  UINTN          Cursor;

  if ((Items == NULL) || (ItemCount == 0) || (Selected == NULL) || (Escaped == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Cursor   = 0;
  *Escaped = FALSE;
  BdFlushKeys ();

  for ( ; ;) {
    BdDrawMenu (Title, Items, ItemCount, Cursor);

    Status = BdWaitForKey (&Key);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Key.ScanCode == SCAN_UP) {
      if (Cursor == 0) {
        Cursor = ItemCount - 1;
      } else {
        Cursor--;
      }
    } else if (Key.ScanCode == SCAN_DOWN) {
      Cursor++;
      if (Cursor >= ItemCount) {
        Cursor = 0;
      }
    } else if (Key.ScanCode == SCAN_ESC) {
      *Escaped = TRUE;
      return EFI_SUCCESS;
    } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      *Selected = Cursor;
      return EFI_SUCCESS;
    }
  }
}

VOID
EFIAPI
BdPageBufferReset (
  IN OUT BD_PAGE_BUFFER  *Buf
  )
{
  if (Buf != NULL) {
    Buf->Count = 0;
  }
}

VOID
EFIAPI
BdPageBufferAdd (
  IN OUT BD_PAGE_BUFFER  *Buf,
  IN     CONST CHAR16    *Format,
  ...
  )
{
  VA_LIST  Args;

  if ((Buf == NULL) || (Format == NULL) || (Buf->Count >= BD_MAX_PAGE_LINES)) {
    return;
  }

  VA_START (Args, Format);
  UnicodeVSPrint (Buf->Lines[Buf->Count], sizeof (Buf->Lines[0]), Format, Args);
  VA_END (Args);
  Buf->Count++;
}

STATIC
UINTN
BdVisibleRows (
  VOID
  )
{
  UINTN  Cols;
  UINTN  Rows;

  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return 20;
  }

  if (EFI_ERROR (gST->ConOut->QueryMode (
                                gST->ConOut,
                                gST->ConOut->Mode->Mode,
                                &Cols,
                                &Rows
                                )))
  {
    return 20;
  }

  // Banner (~5) + footer (~3)
  if (Rows > 10) {
    return Rows - 8;
  }

  return 12;
}

EFI_STATUS
EFIAPI
BdShowScrollablePage (
  IN CONST CHAR16          *Title,
  IN CONST BD_PAGE_BUFFER  *Buf
  )
{
  EFI_STATUS     Status;
  EFI_INPUT_KEY  Key;
  UINTN          Scroll;
  UINTN          Visible;
  UINTN          Index;
  UINTN          End;
  UINTN          MaxScroll;

  if (Buf == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Scroll  = 0;
  Visible = BdVisibleRows ();
  BdFlushKeys ();

  for ( ; ;) {
    BdDrawPageFrame (Title);
    if (Buf->Count == 0) {
      Print (L"  (empty)\n");
    } else {
      End = Scroll + Visible;
      if (End > Buf->Count) {
        End = Buf->Count;
      }

      for (Index = Scroll; Index < End; Index++) {
        Print (L"%s\n", Buf->Lines[Index]);
      }
    }

    MaxScroll = 0;
    if (Buf->Count > Visible) {
      MaxScroll = Buf->Count - Visible;
    }

    BdSetAccentAttr ();
    Print (
      L"\n  Lines %u-%u / %u   Up/Down/PgUp/PgDn scroll   Enter/Esc return\n",
      (Buf->Count == 0) ? 0 : (UINT32)(Scroll + 1),
      (UINT32)((Scroll + Visible > Buf->Count) ? Buf->Count : (Scroll + Visible)),
      (UINT32)Buf->Count
      );
    BdSetNormalAttr ();

    Status = BdWaitForKey (&Key);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == CHAR_CARRIAGE_RETURN)) {
      return EFI_SUCCESS;
    }

    if (Key.ScanCode == SCAN_UP) {
      if (Scroll > 0) {
        Scroll--;
      }
    } else if (Key.ScanCode == SCAN_DOWN) {
      if (Scroll < MaxScroll) {
        Scroll++;
      }
    } else if (Key.ScanCode == SCAN_PAGE_UP) {
      if (Scroll > Visible) {
        Scroll -= Visible;
      } else {
        Scroll = 0;
      }
    } else if (Key.ScanCode == SCAN_PAGE_DOWN) {
      Scroll += Visible;
      if (Scroll > MaxScroll) {
        Scroll = MaxScroll;
      }
    }
  }
}

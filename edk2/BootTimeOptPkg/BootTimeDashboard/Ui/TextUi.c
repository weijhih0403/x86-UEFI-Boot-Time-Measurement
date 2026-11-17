/** @file
  Text-mode menu UI and scrollable page viewer.

  Adapted from FirmwareDashboardPkg; scroll support added for long pages.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// ============================================================================
// 【這個檔案是什麼？】
// 整個程式的「畫面層」：負責設定顏色、畫標題橫幅、畫主選單、
// 以及可上下捲動的頁面。可以分成幾組功能：
//
//   1. 顏色設定  : BdSetNormalAttr / BdSetBannerAttr / BdSetAccentAttr / BdSetHighlightAttr
//   2. 終端狀態  : BdUiSaveConsoleState / BdUiRestoreConsoleState（備份與還原）
//   3. 畫面繪製  : BdDrawPageFrame / BdDrawMenu
//   4. 選單迴圈  : BdRunTextMenu（處理上下鍵與 Enter / Esc）
//   5. 頁面緩衝  : BdPageBufferReset / BdPageBufferAdd
//   6. 捲動頁面  : BdVisibleRows / BdShowScrollablePage
//
// 【UEFI 文字輸出怎麼做？用 C 的角度理解】
// gST->ConOut 是一個 struct 指標，struct 裡面放了許多「函式指標」：
//   ClearScreen、SetAttribute、SetCursorPosition、EnableCursor、QueryMode ...
// 呼叫方式是 gST->ConOut->函式名(gST->ConOut, 其他參數)。
// 第一個參數固定要再把 ConOut 傳進去，這是 C 版本的「物件導向」寫法：
// 等同其他語言的 obj.method()，而 C 沒有 this，所以手動把 obj 傳進去當第一個參數。
//
// 【顏色怎麼設定？】
// EFI_TEXT_ATTR(前景色, 背景色) 會把兩個顏色組合成一個 UINTN 數值，
// 然後交給 SetAttribute。之後 Print() 印出的文字都會用這組顏色，直到再次改變為止。
// ============================================================================
//

#include "TextUi.h"
#include "Input.h"

#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

// UnicodeVSPrint is provided by PrintLib.

//
// 以下四個是「檔案內全域變數」。
// STATIC 放在檔案最外層 = 只有這個 .c 檔看得到（等同 C 的 static 全域變數）。
// 用途：在程式開始時把螢幕原本的狀態存起來，程式結束時還原。
//   mSavedAttribute     : 原本的顏色組合（-1 代表還沒存過）
//   mSavedCursorVisible : 原本游標是否顯示
//   mSavedCursorCol/Row : 原本游標所在的欄與列
// 命名慣例：EDK2 習慣把模組內全域變數用 m 開頭（module），全域用 g 開頭（global）。
//
STATIC INT32    mSavedAttribute = -1;
STATIC BOOLEAN  mSavedCursorVisible = TRUE;
STATIC INT32    mSavedCursorCol = 0;
STATIC INT32    mSavedCursorRow = 0;

/**
  設定「一般文字」顏色：淺灰字、黑底。
  這是預設顏色，每次畫完特殊區塊（橫幅、高亮）後都會呼叫它換回來。
**/
VOID
EFIAPI
BdSetNormalAttr (
  VOID
  )
{
  // 先確認系統表與輸出裝置都存在，才能透過函式指標設定顏色。
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->SetAttribute (
                   gST->ConOut,                                  // 第一個參數固定是「物件自己」
                   EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK)      // 前景淺灰、背景黑
                   );
  }
}

/**
  設定「橫幅」顏色：白字、藍底（用來畫頁面上方的標題區）。
**/
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

/**
  設定「強調」顏色：亮青色字、黑底（用來畫邊框線與提示文字）。
**/
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

/**
  設定「反白（高亮）」顏色：黑字、亮青色底（用來標示選單目前選到的項目）。
**/
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

/**
  備份目前螢幕狀態（顏色、游標可見性、游標位置），並隱藏游標。

  為什麼要備份？這個程式會改顏色、清畫面。
  如果離開時不還原，UEFI Shell 就會留下我們設的顏色，看起來怪怪的。
  做法：從 gST->ConOut->Mode（一個記錄目前狀態的 struct）把值讀出來，
  存進上面的檔案內全域變數。
**/
VOID
EFIAPI
BdUiSaveConsoleState (
  VOID
  )
{
  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return;
  }

  // 讀取目前狀態並做型別轉換後存起來。
  mSavedAttribute     = (INT32)gST->ConOut->Mode->Attribute;
  mSavedCursorVisible = (BOOLEAN)gST->ConOut->Mode->CursorVisible;
  mSavedCursorCol     = gST->ConOut->Mode->CursorColumn;
  mSavedCursorRow     = gST->ConOut->Mode->CursorRow;
  // 隱藏游標（選單介面不需要閃爍的游標）。
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
}

/**
  還原先前用 BdUiSaveConsoleState 備份的螢幕狀態。
**/
VOID
EFIAPI
BdUiRestoreConsoleState (
  VOID
  )
{
  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return;
  }

  // mSavedAttribute >= 0 表示有成功備份過，就還原；否則退而求其次，設回一般顏色。
  if (mSavedAttribute >= 0) {
    gST->ConOut->SetAttribute (gST->ConOut, (UINTN)mSavedAttribute);
  } else {
    BdSetNormalAttr ();
  }

  // 還原游標的顯示狀態與位置。
  gST->ConOut->EnableCursor (gST->ConOut, mSavedCursorVisible);
  gST->ConOut->SetCursorPosition (
                 gST->ConOut,
                 (UINTN)mSavedCursorCol,
                 (UINTN)mSavedCursorRow
                 );
}

/**
  畫出頁面的標題橫幅：清畫面 -> 藍底白字的標題 -> 一條亮青色的分隔線。

  @param[in] Title  要顯示的標題；若為 NULL 就用預設的 "BootTimeDashboard"。
**/
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
  // 切換成藍底，印出三行：空白行、標題行、空白行，形成一個藍色橫幅。
  BdSetBannerAttr ();
  Print (L"                                                                                \n");
  // %-76s：靠左對齊並佔 76 格，讓整行底色一致。
  // 三元運算子：Title 為 NULL 時改用預設標題。
  Print (L"  %-76s\n", (Title != NULL) ? Title : L"BootTimeDashboard");
  Print (L"                                                                                \n");
  BdSetNormalAttr ();
  // 用強調色畫一條分隔線，然後回到一般顏色。
  BdSetAccentAttr ();
  Print (L"  +--------------------------------------------------------------------------+\n");
  BdSetNormalAttr ();
  Print (L"\n");
}

/**
  畫出主選單（只負責「畫」，不處理按鍵）。

  @param[in] Title      程式標題。
  @param[in] Items      選單項目陣列。
  @param[in] ItemCount  項目數量。
  @param[in] Cursor     目前被選中的項目索引（會用反白顯示）。
**/
STATIC
VOID
BdDrawMenu (
  IN CONST CHAR16       *Title,
  IN CONST BD_MENU_ITEM *Items,
  IN UINTN               ItemCount,
  IN UINTN               Cursor
  )
{
  UINTN  Index;   // for 迴圈索引

  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return;
  }

  // 每次重畫都先清畫面（簡單做法：整個畫面重畫，不做局部更新）。
  gST->ConOut->ClearScreen (gST->ConOut);

  // 上方橫幅（藍底白字）。
  BdSetBannerAttr ();
  Print (L"                                                                                \n");
  Print (L"  %-40s  text UI                                 \n",
         (Title != NULL) ? Title : L"BootTimeDashboard");
  Print (L"  Boot-time measurement viewer (not an optimizer)                               \n");
  Print (L"                                                                                \n");
  BdSetNormalAttr ();

  // 選單邊框與操作提示。
  BdSetAccentAttr ();
  Print (L"\n  +--------------------------- Main Menu ------------------------------------+\n");
  BdSetNormalAttr ();
  Print (L"  |  Up/Down move   Enter select   Esc quit                                  |\n");
  BdSetAccentAttr ();
  Print (L"  +--------------------------------------------------------------------------+\n\n");
  BdSetNormalAttr ();

  // 逐項印出選單。目前選到的那一項用反白顏色並加上 ">> <<" 標記，其他項目用一般顏色。
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

/**
  執行主選單：畫選單 -> 等按鍵 -> 更新游標 -> 重複，直到使用者按 Enter 或 Esc。

  這是典型的「事件迴圈 (event loop)」：
    for (;;) { 畫畫面; 讀按鍵; 依按鍵更新狀態; }

  @param[in]  Title      標題。
  @param[in]  Items      選單項目陣列。
  @param[in]  ItemCount  項目數量。
  @param[out] Selected   使用者按 Enter 時選到的索引。
  @param[out] Escaped    使用者按 Esc 時設成 TRUE。

  @retval EFI_SUCCESS            使用者按 Enter（Selected 有效）或 Esc（Escaped 為 TRUE）。
  @retval EFI_INVALID_PARAMETER  參數是 NULL 或項目數為 0。
  @retval Others                 讀鍵盤失敗時的錯誤碼。
**/
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
  EFI_INPUT_KEY  Key;      // 讀到的按鍵（struct：ScanCode 與 UnicodeChar 兩個欄位）
  UINTN          Cursor;   // 目前反白的項目索引

  // 參數檢查：輸出參數的指標不能是 NULL，而且至少要有一個項目。
  if ((Items == NULL) || (ItemCount == 0) || (Selected == NULL) || (Escaped == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Cursor   = 0;        // 一開始選第一項
  *Escaped = FALSE;    // 先假設使用者沒有按 Esc
  BdFlushKeys ();      // 清掉殘留按鍵，避免誤觸

  for ( ; ;) {
    BdDrawMenu (Title, Items, ItemCount, Cursor);

    // 等待並讀取一個按鍵（會卡住直到有人按）。
    Status = BdWaitForKey (&Key);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // 判斷按鍵種類。UEFI 把按鍵分成兩種：
    //   ScanCode    : 功能鍵（上下左右、Esc、PageUp/Down 等）
    //   UnicodeChar : 一般字元（字母、數字、Enter 等）
    //
    if (Key.ScanCode == SCAN_UP) {
      // 上鍵：游標往上。如果已經在第一項，就繞回最後一項（wrap-around）。
      if (Cursor == 0) {
        Cursor = ItemCount - 1;
      } else {
        Cursor--;
      }
    } else if (Key.ScanCode == SCAN_DOWN) {
      // 下鍵：游標往下。超過最後一項就繞回第一項。
      Cursor++;
      if (Cursor >= ItemCount) {
        Cursor = 0;
      }
    } else if (Key.ScanCode == SCAN_ESC) {
      // Esc：告訴呼叫者「使用者想離開」，並結束選單。
      *Escaped = TRUE;
      return EFI_SUCCESS;
    } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      // Enter（對應字元 '\r'）：把目前索引寫進輸出參數 *Selected 並結束選單。
      *Selected = Cursor;
      return EFI_SUCCESS;
    }
  }
}

/**
  清空頁面緩衝區：把行數歸零。
  不需要真的把字元陣列清掉，因為只要 Count 是 0，舊資料之後都會被覆寫。
  （就像把「陣列目前有效長度」設為 0。）

  @param[in,out] Buf  要重設的緩衝區。
**/
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

/**
  在頁面緩衝區尾端新增一行格式化文字（用法像 printf，但是是寫進緩衝區）。

  【可變參數 (variadic) 函式】
  參數列最後的「...」跟 C 的 printf(const char *fmt, ...) 一樣。
  取出額外參數的步驟（跟 C 的 <stdarg.h> 完全對應）：
    VA_LIST  Args;                  // 對應 va_list
    VA_START (Args, Format);        // 對應 va_start：從 Format 後面開始
    ... 使用 Args ...
    VA_END (Args);                  // 對應 va_end：結束
  UnicodeVSPrint 就是 vsprintf 的寬字串版本，會把格式化結果寫進指定的緩衝區。

  如果緩衝區已滿 (Count >= BD_MAX_PAGE_LINES)，這一行會被默默丟棄，避免陣列越界。

  @param[in,out] Buf     目標緩衝區。
  @param[in]     Format  格式字串（寬字串）。
  @param[in]     ...     對應格式字串的參數。
**/
VOID
EFIAPI
BdPageBufferAdd (
  IN OUT BD_PAGE_BUFFER  *Buf,
  IN     CONST CHAR16    *Format,
  ...
  )
{
  VA_LIST  Args;

  // 防呆：任何參數是 NULL，或行數已達上限，就不再新增。
  if ((Buf == NULL) || (Format == NULL) || (Buf->Count >= BD_MAX_PAGE_LINES)) {
    return;
  }

  VA_START (Args, Format);
  // 寫進第 Count 行。第二個參數是這一行的「總 byte 數」：
  // sizeof (Buf->Lines[0]) = 一行的大小 = BD_LINE_CHARS * sizeof(CHAR16)，超過會自動截斷。
  UnicodeVSPrint (Buf->Lines[Buf->Count], sizeof (Buf->Lines[0]), Format, Args);
  VA_END (Args);
  Buf->Count++;   // 行數加一，下次就寫下一行
}

/**
  計算「螢幕上可以顯示幾行頁面內容」。

  做法：問螢幕目前模式有幾列 (Rows)，扣掉上方橫幅與下方提示佔用的行數。
  若查詢失敗就用保守的預設值，確保程式還能運作。

  @return 可用於顯示內容的行數。
**/
STATIC
UINTN
BdVisibleRows (
  VOID
  )
{
  UINTN  Cols;   // 螢幕欄數（這裡用不到，但 QueryMode 一定要給位址）
  UINTN  Rows;   // 螢幕列數

  // 沒有螢幕可查，回傳預設 20 行。
  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return 20;
  }

  // QueryMode：詢問目前文字模式的欄數與列數，結果透過指標輸出到 Cols、Rows。
  // 失敗時一樣回傳預設值 20。
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
  // 橫幅約佔 5 行、底部提示約 3 行，合計扣 8 行，剩下的才是內容區。
  if (Rows > 10) {
    return Rows - 8;
  }

  // 螢幕太小就保底顯示 12 行。
  return 12;
}

/**
  顯示緩衝區中的內容，並讓使用者上下捲動；按 Enter 或 Esc 離開。

  【捲動的原理】
  把緩衝區想成一個很長的字串陣列，螢幕只是一個「視窗」，一次只能看 Visible 行。
  變數 Scroll 代表「視窗最上面那行是陣列的第幾個元素」：
    - 按下：Scroll + 1（但不能超過 MaxScroll）
    - 按上：Scroll - 1（但不能小於 0）
    - PageDown/PageUp：一次移動 Visible 行
  MaxScroll = 總行數 - 可見行數，是 Scroll 的最大值（再往下就會顯示空白）。
  畫面顯示的就是 Lines[Scroll] ~ Lines[Scroll + Visible - 1]。

  @param[in] Title  頁面標題。
  @param[in] Buf    要顯示的內容緩衝區（唯讀）。

  @retval EFI_SUCCESS            使用者按 Enter / Esc 離開。
  @retval EFI_INVALID_PARAMETER  Buf 是 NULL。
  @retval Others                 讀鍵盤失敗時的錯誤碼。
**/
EFI_STATUS
EFIAPI
BdShowScrollablePage (
  IN CONST CHAR16          *Title,
  IN CONST BD_PAGE_BUFFER  *Buf
  )
{
  EFI_STATUS     Status;
  EFI_INPUT_KEY  Key;
  UINTN          Scroll;      // 視窗頂端對應的行索引（捲動位置）
  UINTN          Visible;     // 一個畫面能顯示幾行
  UINTN          Index;       // 繪製時的迴圈索引
  UINTN          End;         // 本次繪製的結束索引（不含）
  UINTN          MaxScroll;   // Scroll 的最大允許值

  if (Buf == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Scroll  = 0;                  // 一開始從第 0 行開始看
  Visible = BdVisibleRows ();   // 計算一個畫面能放幾行
  BdFlushKeys ();               // 清掉殘留按鍵（例如上一頁按的 Enter）

  for ( ; ;) {
    // 每圈：先畫標題橫幅，再畫內容，再畫底部提示，最後等按鍵。
    BdDrawPageFrame (Title);
    if (Buf->Count == 0) {
      Print (L"  (empty)\n");
    } else {
      // 計算本頁要顯示到哪一行（不能超過總行數，避免讀到沒資料的行）。
      End = Scroll + Visible;
      if (End > Buf->Count) {
        End = Buf->Count;
      }

      // 印出 Lines[Scroll] 到 Lines[End - 1]。
      for (Index = Scroll; Index < End; Index++) {
        Print (L"%s\n", Buf->Lines[Index]);
      }
    }

    // 計算最大捲動值。如果全部內容一個畫面就放得下，就不能捲動（MaxScroll 維持 0）。
    // 這裡先判斷 Count > Visible 才相減，避免 unsigned 減法下溢成超大數字。
    MaxScroll = 0;
    if (Buf->Count > Visible) {
      MaxScroll = Buf->Count - Visible;
    }

    // 底部提示列：顯示目前看到第幾行到第幾行、共幾行。
    // 三元運算子用來處理「沒有內容」與「最後一頁不足一整頁」的邊界情況。
    BdSetAccentAttr ();
    Print (
      L"\n  Lines %u-%u / %u   Up/Down/PgUp/PgDn scroll   Enter/Esc return\n",
      (Buf->Count == 0) ? 0 : (UINT32)(Scroll + 1),
      (UINT32)((Scroll + Visible > Buf->Count) ? Buf->Count : (Scroll + Visible)),
      (UINT32)Buf->Count
      );
    BdSetNormalAttr ();

    // 等使用者按鍵。
    Status = BdWaitForKey (&Key);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    // Enter 或 Esc：離開此頁面，回到主選單。
    if ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == CHAR_CARRIAGE_RETURN)) {
      return EFI_SUCCESS;
    }

    // 依按鍵調整 Scroll。每次都有邊界判斷，確保 Scroll 永遠落在 0 ~ MaxScroll 之間。
    if (Key.ScanCode == SCAN_UP) {
      if (Scroll > 0) {
        Scroll--;
      }
    } else if (Key.ScanCode == SCAN_DOWN) {
      if (Scroll < MaxScroll) {
        Scroll++;
      }
    } else if (Key.ScanCode == SCAN_PAGE_UP) {
      // 往上翻一頁；若剩餘不足一頁就直接回到最上面（避免 unsigned 減成負數）。
      if (Scroll > Visible) {
        Scroll -= Visible;
      } else {
        Scroll = 0;
      }
    } else if (Key.ScanCode == SCAN_PAGE_DOWN) {
      // 往下翻一頁；超過 MaxScroll 就停在最後一頁。
      Scroll += Visible;
      if (Scroll > MaxScroll) {
        Scroll = MaxScroll;
      }
    }
  }
}

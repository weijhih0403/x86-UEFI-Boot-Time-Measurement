/** @file
  Shared types for BootTimeDashboard UI.

  This header only defines the menu contract used by BootTimeDashboard.c
  and Ui/TextUi.c. Page content buffers (BD_PAGE_BUFFER) live in TextUi.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// ============================================================================
// 【這個檔案是什麼？】
// 這是一個「標頭檔 (.h)」，只放型別定義，不放程式邏輯。
// 它定義了「選單項目長什麼樣子」，讓主程式 (BootTimeDashboard.c)
// 與畫選單的程式 (Ui/TextUi.c) 都能共用同一種資料格式。
//
// 【include guard（防止重複引入）】
// 底下的 #ifndef / #define / #endif 就是 C 標頭檔常見的寫法：
// 第一次被 include 時定義 BD_TYPES_H_ 並展開內容；
// 之後再被 include 時，因為已經定義過了，就整段跳過，避免「重複定義」錯誤。
// ============================================================================
//

#ifndef BD_TYPES_H_
#define BD_TYPES_H_

#include <Uefi.h>   // UEFI 基本型別 (EFI_STATUS / CHAR16 / BOOLEAN ...)

/**
  Callback for one menu entry.

  Each page (Boot-Time / System Info / Memory Map / Exit) implements this
  signature. The menu loop calls Handler after the user selects the item.

  @retval EFI_SUCCESS  Page finished normally (or Exit stub returned).
  @retval Others       Propagated if a page wants to abort the app.
**/
//
// 【函式指標型別 (function pointer typedef)】
// 這一段在 C 裡等同於：
//     typedef EFI_STATUS (*BD_PAGE_HANDLER)(void);
// 意思是：「BD_PAGE_HANDLER」是一種型別，代表
// 「一個沒有參數、回傳 EFI_STATUS 的函式的指標」。
// 凡是符合這種「長相」的函式（例如 BdBootTimePage），
// 都可以放進這種型別的變數或 struct 欄位裡，之後透過它呼叫。
// EFIAPI 只是呼叫慣例標註；VOID 就是 void（沒有參數）。
//
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
//
// 【struct：選單的一列】
// 一個 BD_MENU_ITEM 變數就代表選單裡的一行。三個欄位：
//   Title     ：選單上顯示的文字。CONST CHAR16 * 是「指向唯讀寬字元字串的指標」，
//               就像 C 的 const char *，只是字元是 16 位元。
//   Handler   ：被選到時要呼叫的函式（上面定義的函式指標）。
//   ExitAfter ：執行完 Handler 後，要不要結束整個程式。
//
// 這就像在 C 裡寫：
//     struct MenuItem { const char *title; void (*handler)(void); int exit_after; };
// 然後做一個 MenuItem 陣列來當選單。
//
typedef struct {
  CONST CHAR16       *Title;
  BD_PAGE_HANDLER     Handler;
  BOOLEAN             ExitAfter;
} BD_MENU_ITEM;

#endif

/** @file
  Text-mode menu and page helpers.

  Adapted from FirmwareDashboardPkg.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// ============================================================================
// 【這個檔案是什麼？】
// 文字介面 (Text UI) 的標頭檔，宣告「畫選單」「畫頁面」「可捲動頁面」等函式，
// 以及一個叫 BD_PAGE_BUFFER 的資料結構（頁面內容的暫存區）。
// 實作在 TextUi.c。
//
// 【設計想法：先把內容存起來，再一次顯示】
// 各個頁面 (BootTimePage / SystemInfoPage / MemoryMapPage) 不直接 printf，
// 而是先用 BdPageBufferAdd() 把每一行文字存進 BD_PAGE_BUFFER（二維字元陣列），
// 最後呼叫 BdShowScrollablePage() 顯示。
// 好處：內容超過一個螢幕時，可以讓使用者上下捲動。
// ============================================================================
//

#ifndef BD_TEXT_UI_H_
#define BD_TEXT_UI_H_

#include "Common/BdTypes.h"   // 取得 BD_MENU_ITEM 型別

// 一個頁面最多可存幾行文字。
#define BD_MAX_PAGE_LINES  96
// 每一行最多幾個 CHAR16 字元（含結尾的 L'\0'）。
#define BD_LINE_CHARS      120

//
// 頁面緩衝區 (page buffer)。
// 可以想成 C 裡的「字串陣列」：
//     CHAR16 Lines[96][120];   // 96 行，每行最多 120 個寬字元
//     UINTN  Count;            // 目前實際用了幾行
// 就像 char lines[96][120]; int count; 包在同一個 struct 裡。
// 注意：這個 struct 很大（96*120*2 約 23 KB），所以通常宣告成區域變數就要小心堆疊大小。
//
typedef struct {
  CHAR16  Lines[BD_MAX_PAGE_LINES][BD_LINE_CHARS];
  UINTN   Count;
} BD_PAGE_BUFFER;

//
// 備份目前螢幕的顏色、游標位置與可見狀態，並把游標隱藏。
// 程式開始時呼叫一次。
//
VOID
EFIAPI
BdUiSaveConsoleState (
  VOID
  );

//
// 還原先前備份的螢幕狀態。程式結束前呼叫，讓 UEFI Shell 恢復原本樣子。
//
VOID
EFIAPI
BdUiRestoreConsoleState (
  VOID
  );

//
// 把文字顏色設回「一般」（淺灰字、黑底）。
//
VOID
EFIAPI
BdSetNormalAttr (
  VOID
  );

//
// 清畫面並畫出頁面最上方的藍色標題橫幅。Title 是要顯示的標題字串。
//
VOID
EFIAPI
BdDrawPageFrame (
  IN CONST CHAR16  *Title
  );

//
// 顯示主選單並等待使用者操作。
//   Title     (IN)  ：選單標題
//   Items     (IN)  ：選單項目陣列（BD_MENU_ITEM 陣列）
//   ItemCount (IN)  ：陣列長度
//   Selected  (OUT) ：使用者按 Enter 時，被選中的索引
//   Escaped   (OUT) ：使用者是否按 Esc 離開
// 這是「傳指標當輸出參數」的標準寫法（像 scanf 的 &x）。
//
EFI_STATUS
EFIAPI
BdRunTextMenu (
  IN  CONST CHAR16        *Title,
  IN  CONST BD_MENU_ITEM  *Items,
  IN  UINTN                ItemCount,
  OUT UINTN               *Selected,
  OUT BOOLEAN             *Escaped
  );

//
// 清空頁面緩衝區（把 Count 歸零，相當於「從頭開始存」）。
//
VOID
EFIAPI
BdPageBufferReset (
  IN OUT BD_PAGE_BUFFER  *Buf
  );

//
// 在緩衝區尾端新增一行文字，用法跟 printf 一樣（格式字串 + 可變參數）。
// 「...」代表可變參數 (variadic)，與 C 的 printf(const char *fmt, ...) 同樣概念。
//
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
//
// 把緩衝區內容顯示出來，並讓使用者用上下鍵 / PageUp / PageDown 捲動，
// 按 Enter 或 Esc 離開此頁面。
//
EFI_STATUS
EFIAPI
BdShowScrollablePage (
  IN CONST CHAR16        *Title,
  IN CONST BD_PAGE_BUFFER  *Buf
  );

#endif

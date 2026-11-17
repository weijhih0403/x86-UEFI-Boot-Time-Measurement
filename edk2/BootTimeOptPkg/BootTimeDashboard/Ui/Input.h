/** @file
  Keyboard input helpers (WaitForEvent, no busy polling).

  Adapted from FirmwareDashboardPkg (https://github.com/weijhih0403/FirmwareDashboardPkg).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// 【這個檔案是什麼？】
// 鍵盤輸入的標頭檔，宣告兩個函式：
//   BdWaitForKey : 等待使用者按一個鍵，並把按鍵資訊讀出來（類似 getchar()）
//   BdFlushKeys  : 把鍵盤緩衝區裡還沒讀的按鍵全部丟掉（清空 stdin 的概念）
// 實作在 Input.c。
// （#ifndef ... #endif 是 include guard，避免重複引入。）
//

#ifndef BD_INPUT_H_
#define BD_INPUT_H_

#include <Uefi.h>

//
// 等待並讀取一個按鍵。
//   Key (OUT)：呼叫者傳入 EFI_INPUT_KEY 變數的位址，函式把按鍵資料填進去。
//              EFI_INPUT_KEY 是個 struct，有兩個欄位：
//                ScanCode    : 特殊鍵代碼（上下左右、Esc、PageUp 等）
//                UnicodeChar : 一般字元（例如 'a'、Enter 的 '\r'）
// 回傳 EFI_SUCCESS 代表成功讀到按鍵。
//
EFI_STATUS
EFIAPI
BdWaitForKey (
  OUT EFI_INPUT_KEY  *Key
  );

//
// 清空鍵盤緩衝區，避免「上一頁殘留的按鍵」被下一頁誤當成新輸入。
//
VOID
EFIAPI
BdFlushKeys (
  VOID
  );

#endif

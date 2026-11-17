/** @file
  UEFI Memory Map summary page.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// 【這個檔案是什麼？】
// 「記憶體地圖頁面」的標頭檔，只宣告 BdMemoryMapPage 這一個函式。
// 實作在 MemoryMapPage.c。
// （#ifndef ... #endif 是 include guard，避免重複引入。）
//

#ifndef BD_MEMORY_MAP_PAGE_H_
#define BD_MEMORY_MAP_PAGE_H_

#include <Uefi.h>

//
// 顯示「UEFI 記憶體地圖摘要」頁面。
// 形狀符合 BD_PAGE_HANDLER 函式指標型別（無參數、回傳 EFI_STATUS）。
//
EFI_STATUS
EFIAPI
BdMemoryMapPage (
  VOID
  );

#endif

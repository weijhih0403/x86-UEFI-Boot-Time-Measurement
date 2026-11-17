/** @file
  Boot-Time Measurements page.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// 【這個檔案是什麼？】
// 「開機時間頁面」的標頭檔，只宣告一個函式 BdBootTimePage，
// 讓主程式 (BootTimeDashboard.c) 可以把它放進選單。
// 這就是 C 語言裡「函式原型 (prototype) 放在 .h，實作放在 .c」的標準分工。
//
// #ifndef / #define / #endif 是 include guard，防止同一個標頭檔被重複引入。
//

#ifndef BD_BOOT_TIME_PAGE_H_
#define BD_BOOT_TIME_PAGE_H_

#include <Uefi.h>

//
// 顯示「開機時間量測」頁面。
// 回傳 EFI_SUCCESS 代表頁面正常結束（像 return 0）。
// 這個函式的形狀（無參數、回傳 EFI_STATUS）符合 BdTypes.h 裡的
// BD_PAGE_HANDLER 函式指標型別，所以能直接放進選單。
//
EFI_STATUS
EFIAPI
BdBootTimePage (
  VOID
  );

#endif

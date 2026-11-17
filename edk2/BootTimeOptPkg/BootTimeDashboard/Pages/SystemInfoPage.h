/** @file
  System Information page.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// 【這個檔案是什麼？】
// 「系統資訊頁面」的標頭檔，只宣告 BdSystemInfoPage 這一個函式。
// 實作在 SystemInfoPage.c。主程式把它放進選單，選到時被呼叫。
// （#ifndef ... #endif 是 include guard，避免重複引入。）
//

#ifndef BD_SYSTEM_INFO_PAGE_H_
#define BD_SYSTEM_INFO_PAGE_H_

#include <Uefi.h>

//
// 顯示「系統資訊」頁面（韌體資訊、即時時鐘、SMBIOS 硬體清單）。
// 形狀符合 BD_PAGE_HANDLER 函式指標型別（無參數、回傳 EFI_STATUS）。
//
EFI_STATUS
EFIAPI
BdSystemInfoPage (
  VOID
  );

#endif

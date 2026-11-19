/** @file
  BootTimeDashboard — interactive viewer for boot-time measurements and
  system/memory diagnostics. Does NOT call ExitBootServices.

  UI / SMBIOS / memory-map logic adapted from:
  https://github.com/weijhih0403/FirmwareDashboardPkg

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// ============================================================================
// 【給只學過 C 的新手：這個檔案是什麼？】
//
// 這是整個「BootTimeDashboard」程式的「主程式檔」。
// 在一般 C 程式裡，程式從 int main(void) 開始執行；
// 在 UEFI 應用程式裡，程式是從 UefiMain() 開始執行，角色等同於 main()。
//
// 這個程式做的事情很單純：
//   1. 在螢幕上顯示一個文字選單（類似小時候寫的「1. 新增 2. 刪除 3. 離開」選單）。
//   2. 使用者用上下鍵選項目、按 Enter 進入。
//   3. 每個選項對應一個「頁面函式」，負責把資訊印出來。
//   4. 選到 Exit 或按 Esc 就結束程式。
//
// 【UEFI 是什麼？用 C 的角度理解】
//   UEFI 是電腦開機時（作業系統還沒啟動前）執行的韌體環境。
//   它沒有 printf / scanf / malloc 這些 C 標準函式庫，
//   而是提供自己的一組函式，放在「表格」(struct) 裡，
//   表格裡的每個欄位其實就是「函式指標」。
//   例如：gST->ConOut->ClearScreen(...) 就是
//   「透過 struct 裡的函式指標，呼叫清除螢幕的函式」。
//   你可以把 gST 想成一個很大的全域 struct 指標，裡面裝滿工具箱。
//
// 【常見 UEFI 型別對照 C 型別】
//   UINTN    ≈ 和 CPU 位元數一樣寬的 unsigned 整數（64 位元機器就是 64 bit）
//   UINT8/16/32/64 ≈ uint8_t / uint16_t / uint32_t / uint64_t
//   CHAR16   ≈ 16 位元寬字元（UEFI 字串用 UTF-16，字串前要加 L，例如 L"abc"）
//   BOOLEAN  ≈ 只用 TRUE / FALSE 的布林值
//   VOID     ≈ void
//   EFI_STATUS ≈ 函式回傳的「錯誤碼」，EFI_SUCCESS 代表成功（類似 return 0）
//   IN / OUT / OPTIONAL ≈ 純粹的「標註」，編譯時會被展開成空白，
//                         只是告訴讀者「這個參數是輸入 / 輸出」。
//   EFIAPI   ≈ 呼叫慣例標註（跨編譯器統一用法），初學先當作裝飾字就好。
//   STATIC   ≈ static，代表此函式只在本檔案可見（不會被別的 .c 檔呼叫）。
// ============================================================================
//

//
// 下面這些 #include 就是 C 的 #include <stdio.h> 的 UEFI 版本。
// 不同的是它們是 EDK2 (UEFI 開發套件) 提供的標頭檔。
//
#include <Uefi.h>                                   // UEFI 基本型別與常數（等同 C 的基礎型別定義）

#include <Library/UefiApplicationEntryPoint.h>      // 讓 UefiMain 成為程式進入點
#include <Library/UefiBootServicesTableLib.h>       // 提供全域變數 gST / gBS / gImageHandle
#include <Library/UefiLib.h>                        // 提供 Print()，類似 printf()

#include <Library/BootTimeBdsProbeLib.h>            // 進入時標記 LoadPath

#include "Common/BdTypes.h"                         // 本專案自訂：選單項目型別 BD_MENU_ITEM
#include "Pages/BootTimePage.h"                     // 「開機時間」頁面函式宣告
#include "Pages/MemoryMapPage.h"                    // 「記憶體地圖」頁面函式宣告
#include "Pages/SystemInfoPage.h"                   // 「系統資訊」頁面函式宣告
#include "Ui/TextUi.h"                              // 文字介面（畫選單、畫頁面）

// 程式名稱字串。#define 就是 C 的巨集；L"..." 表示 UTF-16 寬字串。
#define BOOT_TIME_DASHBOARD_NAME  L"BootTimeDashboard"

/**
  「Exit」選單項目對應的頁面函式。

  白話：選單每一項都需要一個「函式」，Exit 其實不需要做事，
  所以放一個什麼都不做、直接回傳成功的空函式。
  真正「離開程式」的動作由選單項目的 ExitAfter = TRUE 欄位控制（見 UefiMain）。

  @retval EFI_SUCCESS  永遠成功（類似 return 0;）。
**/
STATIC
EFI_STATUS
EFIAPI
BdExitPage (
  VOID
  )
{
  return EFI_SUCCESS;
}

/**
  程式進入點（等同 C 的 main）。

  整體流程（把它想成一個 do-while 選單迴圈）：
    1. 檢查螢幕/鍵盤是否可用。
    2. 備份目前終端機狀態（顏色、游標），結束時還原。
    3. 反覆：顯示選單 -> 使用者選擇 -> 呼叫對應頁面函式。
    4. 離開時還原終端機並印出 bye。

  @param[in] ImageHandle  這個應用程式自己的「身分證」（本程式沒用到）。
  @param[in] SystemTable  UEFI 系統表，裡面有螢幕/鍵盤/各種服務（本檔改用全域 gST）。

  @retval EFI_SUCCESS      正常結束。
  @retval EFI_UNSUPPORTED  找不到螢幕或鍵盤。
  @retval Others           選單執行出錯時，傳回該錯誤碼。
**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS          Status;      // 儲存各函式回傳的狀態碼（像 int ret;）
  UINTN               Selected;    // 使用者選到的是第幾個選單項目（陣列索引，從 0 開始）
  BOOLEAN             Escaped;     // 使用者是不是按了 Esc 想離開
  BOOLEAN             Running;     // 主迴圈是否繼續跑（像 while(running) 的旗標）

  //
  // 選單陣列：這是「struct 陣列 + 函式指標」的經典用法。
  // 每一列的格式是 { 顯示文字, 要呼叫的函式, 執行完是否離開 }，
  // 對應 BdTypes.h 裡的 BD_MENU_ITEM 結構。
  // 這個技巧叫做「查表法 (table-driven)」：
  // 不用寫一長串 if/else 或 switch 判斷選了誰，
  // 直接用索引 Menu[Selected].Handler() 就能呼叫對應函式。
  // CONST 就是 const（內容唯讀）。
  //
  CONST BD_MENU_ITEM  Menu[] = {
    { L"Boot-Time Measurements", BdBootTimePage,   FALSE },  // 看開機各階段耗時
    { L"System Information",     BdSystemInfoPage, FALSE },  // 看韌體/時鐘/SMBIOS 資訊
    { L"Memory Map",             BdMemoryMapPage,  FALSE },  // 看 UEFI 記憶體分布
    { L"Exit",                   BdExitPage,       TRUE  },  // 離開（ExitAfter 為 TRUE）
  };

  // 這兩個參數沒用到，用 (VOID) 強制轉型可以避免編譯器警告「未使用的參數」。
  (VOID)ImageHandle;
  (VOID)SystemTable;

  //
  // 防呆檢查：就像 C 裡使用指標前先檢查 NULL。
  // gST       = 系統表（全域指標）
  // gST->ConOut = 輸出（螢幕）裝置
  // gST->ConIn  = 輸入（鍵盤）裝置
  // 任何一個是 NULL，這個程式就沒辦法互動，直接回傳「不支援」。
  //
  if ((gST == NULL) || (gST->ConOut == NULL) || (gST->ConIn == NULL)) {
    return EFI_UNSUPPORTED;
  }

  // 與 Dump 相同：一進應用就記 LoadPath（ReadyToBoot → 本程式進入）
  BootTimeBdsMarkLoadPathAtAppEntry ();

  // 先記住目前螢幕的顏色/游標狀態，離開時要還原，避免影響 UEFI Shell。
  BdUiSaveConsoleState ();

  // 清除螢幕（透過 ConOut 裡的函式指標 ClearScreen 呼叫）。
  gST->ConOut->ClearScreen (gST->ConOut);
  // Print() 就是 UEFI 版 printf。注意：%s 對應 CHAR16 寬字串，%r 可以直接印 EFI_STATUS 的文字。
  Print (L"%s\n", BOOT_TIME_DASHBOARD_NAME);
  Print (L"Interactive viewer. Does not accelerate boot. Does not call ExitBootServices.\n");

  //
  // 主迴圈：不斷顯示選單，直到使用者離開。
  //
  Running = TRUE;
  while (Running) {
    // 顯示選單並等待使用者按鍵。
    // &Selected / &Escaped 是「輸出參數」：傳位址進去，函式幫我們把結果填進來
    // （等同 scanf("%d", &x) 的概念）。
    // ARRAY_SIZE(Menu) 會算出陣列有幾個元素（等同 sizeof(Menu)/sizeof(Menu[0])）。
    Status = BdRunTextMenu (
               BOOT_TIME_DASHBOARD_NAME,
               Menu,
               ARRAY_SIZE (Menu),
               &Selected,
               &Escaped
               );
    // EFI_ERROR(Status) 判斷狀態碼是不是「失敗」（UEFI 的錯誤碼最高位元為 1）。
    if (EFI_ERROR (Status)) {
      Print (L"Menu error: %r\n", Status);
      BdUiRestoreConsoleState ();    // 出錯也要把螢幕設定還原再離開
      return Status;
    }

    // 使用者按了 Esc：跳出 while 迴圈，結束程式。
    if (Escaped) {
      break;
    }

    // 保險檢查：索引不可超出陣列範圍（避免越界存取），超出就重新顯示選單。
    if (Selected >= ARRAY_SIZE (Menu)) {
      continue;
    }

    // 呼叫被選中項目的函式指標。
    // 這行就是「函式指標呼叫」：Menu[Selected].Handler 是個指向函式的指標，
    // 後面加 () 就等於呼叫它，例如選第 0 項就等於呼叫 BdBootTimePage()。
    Status = Menu[Selected].Handler ();
    if (EFI_ERROR (Status)) {
      Print (L"Page error: %r\n", Status);
    }

    // 如果這個項目標記 ExitAfter = TRUE（也就是 Exit 項目），就結束主迴圈。
    if (Menu[Selected].ExitAfter) {
      Running = FALSE;
    }
  }

  // 正常離開：還原螢幕設定，清畫面，印出再見訊息。
  BdUiRestoreConsoleState ();
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->ClearScreen (gST->ConOut);
    Print (L"%s: bye.\n", BOOT_TIME_DASHBOARD_NAME);
  }

  return EFI_SUCCESS;
}

/** @file
  Event-driven keyboard input.

  Adapted from FirmwareDashboardPkg.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// ============================================================================
// 【這個檔案是什麼？】
// 負責「讀鍵盤」。在一般 C 裡你會用 getchar() 或 scanf()；
// UEFI 沒有這些，要改用「系統表 (gST) -> 輸入裝置 (ConIn)」提供的函式。
//
// 【為什麼不用迴圈一直問 (busy polling)？】
// 笨方法：while (沒有按鍵) { 一直檢查 }  -> 白白吃 CPU。
// 這裡的方法：呼叫 WaitForEvent，讓韌體「睡到有按鍵才叫醒我」，比較省電。
// ============================================================================
//

#include "Input.h"

#include <Library/UefiBootServicesTableLib.h>   // 提供 gST、gBS 這兩個全域指標

/**
  等待使用者按下一個鍵，並回傳按鍵內容。

  @param[out] Key  呼叫者提供的 EFI_INPUT_KEY 變數位址，函式把結果寫進去。

  @retval EFI_SUCCESS            成功讀到按鍵。
  @retval EFI_INVALID_PARAMETER  Key 是 NULL。
  @retval EFI_UNSUPPORTED        系統沒有鍵盤或 Boot Services。
  @retval Others                 等待事件或讀取按鍵時發生的錯誤。
**/
EFI_STATUS
EFIAPI
BdWaitForKey (
  OUT EFI_INPUT_KEY  *Key
  )
{
  EFI_STATUS  Status;   // 函式回傳的狀態碼
  UINTN       Index;    // WaitForEvent 會告訴我們「是第幾個事件被觸發」，這裡只等 1 個所以用不到

  // 參數檢查：傳進來的指標不能是 NULL，否則寫入會當機（跟 C 一樣）。
  if (Key == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // gST = System Table（系統表，大 struct 的指標）。
  // gST->ConIn = 鍵盤輸入裝置；gBS = Boot Services（開機期服務，像是 OS 提供的 API 集合）。
  // 任何一個是 NULL 就沒辦法讀鍵盤。
  if ((gST == NULL) || (gST->ConIn == NULL) || (gBS == NULL)) {
    return EFI_UNSUPPORTED;
  }

  //
  // 等待「有按鍵可讀」這個事件發生。
  // 參數：1 = 只等一個事件；&gST->ConIn->WaitForKey = 事件陣列（單一事件，所以傳它的位址）；
  //       &Index = 輸出：哪個事件被觸發。
  // 這行會「卡住」直到使用者按鍵，類似 getchar() 的阻塞行為。
  // 注意：gBS->WaitForEvent 是 struct 裡的「函式指標」，這裡就是用函式指標呼叫。
  //
  Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // 事件發生了，表示緩衝區有按鍵，現在把它真正讀出來，存進 *Key。
  return gST->ConIn->ReadKeyStroke (gST->ConIn, Key);
}

/**
  清掉鍵盤緩衝區中所有還沒被讀取的按鍵。

  用途：進入新頁面前先清空，避免先前殘留的按鍵（例如使用者連按 Enter）
  被當成新頁面的輸入。這等同於 C 裡「把 stdin 清空」的動作。
**/
VOID
EFIAPI
BdFlushKeys (
  VOID
  )
{
  EFI_INPUT_KEY  Key;      // 暫存讀出來的按鍵（讀完就丟掉）
  EFI_STATUS     Status;

  // 沒有鍵盤就什麼都不用做，直接結束。
  if ((gST == NULL) || (gST->ConIn == NULL)) {
    return;
  }

  //
  // do-while 迴圈：一直讀，讀到「失敗」為止。
  // ReadKeyStroke 在緩衝區有鍵時回傳成功；沒有鍵時回傳錯誤 (EFI_NOT_READY)。
  // 所以「讀到失敗」就代表緩衝區已經被我們清空了。
  //
  do {
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  } while (!EFI_ERROR (Status));
}

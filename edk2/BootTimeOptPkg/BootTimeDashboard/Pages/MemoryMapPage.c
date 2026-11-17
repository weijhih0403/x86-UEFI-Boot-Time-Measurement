/** @file
  GetMemoryMap snapshot while this application is running.

  Adapted from FirmwareDashboardPkg SystemInfo memory-map block.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// ============================================================================
// 【這個檔案是什麼？】
// 「Memory Map（記憶體地圖）」頁面。它向 UEFI 要一份「目前記憶體怎麼被使用」的清單，
// 把各種用途的記憶體加總後，用 MiB 顯示出來。
//
// 【什麼是 Memory Map？用 C 的角度理解】
// 在一般 C 程式，作業系統幫你管理 malloc 的記憶體。
// UEFI 韌體自己也要管理整台電腦的實體記憶體，它用一張「描述表」記錄：
//   「從位址 A 開始、共 N 頁，這一段是 XXX 用途」。
// 這張表就是 EFI_MEMORY_DESCRIPTOR 的陣列（每個元素是一個 struct，描述一段記憶體）。
// 一頁 (page) = 4 KiB (EFI_PAGE_SIZE = 4096 bytes)。
//
// 【取得這張表的麻煩之處】
// 呼叫 gBS->GetMemoryMap() 時，你必須提供一塊夠大的緩衝區。但是你不知道要多大，
// 所以標準做法是「先呼叫一次問大小 -> 配置記憶體 -> 再呼叫一次真正取資料」。
// 而且「配置記憶體」這個動作本身又會改變 memory map（多一筆描述），
// 所以才要多留一點空間，並且在失敗時重試幾次。
// ============================================================================
//

#include "MemoryMapPage.h"

#include <Library/MemoryAllocationLib.h>        // AllocatePool / FreePool（等同 malloc / free）
#include <Library/UefiBootServicesTableLib.h>   // gBS：Boot Services 指標
#include <Library/UefiLib.h>

#include "Ui/TextUi.h"                          // 頁面緩衝區與顯示函式

/**
  顯示 UEFI 記憶體地圖摘要頁面。

  流程：
    1. 問 UEFI「記憶體地圖需要多大」。
    2. malloc 一塊緩衝區，取得整張地圖（最多重試 6 次）。
    3. 用迴圈走訪每個描述 (descriptor)，依類型把頁數累加。
    4. 頁數換算成 MiB，寫進頁面緩衝區並顯示。
    5. free 掉緩衝區。

  @retval EFI_SUCCESS  頁面正常顯示結束。
  @retval Others       顯示頁面時的錯誤（取地圖失敗只會印在頁面上，不會讓程式崩潰）。
**/
EFI_STATUS
EFIAPI
BdMemoryMapPage (
  VOID
  )
{
  BD_PAGE_BUFFER         Buf;            // 頁面文字緩衝區（先寫進這裡，最後一起顯示）
  EFI_STATUS             Status;         // 函式回傳狀態碼
  EFI_MEMORY_DESCRIPTOR  *Map;           // 指向「描述陣列」的起點（像 malloc 回來的指標）
  EFI_MEMORY_DESCRIPTOR  *Desc;          // 迴圈中指向「目前這一筆描述」
  UINTN                  MapSize;        // 地圖總共多少 bytes
  UINTN                  MapKey;         // 地圖版本鑰匙（呼叫 ExitBootServices 才會用到，本程式只是接收）
  UINTN                  DescSize;       // 每一筆描述的大小（bytes），由韌體決定，不一定等於 sizeof(struct)
  UINT32                 DescVer;        // 描述格式的版本號
  UINTN                  Index;          // for 迴圈索引
  UINTN                  Count;          // 地圖裡共有幾筆描述
  UINTN                  Retry;          // 重試次數計數
  // 以下是各類記憶體的「頁數」累計值（還不是 MiB）
  UINT64                 Conventional;   // 一般可用記憶體（OS 之後可以拿去用）
  UINT64                 BootServices;   // 開機服務用的記憶體（進入 OS 後會被回收）
  UINT64                 Runtime;        // 執行期服務用的記憶體（OS 啟動後仍需保留）
  UINT64                 Reserved;       // 保留記憶體
  UINT64                 Acpi;           // ACPI 表使用的記憶體
  UINT64                 Other;          // 其他類型
  UINT64                 Pages;          // 目前這筆描述的頁數

  // 初始化頁面緩衝區，然後先寫幾行說明文字。
  BdPageBufferReset (&Buf);
  BdPageBufferAdd (&Buf, L"[ UEFI Memory Map  GetMemoryMap() ]");
  BdPageBufferAdd (&Buf, L"  Snapshot while BootTimeDashboard is running.");
  BdPageBufferAdd (&Buf, L"  NOT the final map after ExitBootServices.");
  BdPageBufferAdd (&Buf, L"  SMBIOS Type17 capacity != these category totals.");
  BdPageBufferAdd (&Buf, L"");

  // Boot Services 不存在就沒辦法查（保護檢查）。
  if (gBS == NULL) {
    BdPageBufferAdd (&Buf, L"  Memory map            unavailable");
    return BdShowScrollablePage (L"Memory Map", &Buf);
  }

  //
  // 第 1 步：先問需要多大的緩衝區。
  // 故意傳 MapSize = 0 和 Map = NULL，韌體會回傳 EFI_BUFFER_TOO_SMALL，
  // 並把「需要的大小」填進 MapSize，把每筆描述大小填進 DescSize。
  // 這是 UEFI 很常見的「先問大小再取資料」的呼叫模式。
  //
  Map     = NULL;
  MapSize = 0;
  Status  = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVer);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    // 照理應該回傳 BUFFER_TOO_SMALL；若不是，表示發生了別的問題。
    BdPageBufferAdd (&Buf, L"  Memory map            unavailable (%r)", Status);
    return BdShowScrollablePage (L"Memory Map", &Buf);
  }

  //
  // 第 2 步：配置緩衝區並真正取得地圖，最多嘗試 6 次。
  // 為什麼要重試？因為 AllocatePool 會讓地圖多出一筆，
  // 可能導致剛配置的空間又不夠，所以每次迴圈再多加 16 筆描述的餘裕。
  //
  for (Retry = 0; Retry < 6; Retry++) {
    // 多留 16 筆描述大小的額外空間。
    MapSize += DescSize * 16;
    // 如果上一輪已經配置過，先釋放再重新配置較大的（類似 free + malloc）。
    if (Map != NULL) {
      FreePool (Map);
      Map = NULL;
    }

    Map = AllocatePool (MapSize);   // 等同 malloc(MapSize)
    if (Map == NULL) {
      // 記憶體不足 (OOM = Out Of Memory)。
      BdPageBufferAdd (&Buf, L"  Memory map            unavailable (OOM)");
      return BdShowScrollablePage (L"Memory Map", &Buf);
    }

    // 真正取得地圖。成功時 MapSize 會被更新成實際使用的大小。
    Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (!EFI_ERROR (Status)) {
      break;   // 成功，跳出重試迴圈
    }

    // 如果錯誤不是「緩衝區太小」，重試也沒意義，直接放棄（記得釋放記憶體，避免 memory leak）。
    if (Status != EFI_BUFFER_TOO_SMALL) {
      BdPageBufferAdd (&Buf, L"  Memory map            unavailable (%r)", Status);
      FreePool (Map);
      return BdShowScrollablePage (L"Memory Map", &Buf);
    }
  }

  // 重試完仍失敗，或 DescSize 為 0（會造成後面除以 0），都當作失敗處理。
  if (EFI_ERROR (Status) || (DescSize == 0)) {
    BdPageBufferAdd (&Buf, L"  Memory map            unavailable (%r)", Status);
    if (Map != NULL) {
      FreePool (Map);
    }

    return BdShowScrollablePage (L"Memory Map", &Buf);
  }

  // 第 3 步：各類別累加器歸零。
  Conventional = 0;
  BootServices = 0;
  Runtime      = 0;
  Reserved     = 0;
  Acpi         = 0;
  Other        = 0;
  // 總共幾筆描述 = 總 bytes / 每筆 bytes。
  Count        = MapSize / DescSize;

  //
  // 走訪每一筆描述。
  // 【為什麼不能寫 Map[Index]？】
  // 因為韌體回報的 DescSize 可能比 sizeof(EFI_MEMORY_DESCRIPTOR) 大（為了未來擴充），
  // 用陣列下標會用 sizeof 計算位移而算錯。
  // 所以改成：先把 Map 轉成 byte 指標 (UINT8 *)，手動乘上 DescSize 算位址，
  // 再轉型回 struct 指標。這是「用 byte 指標做位址運算」的典型技巧。
  //
  for (Index = 0; Index < Count; Index++) {
    Desc  = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescSize);
    Pages = Desc->NumberOfPages;   // 這段記憶體有幾頁
    // 依「用途類型」把頁數累加到對應類別。這就是一個普通的 switch-case。
    switch (Desc->Type) {
      case EfiConventionalMemory:        // 空閒、可自由使用的記憶體
        Conventional += Pages;
        break;
      case EfiBootServicesCode:          // 開機服務的程式碼
      case EfiBootServicesData:          // 開機服務的資料
        // 兩個 case 寫在一起，表示都做同樣的事（沒有 break 就會「穿透」到下一個 case）。
        BootServices += Pages;
        break;
      case EfiRuntimeServicesCode:       // 執行期服務的程式碼
      case EfiRuntimeServicesData:       // 執行期服務的資料
        Runtime += Pages;
        break;
      case EfiReservedMemoryType:        // 保留給系統用
        Reserved += Pages;
        break;
      case EfiACPIReclaimMemory:         // ACPI 表（OS 讀完後可回收）
      case EfiACPIMemoryNVS:             // ACPI 非揮發性儲存區
        Acpi += Pages;
        break;
      default:                           // 其它所有類型
        Other += Pages;
        break;
    }
  }

  //
  // 第 4 步：輸出結果。
  // 頁數 * EFI_PAGE_SIZE (4096) = bytes；再除以 (1024*1024) = MiB。
  // (UINT32) 是強制轉型，因為 Print 的 %u 預期 32 位元整數。
  //
  BdPageBufferAdd (&Buf, L"  Descriptors           %u", (UINT32)Count);
  BdPageBufferAdd (&Buf, L"  Conventional Memory   %u MiB", (UINT32)((Conventional * EFI_PAGE_SIZE) / (1024 * 1024)));
  BdPageBufferAdd (&Buf, L"  Boot Services Memory  %u MiB", (UINT32)((BootServices * EFI_PAGE_SIZE) / (1024 * 1024)));
  BdPageBufferAdd (&Buf, L"  Runtime Services Mem  %u MiB", (UINT32)((Runtime * EFI_PAGE_SIZE) / (1024 * 1024)));
  BdPageBufferAdd (&Buf, L"  Reserved Memory       %u MiB", (UINT32)((Reserved * EFI_PAGE_SIZE) / (1024 * 1024)));
  BdPageBufferAdd (&Buf, L"  ACPI Memory           %u MiB", (UINT32)((Acpi * EFI_PAGE_SIZE) / (1024 * 1024)));
  BdPageBufferAdd (&Buf, L"  Other Types           %u MiB", (UINT32)((Other * EFI_PAGE_SIZE) / (1024 * 1024)));

  // 第 5 步：用完 malloc 的記憶體一定要 free，這裡 FreePool 等同 free(Map)。
  FreePool (Map);
  // 顯示可捲動頁面，等使用者按 Enter/Esc 才返回。
  return BdShowScrollablePage (L"Memory Map", &Buf);
}

/** @file
  Bounds-checked SMBIOS helpers.

  Adapted from FirmwareDashboardPkg.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// ============================================================================
// 【這個檔案是什麼？】
// SMBIOS 解析工具的標頭檔。
//
// 【SMBIOS 是什麼？】
// 開機時韌體會在記憶體中放一份「硬體清單」：BIOS 版本、主機板廠牌、
// CPU 型號、每條記憶體的容量……這份清單叫 SMBIOS。
// 它的格式是「一個接一個的小 struct（稱為 record / 結構）」，
// 每個 struct 開頭是 header：Type（種類）、Length（這個 struct 的固定部分長度）；
// struct 後面接著「字串區」，字串是用 '\0' 分隔、最後以連續兩個 '\0' 結尾。
// 你可以想成一大塊 byte 陣列，要用指標一個一個往後走訪（像走訪 linked list，
// 只是「下一個」的位置要用 Length 與字串區長度算出來）。
//
// 【為什麼叫 Bounds-checked（有邊界檢查）？】
// 這些資料來自韌體，我們不能盲目相信。每一次用指標往前走，
// 都要先確認沒有超出整張表的範圍，否則會讀到不該讀的記憶體（越界存取）。
// ============================================================================
//

#ifndef BD_SMBIOS_PARSE_H_
#define BD_SMBIOS_PARSE_H_

#include <Uefi.h>
#include <IndustryStandard/SmBios.h>   // 定義 SMBIOS_STRUCTURE、SMBIOS_TABLE_TYPE0/1/4/17... 等 struct

//
// BD_SMBIOS_VIEW：SMBIOS 表的「檢視」(view)，也就是這張表的位置與大小資訊。
// 可以想成：一個「byte 陣列的起點 + 長度」，再加上版本號。
//   Table     ：指向整張 SMBIOS 結構表起點的指標（像 char *buf）
//   TableSize ：整張表的總長度（byte 數），用來做邊界檢查（像 buf 的 size）
//   Major     ：SMBIOS 規格主版本號（例如 3）
//   Minor     ：SMBIOS 規格次版本號（例如 0）
//   Is30      ：TRUE 表示使用 3.x 版入口；FALSE 表示 2.x 版入口
//
typedef struct {
  UINT8    *Table;
  UINTN     TableSize;
  UINT8     Major;
  UINT8     Minor;
  BOOLEAN   Is30;
} BD_SMBIOS_VIEW;

//
// 找出 SMBIOS 表在記憶體中的位置，填進 View。
// 找不到或資料不合理時回傳錯誤碼（例如 EFI_NOT_FOUND）。
//
EFI_STATUS
EFIAPI
BdSmbiosLocate (
  OUT BD_SMBIOS_VIEW  *View
  );

//
// 走到「下一筆」SMBIOS 結構。
//   Current (IN OUT)：指向「目前結構位置」的指標的位址（指標的指標）。
//                     成功時函式會把 *Current 改成下一筆的位置。
//                     這跟 C 裡想「在函式裡修改呼叫者的指標」要傳 char ** 是同一個道理。
// 回傳 TRUE 表示還有下一筆；FALSE 表示走到盡頭或資料有問題。
//
BOOLEAN
EFIAPI
BdSmbiosNext (
  IN     CONST BD_SMBIOS_VIEW  *View,
  IN OUT UINT8                **Current
  );

//
// 取得某筆結構裡「第 StringIndex 個字串」。
// SMBIOS 結構的欄位不直接存字串，而是存「字串編號」（從 1 開始），
// 要用這個函式去字串區查出真正的 C 字串。
// 找不到或不安全時回傳 NULL。
//
CONST CHAR8 *
EFIAPI
BdSmbiosGetString (
  IN CONST SMBIOS_STRUCTURE  *Hdr,
  IN CONST BD_SMBIOS_VIEW    *View,
  IN UINT8                    StringIndex
  );

//
// 檢查某筆結構「是否完整落在表的範圍內」，而且長度夠不夠 RequiredLength。
// 在讀取結構的某個欄位前先呼叫它，確認欄位真的存在（舊版規格的結構可能比較短）。
// 回傳 TRUE 才可以安全地讀取。
//
BOOLEAN
EFIAPI
BdSmbiosHasLength (
  IN CONST SMBIOS_STRUCTURE  *Hdr,
  IN CONST BD_SMBIOS_VIEW    *View,
  IN UINT8                    RequiredLength
  );

//
// 把 Type 17（記憶體條 / DIMM）結構裡的容量欄位換算成「位元組 (bytes)」。
// 回傳 0 表示沒有安裝或容量未知。
//
UINT64
EFIAPI
BdSmbiosMemoryDeviceBytes (
  IN CONST SMBIOS_TABLE_TYPE17  *Type17
  );

#endif

/** @file
  Safe SMBIOS walking — interview talking point:
  never trust Length / string index without checking against TableSize.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// ============================================================================
// 【這個檔案是什麼？】
// 實作「安全地走訪 SMBIOS 表」的五個函式（宣告在 SmbiosParse.h）：
//   BdSmbiosLocate            找到 SMBIOS 表在哪裡
//   FdPtrInTable (STATIC)     檢查指標有沒有超出表的範圍（內部小工具）
//   BdSmbiosHasLength         確認某筆結構完整且夠長
//   BdSmbiosGetString         取出結構裡的字串
//   BdSmbiosNext              走到下一筆結構
//   BdSmbiosMemoryDeviceBytes 把記憶體條容量換算成 bytes
//
// 【SMBIOS 表的記憶體排列（想像成一大塊 byte 陣列）】
//
//   +----------------------+----------------+-----------------+----------+
//   | 結構1 固定部分       | 字串區         | 結構2 固定部分  | ...      |
//   | (Type, Length, ...)  | "A\0B\0C\0\0"  |                 |          |
//   +----------------------+----------------+-----------------+----------+
//   ^ Table                                                              ^ Table + TableSize
//
//   - 固定部分開頭是 SMBIOS_STRUCTURE header：Type（種類）、Length（固定部分長度）、Handle。
//   - 字串區緊跟在固定部分後面，字串以 '\0' 分開，整個字串區以「兩個連續的 '\0'」結尾。
//   - 所以「下一筆結構」的位置 = 目前位置 + Length，再跳過字串區（找到 "\0\0"）。
//
// 【安全重點（面試/考試常被問）】
// 資料是別人（韌體）給的，Length 或字串編號可能是錯的。
// 每次移動指標前都要檢查「還在 Table ~ Table+TableSize 之內」，
// 就像 C 裡存取陣列前先檢查 index < size，否則會越界讀取。
// ============================================================================
//

#include "SmbiosParse.h"

#include <Guid/SmBios.h>                        // gEfiSmbiosTableGuid / gEfiSmbios3TableGuid
#include <Library/BaseMemoryLib.h>              // ZeroMem、CompareMem、CompareGuid（類似 memset / memcmp）
#include <Library/UefiBootServicesTableLib.h>   // gST
#include <Library/UefiLib.h>

/**
  在 UEFI「設定表」中尋找 SMBIOS 入口，並填好 View。

  【背景】
  gST->ConfigurationTable 是一個陣列，每個元素是 { GUID, 指標 }：
  GUID 是一個 128 位元的「名牌」，指標指向對應的資料。
  我們拿 SMBIOS 的 GUID 去陣列裡比對，找到就能拿到 SMBIOS 入口結構 (Entry Point)，
  入口結構裡記錄了真正的 SMBIOS 表位置與大小。
  這就像：在一個 { name, ptr } 的陣列中用 strcmp 找名字，找到後取出 ptr。

  SMBIOS 有兩種入口：3.x（錨點字串 "_SM3_"）與 2.x（錨點字串 "_SM_"）。
  這裡先找 3.x，找不到再找 2.x。「錨點字串」就是用來確認這塊資料真的是 SMBIOS 的魔術字（magic number）。

  @param[out] View  輸出：填入表的位置、大小、版本。

  @retval EFI_SUCCESS            成功找到並填好 View。
  @retval EFI_INVALID_PARAMETER  View 為 NULL 或系統表不存在。
  @retval EFI_NOT_FOUND          找不到 SMBIOS，或表指標/大小無效。
**/
EFI_STATUS
EFIAPI
BdSmbiosLocate (
  OUT BD_SMBIOS_VIEW  *View
  )
{
  UINTN                        Index;   // 走訪設定表的迴圈索引
  SMBIOS_TABLE_3_0_ENTRY_POINT  *Ep3;   // 3.x 入口結構指標
  SMBIOS_TABLE_ENTRY_POINT      *Ep2;   // 2.x 入口結構指標

  // 參數與全域指標檢查（防呆）。
  if ((View == NULL) || (gST == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  // 先把 View 整個清成 0（等同 memset(View, 0, sizeof(*View))）。
  ZeroMem (View, sizeof (*View));

  //
  // 第一輪：先找 SMBIOS 3.x。
  // 這是一個標準 for 迴圈，走訪設定表陣列，共 NumberOfTableEntries 筆。
  //
  for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    // CompareGuid 比較兩個 GUID 是否相同（類似 memcmp，相同回傳 TRUE）。
    if (CompareGuid (&gST->ConfigurationTable[Index].VendorGuid, &gEfiSmbios3TableGuid)) {
      // GUID 對上了，VendorTable 就是入口結構的位址，轉型成對應的 struct 指標。
      Ep3 = (SMBIOS_TABLE_3_0_ENTRY_POINT *)gST->ConfigurationTable[Index].VendorTable;
      // 入口指標為 NULL，或長度比規格最小值還小，視為壞資料，換下一筆繼續找。
      if ((Ep3 == NULL) || (Ep3->EntryPointLength < sizeof (SMBIOS_TABLE_3_0_ENTRY_POINT))) {
        continue;
      }

      // 檢查「錨點字串」是否為 "_SM3_"；不是就不是真的 SMBIOS 3.x，跳過。
      if (CompareMem (Ep3->AnchorString, "_SM3_", SMBIOS_3_0_ANCHOR_STRING_LENGTH) != 0) {
        continue;
      }

      // 通過檢查，把表的位置/大小/版本抄進 View。
      // TableAddress 是一個整數位址，要先轉成 UINTN 再轉成指標（整數 -> 指標的轉型）。
      View->Table     = (UINT8 *)(UINTN)Ep3->TableAddress;
      View->TableSize = Ep3->TableMaximumSize;
      View->Major     = Ep3->MajorVersion;
      View->Minor     = Ep3->MinorVersion;
      View->Is30      = TRUE;
      // 位址或大小無效就當作找不到。
      if ((View->Table == NULL) || (View->TableSize == 0)) {
        return EFI_NOT_FOUND;
      }

      return EFI_SUCCESS;
    }
  }

  //
  // 第二輪：沒有 3.x，改找舊版 SMBIOS 2.x，邏輯跟上面幾乎一樣，
  // 差別只在 GUID、錨點字串 ("_SM_")、以及欄位名稱 (TableLength)。
  //
  for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    if (CompareGuid (&gST->ConfigurationTable[Index].VendorGuid, &gEfiSmbiosTableGuid)) {
      Ep2 = (SMBIOS_TABLE_ENTRY_POINT *)gST->ConfigurationTable[Index].VendorTable;
      if ((Ep2 == NULL) || (Ep2->EntryPointLength < sizeof (SMBIOS_TABLE_ENTRY_POINT))) {
        continue;
      }

      if (CompareMem (Ep2->AnchorString, "_SM_", SMBIOS_ANCHOR_STRING_LENGTH) != 0) {
        continue;
      }

      View->Table     = (UINT8 *)(UINTN)Ep2->TableAddress;
      View->TableSize = Ep2->TableLength;
      View->Major     = Ep2->MajorVersion;
      View->Minor     = Ep2->MinorVersion;
      View->Is30      = FALSE;
      if ((View->Table == NULL) || (View->TableSize == 0)) {
        return EFI_NOT_FOUND;
      }

      return EFI_SUCCESS;
    }
  }

  // 兩種都沒找到。
  return EFI_NOT_FOUND;
}

/**
  內部小工具：檢查「從 Ptr 開始往後 Need 個 byte」是否都還在 SMBIOS 表範圍內。

  白話：就是陣列的「邊界檢查」。
  合法範圍是 [View->Table, View->Table + View->TableSize)。
  STATIC 表示只有本檔案能用。

  @param[in] View  表的位置與大小。
  @param[in] Ptr   想要存取的起始位置。
  @param[in] Need  需要連續可讀的 byte 數。

  @retval TRUE   Ptr 起算 Need bytes 都在表內，可以安全讀取。
  @retval FALSE  會超出範圍或參數不合法。
**/
STATIC
BOOLEAN
FdPtrInTable (
  IN CONST BD_SMBIOS_VIEW  *View,
  IN CONST UINT8           *Ptr,
  IN UINTN                  Need
  )
{
  UINTN  Off;   // Ptr 距離表起點的位移量 (offset)

  // 任何指標是 NULL 都不能用。
  if ((View == NULL) || (View->Table == NULL) || (Ptr == NULL)) {
    return FALSE;
  }

  // Ptr 在表起點「之前」，越界。
  if (Ptr < View->Table) {
    return FALSE;
  }

  // 兩個指標相減得到位移量（單位是 byte，因為指標型別是 UINT8 *）。
  Off = (UINTN)(Ptr - View->Table);
  // 位移量已經超過表大小，越界。
  if (Off > View->TableSize) {
    return FALSE;
  }

  // 剩餘空間 (TableSize - Off) 必須 >= 需要的 byte 數。
  // 先確定 Off <= TableSize 才相減，避免 unsigned 減法變成超大正數（下溢）。
  return (View->TableSize - Off) >= Need;
}

/**
  檢查一筆 SMBIOS 結構是否「完整落在表內」且「固定部分至少 RequiredLength bytes」。

  為什麼需要？不同版本的 SMBIOS 規格，同一種 Type 的結構長度可能不同（新版會多欄位）。
  在讀某個欄位之前，先確認這筆資料真的有包含該欄位，才不會讀到結構之外的垃圾。

  @param[in] Hdr             指向這筆結構的開頭（header）。
  @param[in] View            表的範圍。
  @param[in] RequiredLength  想讀取的欄位需要結構至少多長。

  @retval TRUE   安全，可以讀取。
  @retval FALSE  不安全。
**/
BOOLEAN
EFIAPI
BdSmbiosHasLength (
  IN CONST SMBIOS_STRUCTURE  *Hdr,
  IN CONST BD_SMBIOS_VIEW    *View,
  IN UINT8                    RequiredLength
  )
{
  // 第一關：連 header 本身（Type/Length/Handle 共 4 bytes）都不在表內，就不能讀 Length。
  if ((Hdr == NULL) || !FdPtrInTable (View, (CONST UINT8 *)Hdr, sizeof (SMBIOS_STRUCTURE))) {
    return FALSE;
  }

  // 第二關：結構自己宣告的 Length 比我們需要的還短，代表欄位不存在。
  if (Hdr->Length < RequiredLength) {
    return FALSE;
  }

  // 第三關：整個固定部分 (Hdr->Length 個 byte) 都必須在表內。
  return FdPtrInTable (View, (CONST UINT8 *)Hdr, Hdr->Length);
}

/**
  取得結構中第 StringIndex 個字串（從 1 開始算）。

  【字串區格式】
  固定部分之後是一串以 '\0' 結尾的 C 字串，彼此相連，最後再多一個 '\0'：
      "Vendor\0Version\0Date\0\0"
       ^1        ^2       ^3
  結構欄位只存「第幾個字串」的編號，0 代表「沒有字串」。
  做法：從字串區開頭出發，一個字串一個字串往後跳，數到第 StringIndex 個就回傳指標。
  這跟在 C 裡「在一塊連續記憶體中切割多個 C 字串」是一樣的概念。

  @param[in] Hdr          結構開頭。
  @param[in] View         表的範圍。
  @param[in] StringIndex  字串編號 (1-based)；0 代表沒有。

  @return 指向該字串的指標（直接指向表內，不是複製）；找不到或不安全時回傳 NULL。
**/
CONST CHAR8 *
EFIAPI
BdSmbiosGetString (
  IN CONST SMBIOS_STRUCTURE  *Hdr,
  IN CONST BD_SMBIOS_VIEW    *View,
  IN UINT8                    StringIndex
  )
{
  CONST UINT8  *End;       // 整張表的結尾（第一個「不能碰」的位址）
  CONST CHAR8  *Cursor;    // 目前掃描到的字元位置
  UINT8         Current;   // 目前正在看的是第幾個字串（從 1 起算）

  // 編號 0 = 沒有字串；其它參數是 NULL 也不能做。
  if ((StringIndex == 0) || (Hdr == NULL) || (View == NULL)) {
    return NULL;
  }

  // 先確認固定部分完整在表內。
  if (!BdSmbiosHasLength (Hdr, View, Hdr->Length)) {
    return NULL;
  }

  // End = 表起點 + 表大小，也就是合法範圍的右邊界。
  End    = View->Table + View->TableSize;
  // 字串區的起點 = 結構起點 + 固定部分長度。
  Cursor = (CONST CHAR8 *)((CONST UINT8 *)Hdr + Hdr->Length);
  // 字串區起點本身已經超出表，沒有字串可讀。
  if ((CONST UINT8 *)Cursor >= End) {
    return NULL;
  }

  //
  // No strings at all: double NUL immediately.
  // （白話：字串區一開頭就是兩個 '\0'，表示這個結構完全沒有字串。）
  //
  if ((Cursor[0] == '\0') && (((CONST UINT8 *)Cursor + 1) < End) && (Cursor[1] == '\0')) {
    return NULL;
  }

  // 從第 1 個字串開始數。
  Current = 1;
  // 外層迴圈：一次處理一個字串，直到超出範圍。
  while ((CONST UINT8 *)Cursor < End) {
    if (Current == StringIndex) {
      //
      // Ensure the whole C string stays inside the table.
      // （白話：就是這個字串了！但還要再確認它的 '\0' 結尾真的在表內，
      //  否則 printf("%a") 會一路讀出界。）
      //
      {
        CONST CHAR8  *Scan;

        // 從字串開頭一路掃到遇見 '\0'，找得到才回傳指標。
        for (Scan = Cursor; (CONST UINT8 *)Scan < End; Scan++) {
          if (*Scan == '\0') {
            return Cursor;
          }
        }
      }
      // 掃到表尾都沒找到 '\0'：字串沒有正常結尾，不安全。
      return NULL;
    }

    // 還沒到目標字串：跳過目前這個字串（走到它的 '\0'）。
    while (((CONST UINT8 *)Cursor < End) && (*Cursor != '\0')) {
      Cursor++;
    }

    // 走到表尾還沒遇到 '\0'，資料壞掉。
    if ((CONST UINT8 *)Cursor >= End) {
      return NULL;
    }

    // 越過這個 '\0'，指向下一個字串的第一個字元。
    Cursor++;
    if ((CONST UINT8 *)Cursor >= End) {
      return NULL;
    }

    // 如果越過後馬上又是 '\0'，代表遇到字串區結尾的「雙 '\0'」，沒有更多字串了。
    if (*Cursor == '\0') {
      return NULL;
    }

    // 字串編號加一，繼續找下一個。
    Current++;
  }

  return NULL;
}

/**
  讓 *Current 指向「下一筆」SMBIOS 結構（類似 linked list 的 p = p->next）。

  【怎麼找下一筆？】
  下一筆從「目前結構的固定部分之後，字串區結束」的地方開始。
  字串區以「兩個連續的 0x00」結束，所以：
    1. 先跳過固定部分 (Length bytes)。
    2. 一個 byte 一個 byte 往後掃，直到遇到連續兩個 0。
    3. 這兩個 0 的後面一格，就是下一筆的開頭。

  @param[in]     View     表的範圍。
  @param[in,out] Current  指向「目前結構位址」的指標變數的位址；成功時會被更新成下一筆位址。

  @retval TRUE   成功移到下一筆。
  @retval FALSE  沒有下一筆（到結尾 / Type 127 結束標記 / 資料不合法）。
**/
BOOLEAN
EFIAPI
BdSmbiosNext (
  IN     CONST BD_SMBIOS_VIEW  *View,
  IN OUT UINT8                **Current
  )
{
  SMBIOS_STRUCTURE  *Hdr;      // 目前這筆結構的 header
  UINT8             *Cursor;   // 掃描字串區用的指標
  UINT8             *End;      // 表的結尾

  // 檢查參數；*Current 是目前位置，也不能是 NULL。
  if ((View == NULL) || (Current == NULL) || (*Current == NULL)) {
    return FALSE;
  }

  // 把目前位置轉型成 header 指標，並確認 header 在表內。
  Hdr = (SMBIOS_STRUCTURE *)*Current;
  if (!BdSmbiosHasLength (Hdr, View, sizeof (SMBIOS_STRUCTURE))) {
    return FALSE;
  }

  // Type 127 是 SMBIOS 規格定義的「End-of-Table」結束標記，到這裡就沒有下一筆了。
  if (Hdr->Type == 127) {
    return FALSE;
  }

  // Length 連 header 自己的大小都不到，資料一定壞了（會造成原地踏步或倒退）。
  if (Hdr->Length < sizeof (SMBIOS_STRUCTURE)) {
    return FALSE;
  }

  End    = View->Table + View->TableSize;
  // 從固定部分的後面，也就是字串區的開頭開始掃描。
  Cursor = *Current + Hdr->Length;

  // 迴圈：逐 byte 找「兩個連續的 0」。條件 (Cursor + 1) < End 確保讀 Cursor[1] 不會越界。
  while ((Cursor + 1) < End) {
    if ((Cursor[0] == 0) && (Cursor[1] == 0)) {
      // 找到字串區結尾，下一筆就在這兩個 0 之後（+2）。
      *Current = Cursor + 2;
      // 下一筆起點已經等於或超過表尾：沒有下一筆了。
      if (*Current >= End) {
        return FALSE;
      }

      // 下一筆連 header 都放不下，也視為結束。
      if (!FdPtrInTable (View, *Current, sizeof (SMBIOS_STRUCTURE))) {
        return FALSE;
      }

      return TRUE;
    }

    Cursor++;
  }

  // 掃到表尾都沒找到雙 0，資料不完整。
  return FALSE;
}

/**
  把 SMBIOS Type 17（一條記憶體 DIMM）的 Size 欄位換算成 bytes。

  【Size 欄位的編碼規則（這是 SMBIOS 規格的約定，看起來有點繞）】
    Size == 0        ：這個插槽沒有裝記憶體。
    Size == 0xFFFF   ：容量未知。
    Size == 0x7FFF   ：容量太大，16 位元放不下，要改看 ExtendedSize 欄位（單位 MiB）。
    最高位元 (bit15) = 1：Size 的低 15 bit 以 KiB 為單位。
    其它情況          ：Size 以 MiB 為單位。

  @param[in] Type17  指向 Type 17 結構。

  @return 容量（bytes）；沒有安裝、未知或資料不足時回傳 0。
**/
UINT64
EFIAPI
BdSmbiosMemoryDeviceBytes (
  IN CONST SMBIOS_TABLE_TYPE17  *Type17
  )
{
  UINT64  Mega;   // 以 MiB 為單位的容量（最後會乘上 1024*1024 變成 bytes）

  // NULL 指標就當作沒有容量。
  if (Type17 == NULL) {
    return 0;
  }

  //
  // Size == 0  -> not installed
  // Size == 0xFFFF -> unknown
  //
  if ((Type17->Size == 0) || (Type17->Size == 0xFFFF)) {
    return 0;
  }

  if (Type17->Size == 0x7FFF) {
    //
    // ExtendedSize is in MiB when Size is the special 0x7FFF sentinel.
    // Only use it if the structure is long enough (SMBIOS 2.7+).
    // （sentinel = 哨兵值，也就是一個「特殊意義的數字」，
    //  跟 C 裡用 -1 或 NULL 表示「無效」的概念相同。）
    //
    // 結構太短就沒有 ExtendedSize 欄位，不能讀。
    // OFFSET_OF(型別, 欄位) 等同 C 的 offsetof()，算出欄位在 struct 裡的位移。
    if (Type17->Hdr.Length < (OFFSET_OF (SMBIOS_TABLE_TYPE17, ExtendedSize) + sizeof (UINT32))) {
      return 0;
    }

    Mega = Type17->ExtendedSize;
  } else if ((Type17->Size & BIT15) != 0) {
    //
    // Bit15 set: value in KiB.
    // （BIT15 是巨集，等於 0x8000，用 & 檢查最高位元；
    //  這時要用 & 0x7FFF 把旗標位元遮掉，剩下的才是數值。）
    //
    return ((UINT64)(Type17->Size & 0x7FFF)) * 1024ULL;
  } else {
    // 一般情況：Size 直接就是 MiB。
    Mega = Type17->Size;
  }

  // MiB 換成 bytes：乘上 1024 * 1024。ULL 表示 unsigned long long 常數，避免 32 位元溢位。
  return Mega * 1024ULL * 1024ULL;
}

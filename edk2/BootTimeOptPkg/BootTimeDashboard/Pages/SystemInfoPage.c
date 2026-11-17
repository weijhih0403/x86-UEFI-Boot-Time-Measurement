/** @file
  System Information page (firmware / RTC / SMBIOS).

  Adapted from FirmwareDashboardPkg. QEMU shows guest firmware inventory,
  not host hardware.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// ============================================================================
// 【這個檔案是什麼？】
// 「System Information（系統資訊）」頁面。它收集三類資訊，各由一個函式負責：
//   BdFillFirmware : 韌體廠商、版本、UEFI 規格版本（讀 gST 這個大 struct）
//   BdFillRtc      : 目前的日期時間（呼叫 gRT->GetTime）
//   BdFillSmbios   : BIOS / 系統 / CPU / 記憶體條 清單（走訪 SMBIOS 表）
// 最後由 BdSystemInfoPage 把三者依序放進頁面緩衝區 (BD_PAGE_BUFFER) 並顯示。
//
// 【名詞對照】
//   gST (System Table)   : 全域 struct 指標，韌體資訊都在裡面。
//   gRT (Runtime Services): 執行期服務，裡面有 GetTime 等函式指標。
//   gBS (Boot Services)   : 開機期服務，裡面有 WaitForEvent、GetMemoryMap 等函式指標。
//   OVMF                  : 在 QEMU 虛擬機裡用的 UEFI 韌體，所以看到的是「虛擬機的資訊」，不是你的實體電腦。
// ============================================================================
//

#include "SystemInfoPage.h"
#include "SmbiosParse.h"

#include <IndustryStandard/SmBios.h>              // SMBIOS 各種 Type 的 struct 定義
#include <Library/UefiBootServicesTableLib.h>     // gST、gBS
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>  // gRT

#include "Ui/TextUi.h"                            // BD_PAGE_BUFFER 與相關函式

/**
  把一行「標籤 + ASCII 字串」寫進頁面緩衝區；字串是 NULL 或空字串就顯示 unavailable。

  【為什麼是 ASCII 而不是 CHAR16？】
  SMBIOS 裡的字串是一般 8 位元 char（CHAR8），所以格式用 %a（ASCII）；
  UEFI 的 %s 才是 16 位元寬字串。這點跟 C 的 %s / %ls 的差別類似。
  格式裡的 %-22s 代表「靠左對齊、至少佔 22 格」，讓每行的冒號整齊對齊。

  @param[in,out] Buf    頁面緩衝區。
  @param[in]     Label  左邊的欄位名稱。
  @param[in]     Ascii  要顯示的字串，可以是 NULL。
**/
STATIC
VOID
BdAddAsciiOrUnavailable (
  IN OUT BD_PAGE_BUFFER  *Buf,
  IN     CONST CHAR16    *Label,
  IN     CONST CHAR8     *Ascii
  )
{
  // NULL 或第一個字元就是 '\0'（空字串）都視為沒有資料。
  if ((Ascii == NULL) || (Ascii[0] == '\0')) {
    BdPageBufferAdd (Buf, L"  %-22s unavailable", Label);
  } else {
    BdPageBufferAdd (Buf, L"  %-22s %a", Label, Ascii);
  }
}

/**
  填入「韌體 / UEFI」區塊：廠商、韌體版本、UEFI 規格版本。

  @param[in,out] Buf  頁面緩衝區（輸出）。
**/
STATIC
VOID
BdFillFirmware (
  IN OUT BD_PAGE_BUFFER  *Buf
  )
{
  BdPageBufferAdd (Buf, L"[ Firmware / UEFI ]");
  BdPageBufferAdd (Buf, L"  NOTE: Under QEMU this is guest OVMF info, not the host PC.");

  // gST->FirmwareVendor 是韌體廠商名稱字串（CHAR16 *）。先檢查指標是否有效。
  if ((gST == NULL) || (gST->FirmwareVendor == NULL)) {
    BdPageBufferAdd (Buf, L"  FirmwareVendor        unavailable");
  } else {
    BdPageBufferAdd (Buf, L"  FirmwareVendor        %s", gST->FirmwareVendor);
  }

  if (gST != NULL) {
    // %08x：以 16 進位印出，不足 8 位數補 0。
    BdPageBufferAdd (Buf, L"  FirmwareRevision      0x%08x", gST->FirmwareRevision);
    //
    // UEFI 版本號是把「主版本」與「次版本」塞在同一個 32 位元整數裡：
    //   高 16 位元 = 主版本，低 16 位元 = 次版本。
    // 所以用位移運算 >> 16 取出高 16 位，用 & 0xFFFF 取出低 16 位。
    // 例如 0x0002_0046 會顯示成 2.70。
    // （這就是 C 裡用 bit 運算把多個欄位打包在一個整數的典型技巧。）
    //
    BdPageBufferAdd (
      Buf,
      L"  UEFI Revision         %d.%02d",
      (UINTN)(gST->Hdr.Revision >> 16),
      (UINTN)(gST->Hdr.Revision & 0xFFFF)
      );
  } else {
    BdPageBufferAdd (Buf, L"  FirmwareRevision      unavailable");
    BdPageBufferAdd (Buf, L"  UEFI Revision         unavailable");
  }
}

/**
  填入「即時時鐘 (RTC)」區塊：目前的年月日時分秒。

  做法：呼叫 gRT->GetTime(&Time, NULL)，
  它會把時間填進 EFI_TIME struct（Year / Month / Day / Hour / Minute / Second 等欄位）。
  這很像 C 的 time() + localtime()，不過直接填進 struct。

  @param[in,out] Buf  頁面緩衝區（輸出）。
**/
STATIC
VOID
BdFillRtc (
  IN OUT BD_PAGE_BUFFER  *Buf
  )
{
  EFI_STATUS  Status;   // GetTime 的回傳狀態
  EFI_TIME    Time;     // 時間 struct，GetTime 會把結果寫在這裡

  BdPageBufferAdd (Buf, L"");
  BdPageBufferAdd (Buf, L"[ Real-Time Clock  gRT->GetTime() ]");
  // Runtime Services 不存在就沒辦法讀時間。
  if (gRT == NULL) {
    BdPageBufferAdd (Buf, L"  Current time          unavailable");
    return;
  }

  // 第二個參數 NULL 表示我們不需要「時鐘能力」資訊（Capabilities）。
  Status = gRT->GetTime (&Time, NULL);
  if (EFI_ERROR (Status)) {
    BdPageBufferAdd (Buf, L"  Current time          unavailable (%r)", Status);
    return;
  }

  // %04d-%02d-%02d ...：用 0 補齊的日期時間格式，如 2026-11-14 00:24:00。
  BdPageBufferAdd (
    Buf,
    L"  Current time          %04d-%02d-%02d %02d:%02d:%02d",
    Time.Year,
    Time.Month,
    Time.Day,
    Time.Hour,
    Time.Minute,
    Time.Second
    );
}

/**
  填入「SMBIOS 硬體清單」區塊：BIOS、系統、CPU、記憶體條資訊。

  【整體做法（就像走訪 linked list）】
    1. BdSmbiosLocate 找到表的位置。
    2. 用一個指標 Cursor 指向第一筆結構。
    3. for(;;) 無限迴圈：
         - 看目前這筆的 Type（種類）是哪一種，是我們有興趣的就印出來。
         - 用 BdSmbiosNext 把 Cursor 移到下一筆；沒有下一筆就 break。

  【Type 對照表】
    Type 0  = BIOS 資訊       Type 1  = 系統資訊（廠牌、型號）
    Type 4  = 處理器 (CPU)    Type 17 = 記憶體條 (DIMM)

  【重要的防呆習慣】
  每次要讀結構裡某個欄位前，都先用 BdSmbiosHasLength 檢查
  「這筆結構是否夠長、有包含該欄位」，因為舊版 SMBIOS 的結構可能比較短。
  這裡常見的 OFFSET_OF(struct, 欄位) 等同 C 的 offsetof()，
  能算出欄位在 struct 裡的位移，用來表示「至少要有到這個欄位的長度」。

  @param[in,out] Buf  頁面緩衝區（輸出）。
**/
STATIC
VOID
BdFillSmbios (
  IN OUT BD_PAGE_BUFFER  *Buf
  )
{
  EFI_STATUS            Status;         // 狀態碼
  BD_SMBIOS_VIEW        View;           // SMBIOS 表的位置/大小/版本
  UINT8                *Cursor;         // 目前走到的結構位置（像鏈結串列的 current 指標）
  SMBIOS_STRUCTURE     *Hdr;            // 把 Cursor 當成「通用 header」來看
  SMBIOS_TABLE_TYPE0   *Type0;          // 把同一塊記憶體當成 Type 0 (BIOS) struct 來看
  SMBIOS_TABLE_TYPE1   *Type1;          // ...Type 1 (系統)
  SMBIOS_TABLE_TYPE4   *Type4;          // ...Type 4 (CPU)
  SMBIOS_TABLE_TYPE17  *Type17;         // ...Type 17 (記憶體條)
  UINTN                 ProcShown;      // 已經顯示了幾顆 CPU（最多顯示 2 顆，避免頁面太長）
  UINTN                 MemShown;       // 已經遇到幾條有安裝的記憶體
  UINT64                SmbiosBytes;    // 所有記憶體條容量加總（bytes）
  UINT64                DeviceBytes;    // 目前這一條的容量（bytes）

  BdPageBufferAdd (Buf, L"");
  BdPageBufferAdd (Buf, L"[ SMBIOS inventory ]");
  // 第一步：找到 SMBIOS 表。找不到就顯示原因然後結束這個區塊。
  Status = BdSmbiosLocate (&View);
  if (EFI_ERROR (Status)) {
    BdPageBufferAdd (Buf, L"  Table                 unavailable (%r)", Status);
    return;
  }

  BdPageBufferAdd (
    Buf,
    L"  Spec                  %u.%u (%a)",
    View.Major,
    View.Minor,
    View.Is30 ? "3.x entry" : "2.x entry"   // 三元運算子：依旗標選擇要顯示的字串
    );

  // 第二步：初始化走訪用的變數，Cursor 指向第一筆結構（表的起點）。
  Cursor      = View.Table;
  ProcShown   = 0;
  MemShown    = 0;
  SmbiosBytes = 0;

  // 第三步：走訪所有結構。for ( ; ; ) 是無限迴圈，靠 break 離開。
  for ( ; ;) {
    // 把目前位置「解讀」成通用 header。
    // 這是 C 的指標轉型：同一塊記憶體可以用不同 struct 型別的眼光去看。
    Hdr = (SMBIOS_STRUCTURE *)Cursor;
    // 連 header 都不完整就停止走訪。
    if (!BdSmbiosHasLength (Hdr, &View, sizeof (SMBIOS_STRUCTURE))) {
      break;
    }

    // 依 Type 欄位決定要不要顯示，以及怎麼顯示（if / else if 鏈，等同 switch）。
    if (Hdr->Type == SMBIOS_TYPE_BIOS_INFORMATION) {
      // ---- Type 0：BIOS 資訊 ----
      Type0 = (SMBIOS_TABLE_TYPE0 *)Hdr;
      // 確認結構長度至少涵蓋到 BiosCharacteristics 之前的所有欄位（我們要讀的欄位都在那之前）。
      if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE0, BiosCharacteristics))) {
        BdPageBufferAdd (Buf, L"  -- BIOS (Type 0) --");
        // 欄位裡存的是「字串編號」，要用 BdSmbiosGetString 換成真正的字串。
        BdAddAsciiOrUnavailable (Buf, L"Vendor", BdSmbiosGetString (Hdr, &View, Type0->Vendor));
        BdAddAsciiOrUnavailable (Buf, L"Version", BdSmbiosGetString (Hdr, &View, Type0->BiosVersion));
        BdAddAsciiOrUnavailable (Buf, L"Release Date", BdSmbiosGetString (Hdr, &View, Type0->BiosReleaseDate));
      }
    } else if (Hdr->Type == SMBIOS_TYPE_SYSTEM_INFORMATION) {
      // ---- Type 1：系統資訊（製造商、型號、序號）----
      Type1 = (SMBIOS_TABLE_TYPE1 *)Hdr;
      if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE1, Uuid))) {
        BdPageBufferAdd (Buf, L"  -- System (Type 1) --");
        BdAddAsciiOrUnavailable (Buf, L"Manufacturer", BdSmbiosGetString (Hdr, &View, Type1->Manufacturer));
        BdAddAsciiOrUnavailable (Buf, L"Product Name", BdSmbiosGetString (Hdr, &View, Type1->ProductName));
        BdAddAsciiOrUnavailable (Buf, L"Version", BdSmbiosGetString (Hdr, &View, Type1->Version));
        BdAddAsciiOrUnavailable (Buf, L"Serial Number", BdSmbiosGetString (Hdr, &View, Type1->SerialNumber));
      }
    } else if (Hdr->Type == SMBIOS_TYPE_PROCESSOR_INFORMATION) {
      // ---- Type 4：處理器 (CPU) ----
      Type4 = (SMBIOS_TABLE_TYPE4 *)Hdr;
      // 需要結構長度涵蓋到 CurrentSpeed 欄位（含它本身 2 bytes），而且最多只顯示 2 顆 CPU。
      if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE4, CurrentSpeed) + sizeof (UINT16)) &&
          (ProcShown < 2))
      {
        BdPageBufferAdd (Buf, L"  -- Processor (Type 4) #%u --", (UINT32)(ProcShown + 1));
        BdAddAsciiOrUnavailable (Buf, L"Socket", BdSmbiosGetString (Hdr, &View, Type4->Socket));
        BdAddAsciiOrUnavailable (Buf, L"Manufacturer", BdSmbiosGetString (Hdr, &View, Type4->ProcessorManufacturer));
        BdAddAsciiOrUnavailable (Buf, L"Version", BdSmbiosGetString (Hdr, &View, Type4->ProcessorVersion));
        // CurrentSpeed / MaxSpeed 是直接存的數值（單位 MHz），不是字串編號。
        BdPageBufferAdd (
          Buf,
          L"  %-22s %u MHz (max %u)",
          L"Current Speed",
          Type4->CurrentSpeed,
          Type4->MaxSpeed
          );
        // 核心數欄位在較新的規格才有，所以要再檢查一次結構長度夠不夠。
        if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE4, CoreCount) + sizeof (UINT8))) {
          BdPageBufferAdd (
            Buf,
            L"  %-22s cores=%u enabled=%u threads=%u",
            L"Topology",
            Type4->CoreCount,
            Type4->EnabledCoreCount,
            Type4->ThreadCount
            );
        }

        ProcShown++;   // 又多顯示了一顆 CPU
      }
    } else if (Hdr->Type == SMBIOS_TYPE_MEMORY_DEVICE) {
      // ---- Type 17：記憶體條 (DIMM) ----
      Type17 = (SMBIOS_TABLE_TYPE17 *)Hdr;
      if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE17, Size) + sizeof (UINT16))) {
        // 把 Size 欄位的特殊編碼換算成 bytes（0 = 沒裝）。
        DeviceBytes = BdSmbiosMemoryDeviceBytes (Type17);
        if (DeviceBytes > 0) {
          // 這條有裝：累加進總容量。
          SmbiosBytes += DeviceBytes;
          // 只詳細列出前 4 條，避免頁面太長；但總容量仍會統計所有條。
          if (MemShown < 4) {
            BdPageBufferAdd (Buf, L"  -- Memory Device (Type 17) #%u --", (UINT32)(MemShown + 1));
            BdAddAsciiOrUnavailable (Buf, L"Locator", BdSmbiosGetString (Hdr, &View, Type17->DeviceLocator));
            BdAddAsciiOrUnavailable (Buf, L"Manufacturer", BdSmbiosGetString (Hdr, &View, Type17->Manufacturer));
            BdAddAsciiOrUnavailable (Buf, L"Part Number", BdSmbiosGetString (Hdr, &View, Type17->PartNumber));
            // 如果結構夠長、且 Speed 欄位非 0，就一併顯示速度 (MT/s)；否則只顯示容量。
            if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE17, Speed) + sizeof (UINT16)) &&
                (Type17->Speed != 0))
            {
              BdPageBufferAdd (
                Buf,
                L"  %-22s %u MiB @ %u MT/s",
                L"Size",
                (UINT32)(DeviceBytes / (1024ULL * 1024ULL)),   // bytes 換成 MiB
                Type17->Speed
                );
            } else {
              BdPageBufferAdd (
                Buf,
                L"  %-22s %u MiB",
                L"Size",
                (UINT32)(DeviceBytes / (1024ULL * 1024ULL))
                );
            }
          }

          MemShown++;   // 有裝的記憶體條數量 +1（不論有沒有被詳細列出）
        }
      }
    }

    // 移到下一筆結構；沒有下一筆（或資料有問題）就離開迴圈。
    // 注意：BdSmbiosNext 會直接修改 Cursor 的值，所以傳 &Cursor（指標的位址）。
    if (!BdSmbiosNext (&View, &Cursor)) {
      break;
    }
  }

  // 第四步：整理記憶體總結。
  if (MemShown == 0) {
    BdPageBufferAdd (Buf, L"  Memory Device         unavailable / empty");
  } else {
    BdPageBufferAdd (
      Buf,
      L"  SMBIOS DIMM total     %u MiB  (%u populated module(s))",
      (UINT32)(SmbiosBytes / (1024ULL * 1024ULL)),
      (UINT32)MemShown
      );
    BdPageBufferAdd (Buf, L"  NOTE: Physical inventory from Type 17; not UEFI Memory Map.");
  }
}

/**
  「System Information」頁面的對外入口（主選單選到它時會被呼叫）。

  做法很簡單：清空緩衝區 -> 依序填入三個區塊 -> 顯示可捲動頁面。

  @retval EFI_SUCCESS  使用者正常離開此頁面。
  @retval Others       顯示頁面時發生的錯誤（例如讀鍵盤失敗）。
**/
EFI_STATUS
EFIAPI
BdSystemInfoPage (
  VOID
  )
{
  // 頁面緩衝區（區域變數，約 23 KB）。
  BD_PAGE_BUFFER  Buf;

  BdPageBufferReset (&Buf);     // 把 Count 歸零，準備開始寫
  BdFillFirmware (&Buf);        // 區塊 1：韌體資訊
  BdFillRtc (&Buf);             // 區塊 2：日期時間
  BdFillSmbios (&Buf);          // 區塊 3：SMBIOS 硬體清單
  // 交給 UI 顯示，使用者按 Enter / Esc 才會返回。
  return BdShowScrollablePage (L"System Information", &Buf);
}

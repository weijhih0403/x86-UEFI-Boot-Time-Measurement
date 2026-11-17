/** @file
  Read BootTimeSummary / BootTimeHotspotLog via existing package GUID/structs.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// ============================================================================
// 【這個檔案是什麼？】
// 「Boot-Time Measurements（開機時間量測）」頁面。
// 量測驅動透過 BootTimeMeasurement Protocol 提供「這次開機」即時資料，
// 並順便寫 NVRAM 快照。本頁優先 LocateProtocol，找不到再 GetVariable。
//
// 【三種被讀取的資料】
//   BOOT_TIME_SUMMARY      : 各開機階段之間的耗時摘要
//   BOOT_TIME_BDS_BREAKDOWN: BDS 階段（開機裝置選擇）內部的細項耗時
//   BOOT_TIME_HOTSPOT_LOG  : 耗時最久的項目排行榜 (hotspot)
//
// 【為什麼每筆資料都要檢查 Signature / Version / Size？】
// 因為變數可能不存在、可能是舊版格式、可能被改壞。
// 就像讀取二進位檔案時，先檢查「檔頭魔術數字 (magic)」「版本」「長度」再使用，
// 這是處理外部資料時的基本防禦。
//
// 【時間單位】
// 資料中的時間單位是奈秒 (ns)。1 ms = 1,000,000 ns。
// 顯示時分成「整數毫秒」與「小數三位」，避免使用浮點數：
//   整數部分 = Ns / 1000000
//   小數部分 = (Ns / 1000) % 1000   （也就是微秒 µs）
// ============================================================================
//

#include "BootTimePage.h"

#include <Guid/BootTimeOptPkgVariable.h>
#include <Protocol/BootTimeMeasurement.h>

#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "Ui/TextUi.h"

STATIC
EFI_BOOT_TIME_MEASUREMENT_PROTOCOL *
BdLocateMeasurement (
  VOID
  )
{
  EFI_STATUS                          Status;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;

  Status = gBS->LocateProtocol (
                  &gEfiBootTimeMeasurementProtocolGuid,
                  NULL,
                  (VOID **)&Meas
                  );
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  return Meas;
}

/**
  輸出一行「項目名稱 + 時間」；若該階段沒有走到，則顯示 Not reached。

  @param[in,out] Buf      頁面緩衝區。
  @param[in]     Label    左側的項目名稱。
  @param[in]     Ns       時間，單位奈秒。
  @param[in]     Reached  這個階段是否真的發生過（TRUE 才顯示數字）。
**/
STATIC
VOID
BdAddMsOrNotReached (
  IN OUT BD_PAGE_BUFFER  *Buf,
  IN     CONST CHAR16    *Label,
  IN     UINT64           Ns,
  IN     BOOLEAN          Reached
  )
{
  if (!Reached) {
    // 沒走到：這個時間值是無意義的，所以不印數字。%-36s 表示靠左對齊佔 36 格。
    BdPageBufferAdd (Buf, L"  %-36s Not reached", Label);
  } else {
    // 把奈秒換成「毫秒.微秒」格式，例如 12345678 ns -> 12.345 ms。
    // %03u 表示補 0 到三位數。(UINT32) 是強制轉型，因為 %u 對應 32 位元整數。
    BdPageBufferAdd (
      Buf,
      L"  %-36s %u.%03u ms",
      Label,
      (UINT32)(Ns / 1000000ULL),
      (UINT32)((Ns / 1000ULL) % 1000ULL)
      );
  }
}

/**
  讀取 BootTimeSummary 變數，並驗證其內容是否可信。

  驗證順序（由外到內，任一步失敗就回傳錯誤並把原因寫進 ErrMsg）：
    1. 變數存在嗎？            （EFI_NOT_FOUND）
    2. 讀取有沒有出錯？        （其它 EFI_ERROR）
    3. 大小夠不夠一個 struct？ （Size 太小）
    4. Signature 是否正確？    （像檔案開頭的 magic number）
    5. Version 是否支援？

  @param[out] Summary      輸出：讀到的摘要資料。
  @param[out] ErrMsg       輸出：失敗時的人類可讀原因（寬字串緩衝區）。
  @param[in]  ErrMsgChars  ErrMsg 緩衝區可放多少個 CHAR16 字元。

  @retval EFI_SUCCESS               讀取並驗證成功。
  @retval EFI_NOT_FOUND             變數不存在。
  @retval EFI_INVALID_PARAMETER     參數錯誤、大小太小或 Signature 錯誤。
  @retval EFI_INCOMPATIBLE_VERSION  版本不相容。
  @retval Others                    GetVariable 本身的錯誤碼。
**/
STATIC
EFI_STATUS
BdLoadSummary (
  OUT BOOT_TIME_SUMMARY  *Summary,
  OUT CHAR16             *ErrMsg,
  IN  UINTN               ErrMsgChars,
  OUT BOOLEAN            *FromProtocol
  )
{
  EFI_STATUS                          Status;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;
  UINTN                               Size;
  UINT32                              Attr;

  if ((Summary == NULL) || (ErrMsg == NULL) || (ErrMsgChars == 0) || (FromProtocol == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ErrMsg[0]      = L'\0';
  *FromProtocol  = FALSE;

  Meas = BdLocateMeasurement ();
  if ((Meas != NULL) && (Meas->GetSummary != NULL)) {
    Status = Meas->GetSummary (Meas, Summary);
    if (!EFI_ERROR (Status)) {
      *FromProtocol = TRUE;
      goto Validate;
    }
  }

  Size   = sizeof (*Summary);
  Status = gRT->GetVariable (
                  BOOT_TIME_SUMMARY_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  &Attr,
                  &Size,
                  Summary
                  );
  if (Status == EFI_NOT_FOUND) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"BootTimeSummary not found (Protocol/NVRAM).");
    return Status;
  }

  if (EFI_ERROR (Status)) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"Load Summary failed: %r", Status);
    return Status;
  }

  if (Size < sizeof (*Summary)) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"BootTimeSummary size too small (%u).", (UINT32)Size);
    return EFI_INVALID_PARAMETER;
  }

Validate:
  if (Summary->Signature != BOOT_TIME_SUMMARY_SIGNATURE) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"BootTimeSummary bad Signature.");
    return EFI_INVALID_PARAMETER;
  }

  if (Summary->Version != BOOT_TIME_SUMMARY_VERSION) {
    UnicodeSPrint (
      ErrMsg,
      ErrMsgChars * sizeof (CHAR16),
      L"BootTimeSummary unsupported Version %u (need %u).",
      Summary->Version,
      BOOT_TIME_SUMMARY_VERSION
      );
    return EFI_INCOMPATIBLE_VERSION;
  }

  return EFI_SUCCESS;
}

/**
  讀取 Hotspot（耗時排行榜）變數，並驗證其內容。

  跟 BdLoadSummary 類似，但 Hotspot 結構的尾端有一個「長度不固定的陣列」，
  所以大小檢查只要求「至少包含到 Hotspot 陣列之前的 header 部分」，
  之後再用 Count 欄位檢查陣列筆數是否合理。

  @param[out] Log          輸出：讀到的排行榜資料。
  @param[out] ErrMsg       輸出：失敗原因文字。
  @param[in]  ErrMsgChars  ErrMsg 可放的 CHAR16 字元數。
  @param[out] Present      輸出：TRUE 表示有成功取得可用資料。

  @retval EFI_SUCCESS               成功。
  @retval EFI_NOT_FOUND             變數不存在。
  @retval EFI_INVALID_PARAMETER     大小/Signature/Count 不合理。
  @retval EFI_INCOMPATIBLE_VERSION  版本不相容。
  @retval Others                    GetVariable 本身的錯誤碼。
**/
STATIC
EFI_STATUS
BdLoadHotspot (
  OUT BOOT_TIME_HOTSPOT_LOG  *Log,
  OUT CHAR16                 *ErrMsg,
  IN  UINTN                   ErrMsgChars,
  OUT BOOLEAN                *Present,
  OUT BOOLEAN                *FromProtocol
  )
{
  EFI_STATUS                          Status;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;
  UINTN                               Size;
  UINT32                              Attr;
  BOOLEAN                             SkipSizeCheck;

  *Present      = FALSE;
  *FromProtocol = FALSE;
  ErrMsg[0]     = L'\0';
  SkipSizeCheck = FALSE;

  Meas = BdLocateMeasurement ();
  if ((Meas != NULL) && (Meas->GetHotspotLog != NULL)) {
    Status = Meas->GetHotspotLog (Meas, Log);
    if (!EFI_ERROR (Status)) {
      *FromProtocol = TRUE;
      SkipSizeCheck = TRUE;
      goto ValidateHotspot;
    }
  }

  Size   = sizeof (*Log);
  Status = gRT->GetVariable (
                  BOOT_TIME_HOTSPOT_LOG_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  &Attr,
                  &Size,
                  Log
                  );
  if (Status == EFI_NOT_FOUND) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"Hotspot log not present (collector off or never written).");
    return Status;
  }

  if (EFI_ERROR (Status)) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"Load Hotspot failed: %r", Status);
    return Status;
  }

ValidateHotspot:
  if (!SkipSizeCheck && (Size < OFFSET_OF (BOOT_TIME_HOTSPOT_LOG, Hotspot))) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"Hotspot log size too small (%u).", (UINT32)Size);
    return EFI_INVALID_PARAMETER;
  }

  if (Log->Signature != BOOT_TIME_HOTSPOT_SIGNATURE) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"Hotspot log bad Signature.");
    return EFI_INVALID_PARAMETER;
  }

  if (Log->Version != BOOT_TIME_HOTSPOT_VERSION) {
    UnicodeSPrint (
      ErrMsg,
      ErrMsgChars * sizeof (CHAR16),
      L"Hotspot log unsupported Version %u (need %u).",
      Log->Version,
      BOOT_TIME_HOTSPOT_VERSION
      );
    return EFI_INCOMPATIBLE_VERSION;
  }

  if (Log->Count > BOOT_TIME_HOTSPOT_MAX_RANK) {
    UnicodeSPrint (ErrMsg, ErrMsgChars * sizeof (CHAR16), L"Hotspot Count %u exceeds max.", Log->Count);
    return EFI_INVALID_PARAMETER;
  }

  *Present = TRUE;
  return EFI_SUCCESS;
}

/**
  「Boot-Time Measurements」頁面的對外入口（主選單選到它時被呼叫）。

  頁面分成四個區塊，全部先寫進緩衝區 Buf，最後一次顯示：
    1. 頂端說明（提醒：這些是存在 NVRAM 的紀錄，可能是上一次開機留下的）
    2. [Phase intervals]：各階段之間的耗時
    3. [BDS breakdown]：BDS 階段內部細項
    4. [Hotspot]：耗時排行榜

  @retval EFI_SUCCESS  使用者正常離開頁面。
  @retval Others       顯示頁面時發生的錯誤。
**/
EFI_STATUS
EFIAPI
BdBootTimePage (
  VOID
  )
{
  BD_PAGE_BUFFER         Buf;
  BOOT_TIME_SUMMARY      Summary;
  BOOT_TIME_HOTSPOT_LOG  Hotspot;
  CHAR16                 Err[96];
  EFI_STATUS             Status;
  BOOLEAN                HotspotPresent;
  BOOLEAN                SummaryFromProto;
  BOOLEAN                HotspotFromProto;
  BOOLEAN                BdsFromProto;
  UINTN                  Index;
  CONST CHAR16          *SourceLabel;
  BOOLEAN                EndReached;
  BOOLEAN                ReadyReached;
  BOOLEAN                EbsReached;

  BdPageBufferReset (&Buf);
  BdPageBufferAdd (&Buf, L"  Prefer BootTimeMeasurement Protocol (this boot live).");
  BdPageBufferAdd (&Buf, L"  NVRAM fallback may be a previous boot if Protocol missing.");
  BdPageBufferAdd (&Buf, L"");
  BdPageBufferAdd (&Buf, L"  Profiler Entry = BootTimeProfilerDxe start (NOT power-on).");
  BdPageBufferAdd (&Buf, L"  Interactive mode does not call ExitBootServices;");
  BdPageBufferAdd (&Buf, L"  EBS stays Not reached here. Use BootTimeDump for EBS.");
  BdPageBufferAdd (&Buf, L"");

  Status = BdLoadSummary (&Summary, Err, ARRAY_SIZE (Err), &SummaryFromProto);
  if (EFI_ERROR (Status)) {
    // 讀取或驗證失敗：把原因顯示在頁面上，而不是讓程式當掉。
    BdPageBufferAdd (&Buf, L"[ BootTimeSummary ]");
    BdPageBufferAdd (&Buf, L"  %s", Err);
  } else {
    //
    // Reserved0 欄位在這裡被當成「旗標 (flags) 位元組」使用：
    // 每個 bit 代表一個階段「有沒有走到」。
    // 用 & (bitwise AND) 加上遮罩常數就能取出特定 bit，
    // != 0 表示該 bit 為 1。這是 C 裡常見的「位元旗標」技巧（跟 O_RDONLY | O_CREAT 類似的概念）。
    //
    EndReached   = (BOOLEAN)((Summary.Reserved0 & BOOT_TIME_PHASE_END_OF_DXE) != 0);
    ReadyReached = (BOOLEAN)((Summary.Reserved0 & BOOT_TIME_PHASE_READY_TO_BOOT) != 0);
    EbsReached   = (BOOLEAN)((Summary.Reserved0 & BOOT_TIME_PHASE_EXIT_BOOT_SERVICES) != 0);

    BdPageBufferAdd (
      &Buf,
      L"[ Phase intervals ]  source=%s",
      SummaryFromProto ? L"Protocol" : L"NVRAM"
      );
    // 以下四行：每一行顯示一段「從 A 階段到 B 階段」的耗時。
    // 只有「起點和終點兩個階段都走到」時，這段時間才有意義，所以第四個參數用 && 組合條件。
    BdAddMsOrNotReached (
      &Buf,
      L"Profiler Entry -> EndOfDxe",
      Summary.DeltaEntryToEndOfDxeNs,
      EndReached
      );
    BdAddMsOrNotReached (
      &Buf,
      L"EndOfDxe -> ReadyToBoot",
      Summary.DeltaEndOfDxeToReadyToBootNs,
      (BOOLEAN)(EndReached && ReadyReached)
      );
    BdAddMsOrNotReached (
      &Buf,
      L"ReadyToBoot -> ExitBootServices",
      Summary.DeltaReadyToBootToExitBootServicesNs,
      (BOOLEAN)(ReadyReached && EbsReached)
      );
    BdAddMsOrNotReached (
      &Buf,
      L"Profiler Entry -> ExitBootServices",
      Summary.DeltaEntryToExitBootServicesNs,
      EbsReached
      );
    // 三元運算子 ? : 把 TRUE/FALSE 轉成 1/0 方便印出。
    BdPageBufferAdd (
      &Buf,
      L"  Flags  EndOfDxe=%u ReadyToBoot=%u ExitBootServices=%u",
      EndReached ? 1 : 0,
      ReadyReached ? 1 : 0,
      EbsReached ? 1 : 0
      );
    // %04x：16 進位、補 0 到 4 位，例如 Boot0001。
    BdPageBufferAdd (&Buf, L"  LastBootCurrent          Boot%04x", Summary.LastBootCurrent);
  }

  // ---- 區塊 3：BDS 階段細項 ----
  BdPageBufferAdd (&Buf, L"");
  BdPageBufferAdd (&Buf, L"[ BDS breakdown  EndOfDxe->ReadyToBoot ]");
  {
    BOOT_TIME_BDS_BREAKDOWN             Bds;
    EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;
    UINTN                               BdsSize;
    UINT32                              BdsAttr;

    BdsFromProto = FALSE;
    Meas         = BdLocateMeasurement ();
    if ((Meas != NULL) && (Meas->GetBdsBreakdown != NULL)) {
      Status = Meas->GetBdsBreakdown (Meas, &Bds);
      if (!EFI_ERROR (Status) &&
          (Bds.Signature == BOOT_TIME_BDS_BREAKDOWN_SIGNATURE))
      {
        BdsFromProto = TRUE;
      }
    }

    if (!BdsFromProto) {
      BdsSize = sizeof (Bds);
      Status  = gRT->GetVariable (
                       BOOT_TIME_BDS_BREAKDOWN_VAR_NAME,
                       &gBootTimeOptPkgVariableGuid,
                       &BdsAttr,
                       &BdsSize,
                       &Bds
                       );
      if (EFI_ERROR (Status) ||
          (BdsSize != sizeof (Bds)) ||
          (Bds.Signature != BOOT_TIME_BDS_BREAKDOWN_SIGNATURE))
      {
        BdPageBufferAdd (&Buf, L"  unavailable (%r)", Status);
        goto AfterBds;
      }
    }

    BdPageBufferAdd (
      &Buf,
      L"  source=%s",
      BdsFromProto ? L"Protocol" : L"NVRAM"
      );
    {
      // 下面每一項都用 Bds.Flags 的對應 bit 判斷該細項「是否有被量測到」。
      BdAddMsOrNotReached (
        &Buf,
        L"PreConnect (EoD->Connect)",
        Bds.PreConnectNs,
        (BOOLEAN)((Bds.Flags & BOOT_TIME_BDS_FLAG_CONNECT) != 0)
        );
      BdAddMsOrNotReached (
        &Buf,
        L"ConnectAll",
        Bds.ConnectAllNs,
        (BOOLEAN)((Bds.Flags & BOOT_TIME_BDS_FLAG_CONNECT) != 0)
        );
      BdAddMsOrNotReached (
        &Buf,
        L"RefreshBootOptions",
        Bds.RefreshBootOptionsNs,
        (BOOLEAN)((Bds.Flags & BOOT_TIME_BDS_FLAG_REFRESH) != 0)
        );
      BdAddMsOrNotReached (
        &Buf,
        L"PostRefresh -> ReadyToBoot",
        Bds.PostRefreshToReadyToBootNs,
        (BOOLEAN)((Bds.Flags & BOOT_TIME_BDS_FLAG_READY_TO_BOOT) != 0)
        );
      BdAddMsOrNotReached (
        &Buf,
        L"LoadPath (RTB->Dump entry)",
        Bds.LoadPathNs,
        (BOOLEAN)((Bds.Flags & BOOT_TIME_BDS_FLAG_LOAD_PATH) != 0)
        );
      BdPageBufferAdd (&Buf, L"  Flags                    0x%x", (UINT32)Bds.Flags);
    }
AfterBds:
    ;
  }

  // ---- 區塊 4：耗時排行榜 (Hotspot) ----
  BdPageBufferAdd (&Buf, L"");
  BdPageBufferAdd (&Buf, L"[ Hotspot / duration items ]");
  Status = BdLoadHotspot (&Hotspot, Err, ARRAY_SIZE (Err), &HotspotPresent, &HotspotFromProto);
  if (!HotspotPresent) {
    BdPageBufferAdd (&Buf, L"  %s", Err);
  } else {
    if (Hotspot.Source == BOOT_TIME_HOTSPOT_SOURCE_GAUGE) {
      SourceLabel = L"Performance gauge records (not phase fallback)";
    } else if (Hotspot.Source == BOOT_TIME_HOTSPOT_SOURCE_PHASE_FALLBACK) {
      SourceLabel = L"Phase fallback intervals (NOT per-driver ranking)";
    } else {
      SourceLabel = L"Unknown source";
    }

    BdPageBufferAdd (
      &Buf,
      L"  Transport: %s",
      HotspotFromProto ? L"Protocol" : L"NVRAM"
      );
    BdPageBufferAdd (&Buf, L"  Source: %s", SourceLabel);
    BdPageBufferAdd (&Buf, L"  GaugeEntryCount=%u  Count=%u", Hotspot.GaugeEntryCount, Hotspot.Count);

    if (Hotspot.Count == 0) {
      BdPageBufferAdd (&Buf, L"  (no ranked entries)");
    } else {
      // 走訪排行榜陣列（就是普通的 for 迴圈走訪 struct 陣列）。
      // Count 已經在 BdLoadHotspot 檢查過不會超過上限，所以不會越界。
      for (Index = 0; Index < Hotspot.Count; Index++) {
        // %a 印 ASCII 字串（Module / Token 是 char 陣列，不是寬字串）。
        BdPageBufferAdd (
          &Buf,
          L"  #%u  %a / %a",
          Hotspot.Hotspot[Index].Rank,
          Hotspot.Hotspot[Index].Module,
          Hotspot.Hotspot[Index].Token
          );
        // 如果 Module 欄位的內容恰好是 "NOT_REACHED"（用 AsciiStrCmp，等同 strcmp），
        // 代表這一筆沒有量測到時間。
        if (AsciiStrCmp (Hotspot.Hotspot[Index].Module, "NOT_REACHED") == 0) {
          BdPageBufferAdd (&Buf, L"      Not reached");
        } else {
          // 奈秒換成「毫秒.微秒」（同 BdAddMsOrNotReached 的算法）。
          BdPageBufferAdd (
            &Buf,
            L"      %u.%03u ms",
            (UINT32)(Hotspot.Hotspot[Index].DurationNs / 1000000ULL),
            (UINT32)((Hotspot.Hotspot[Index].DurationNs / 1000ULL) % 1000ULL)
            );
        }
      }
    }
  }

  // 全部內容準備好，交給 UI 顯示成可捲動頁面。使用者按 Enter / Esc 才會返回。
  return BdShowScrollablePage (L"Boot-Time Measurements", &Buf);
}

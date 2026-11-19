/** @file
  Print BootTimeSummary / BootTimeHotspotLog / LastSuccessfulBootOption,
  then call ExitBootServices so Profiler can record the final phase.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

/*
  =====================================================================
  BootTimeDump.c ── 把 NVRAM 裡的開機計時資料「印出來」（教學註解版）
  =====================================================================

  【這個檔案在做什麼？】
  量測服務（BootTimeMeasurement Protocol）在同開機內持有即時資料；
  本程式優先 LocateProtocol 讀取，找不到再 GetVariable（NVRAM 快照）。
  印成 CSV 後呼叫 ExitBootServices，讓 Profiler 補上最後階段。

  【驅動 vs. 應用程式（Application）】
  - 驅動（DXE_DRIVER）  ：載入後留在記憶體，等事件來了再被呼叫。
  - 應用程式（UEFI_APPLICATION）：像一般 C 程式，跑完就結束。
    本檔是應用程式，所以進入點叫 UefiMain，有點像 C 的 main()。
    （進入點名稱在 .inf 裡用 ENTRY_POINT 指定。）

  【整體流程（UefiMain 做的事）】
    1. BootTimeBdsMarkLoadPathAtAppEntry()：記一個時間點，
       用來算「ReadyToBoot → 本程式開始執行」花了多久（載入應用程式的時間）
    2. 依序印出四類資料：Summary、BDS 細分、Hotspot 排行榜、最後成功開機選項
    3. 呼叫 ExitBootServices：觸發 Profiler 記下「最後一個階段」並從序列埠輸出
    4. 之後 Boot Services 已不能用，程式停在無限迴圈（CpuDeadLoop）

  【名詞對照（給只學過 C 的你）】
  - NVRAM / UEFI Variable：韌體裡「關機後還在」的小倉庫（用名字＋GUID 存取的資料）。
  - GetVariable：讀取；SetVariable：寫入（本檔只讀）。
  - Print：UEFI 版 printf（輸出到螢幕）；L"..." 是寬字元字串（Unicode）。
      常見格式：%Lu = UINT64；%u = 無號整數；%a = 窄字串（char *）；
                %r = EFI_STATUS 轉成文字；%04x = 4 位十六進位補 0。
  - CSV：逗號分隔的純文字格式，一行一筆資料。
  - EFIAPI：UEFI 規定的呼叫慣例標記，進入點函式要寫。
  - STATIC：等同 C 的 static，函式只在本 .c 內可見。
  - IN / OUT：說明用空巨集，IN 表示輸入、OUT 表示函式會寫回結果。
  - gRT / gBS：指向「執行期服務 / 開機服務」函式表的全域指標。
  =====================================================================
*/

#include <Uefi.h>                              // UEFI 基本型別

#include <Guid/BootTimeOptPkgVariable.h>       // 本專案：變數名稱、struct、旗標定義
#include <Protocol/BootTimeMeasurement.h>      // 同開機量測服務

#include <Library/BaseLib.h>                   // CpuDeadLoop 等基本函式
#include <Library/BootTimeBdsProbeLib.h>       // 本專案：BDS 細分計時輔助（這裡用其標記函式）
#include <Library/MemoryAllocationLib.h>       // AllocatePool / FreePool（類似 malloc / free）
#include <Library/PcdLib.h>                    // PCD：編譯期設定值（像 #define 設定開關）
#include <Library/UefiBootServicesTableLib.h>  // gBS
#include <Library/UefiLib.h>                   // Print
#include <Library/UefiRuntimeServicesTableLib.h> // gRT

STATIC
EFI_BOOT_TIME_MEASUREMENT_PROTOCOL *
LocateMeasurement (
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

STATIC
EFI_STATUS
LoadSummary (
  OUT BOOT_TIME_SUMMARY  *Summary
  )
{
  EFI_STATUS                          Status;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;
  UINTN                               Size;
  UINT32                              Attr;

  Meas = LocateMeasurement ();
  if ((Meas != NULL) && (Meas->GetSummary != NULL)) {
    Status = Meas->GetSummary (Meas, Summary);
    if (!EFI_ERROR (Status)) {
      return Status;
    }
  }

  Size = sizeof (*Summary);
  return gRT->GetVariable (
                BOOT_TIME_SUMMARY_VAR_NAME,
                &gBootTimeOptPkgVariableGuid,
                &Attr,
                &Size,
                Summary
                );
}

STATIC
EFI_STATUS
LoadBdsBreakdown (
  OUT BOOT_TIME_BDS_BREAKDOWN  *Breakdown
  )
{
  EFI_STATUS                          Status;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;
  UINTN                               Size;
  UINT32                              Attr;

  Meas = LocateMeasurement ();
  if ((Meas != NULL) && (Meas->GetBdsBreakdown != NULL)) {
    Status = Meas->GetBdsBreakdown (Meas, Breakdown);
    if (!EFI_ERROR (Status) &&
        (Breakdown->Signature == BOOT_TIME_BDS_BREAKDOWN_SIGNATURE))
    {
      return EFI_SUCCESS;
    }
  }

  Size   = sizeof (*Breakdown);
  Status = gRT->GetVariable (
                  BOOT_TIME_BDS_BREAKDOWN_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  &Attr,
                  &Size,
                  Breakdown
                  );
  if (EFI_ERROR (Status) ||
      (Size != sizeof (*Breakdown)) ||
      (Breakdown->Signature != BOOT_TIME_BDS_BREAKDOWN_SIGNATURE))
  {
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
LoadHotspot (
  OUT BOOT_TIME_HOTSPOT_LOG  *Log
  )
{
  EFI_STATUS                          Status;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;
  UINTN                               Size;
  UINT32                              Attr;

  Meas = LocateMeasurement ();
  if ((Meas != NULL) && (Meas->GetHotspotLog != NULL)) {
    Status = Meas->GetHotspotLog (Meas, Log);
    if (!EFI_ERROR (Status)) {
      return Status;
    }
  }

  Size = sizeof (*Log);
  return gRT->GetVariable (
                BOOT_TIME_HOTSPOT_LOG_VAR_NAME,
                &gBootTimeOptPkgVariableGuid,
                &Attr,
                &Size,
                Log
                );
}

// ---------------------------------------------------------------------
// 函式：DumpSummary
// 用途：讀 Summary（優先 Protocol，再 NVRAM），印出來。
// ---------------------------------------------------------------------
STATIC
VOID
DumpSummary (
  IN BOOLEAN  Csv
  )
{
  EFI_STATUS         Status;
  BOOT_TIME_SUMMARY  S;

  Status = LoadSummary (&S);
  if (EFI_ERROR (Status)) {
    if (Csv) {
      Print (L"summary,error,%r\n", Status);
    } else {
      Print (L"BootTimeSummary: not found (%r)\n", Status);
    }
    return;
  }

  if (Csv) {
    // 表頭：CSV 的欄位名稱（section=分類, metric=指標名, value=數值）
    Print (L"section,metric,value\n");
    Print (L"summary,EntryToEndOfDxeNs,%Lu\n", S.DeltaEntryToEndOfDxeNs);
    Print (L"summary,EndOfDxeToReadyToBootNs,%Lu\n", S.DeltaEndOfDxeToReadyToBootNs);
    Print (L"summary,ReadyToBootToExitBootServicesNs,%Lu\n", S.DeltaReadyToBootToExitBootServicesNs);
    Print (L"summary,EntryToExitBootServicesNs,%Lu\n", S.DeltaEntryToExitBootServicesNs);
    // Reserved0 是「位元旗標」，用 & 取出某個 bit：
    // (旗標 & 遮罩) != 0 → 該階段有發生。外面再轉成 UINT32 當 0/1 印出（%u）
    Print (L"summary,EndOfDxeReached,%u\n", (UINT32)((S.Reserved0 & BOOT_TIME_PHASE_END_OF_DXE) != 0));
    Print (L"summary,ReadyToBootReached,%u\n", (UINT32)((S.Reserved0 & BOOT_TIME_PHASE_READY_TO_BOOT) != 0));
    Print (L"summary,ExitBootServicesReached,%u\n", (UINT32)((S.Reserved0 & BOOT_TIME_PHASE_EXIT_BOOT_SERVICES) != 0));
    // FixedPcdGetBool：取得「編譯時就固定」的布林設定（像條件編譯的開關），
    // 這裡印出 Policy / Hotspot 功能是否被啟用（A/B 比較時會用到）
    Print (L"summary,PolicyEnabled,%u\n", (UINT32)FixedPcdGetBool (PcdBootTimeOptEnablePolicy));
    Print (L"summary,HotspotEnabled,%u\n", (UINT32)FixedPcdGetBool (PcdBootTimeOptEnableHotspot));
    Print (L"summary,LastBootCurrent,%u\n", (UINT32)S.LastBootCurrent);
  } else {
    // 人類好讀版：用 yes/no 取代 0/1。(條件) ? "yes" : "no" 是 C 的三元運算子
    Print (L"=== BootTimeSummary ===\n");
    Print (L"Entry->EndOfDxe:               %Lu ns\n", S.DeltaEntryToEndOfDxeNs);
    Print (L"EndOfDxe->ReadyToBoot:         %Lu ns\n", S.DeltaEndOfDxeToReadyToBootNs);
    Print (L"ReadyToBoot->ExitBootServices: %Lu ns\n", S.DeltaReadyToBootToExitBootServicesNs);
    Print (L"Entry->ExitBootServices:       %Lu ns\n", S.DeltaEntryToExitBootServicesNs);
    Print (L"EndOfDxe reached:              %a\n", (S.Reserved0 & BOOT_TIME_PHASE_END_OF_DXE) ? "yes" : "no");
    Print (L"ReadyToBoot reached:           %a\n", (S.Reserved0 & BOOT_TIME_PHASE_READY_TO_BOOT) ? "yes" : "no");
    Print (L"ExitBootServices reached:      %a\n", (S.Reserved0 & BOOT_TIME_PHASE_EXIT_BOOT_SERVICES) ? "yes" : "no");
    Print (L"Policy enabled:                %a\n", FixedPcdGetBool (PcdBootTimeOptEnablePolicy) ? "yes" : "no");
    Print (L"Hotspot enabled:               %a\n", FixedPcdGetBool (PcdBootTimeOptEnableHotspot) ? "yes" : "no");
    Print (L"LastBootCurrent:               Boot%04x\n", S.LastBootCurrent);
  }
}

// ---------------------------------------------------------------------
// 函式：DumpBdsBreakdown
// 用途：讀 "BootTimeBdsBreakdown" 並印出。
//       這份資料把 EndOfDxe→ReadyToBoot 這個大區間再切成：
//       PreConnect / ConnectAll / RefreshBootOptions / PostRefresh→ReadyToBoot，
//       以及 LoadPath（ReadyToBoot 到本程式開始執行）。
// 與 DumpSummary 不同：這裡多做了「簽名檢查」──
//   除了讀取成功，還要確認「大小一樣」且「Signature 正確」，
//   避免讀到別人／舊版本寫的不相容資料。
// ---------------------------------------------------------------------
STATIC
VOID
DumpBdsBreakdown (
  IN BOOLEAN  Csv
  )
{
  EFI_STATUS               Status;
  BOOT_TIME_BDS_BREAKDOWN  B;

  Status = LoadBdsBreakdown (&B);
  if (EFI_ERROR (Status)) {
    if (Csv) {
      Print (L"bds,error,%r\n", Status);
    } else {
      Print (L"BootTimeBdsBreakdown: not found (%r)\n", Status);
    }
    return;
  }

  if (Csv) {
    Print (L"bds,PreConnectNs,%Lu\n", B.PreConnectNs);
    Print (L"bds,ConsolePrepNs,%Lu\n", B.ConsolePrepNs);
    Print (L"bds,LogoNs,%Lu\n", B.LogoNs);
    Print (L"bds,PciAcpiNs,%Lu\n", B.PciAcpiNs);
    Print (L"bds,BootOrderNs,%Lu\n", B.BootOrderNs);
    Print (L"bds,PreConnectOtherNs,%Lu\n", B.PreConnectOtherNs);
    Print (L"bds,ConnectAllNs,%Lu\n", B.ConnectAllNs);
    Print (L"bds,RefreshBootOptionsNs,%Lu\n", B.RefreshBootOptionsNs);
    Print (L"bds,PostRefreshToReadyToBootNs,%Lu\n", B.PostRefreshToReadyToBootNs);
    Print (L"bds,LoadPathNs,%Lu\n", B.LoadPathNs);
    Print (L"bds,Flags,%u\n", (UINT32)B.Flags);   // Flags 是位元旗標：哪些子區間有量到
  } else {
    Print (L"=== BootTimeBdsBreakdown ===\n");
    Print (L"PreConnect:                    %Lu ns\n", B.PreConnectNs);
    Print (L"  ConsolePrep:                 %Lu ns\n", B.ConsolePrepNs);
    Print (L"  Logo:                        %Lu ns\n", B.LogoNs);
    Print (L"  PciAcpi:                     %Lu ns\n", B.PciAcpiNs);
    Print (L"  BootOrder:                   %Lu ns\n", B.BootOrderNs);
    Print (L"  PreConnectOther:             %Lu ns\n", B.PreConnectOtherNs);
    Print (L"ConnectAll:                    %Lu ns\n", B.ConnectAllNs);
    Print (L"RefreshBootOptions:            %Lu ns\n", B.RefreshBootOptionsNs);
    Print (L"PostRefresh->ReadyToBoot:      %Lu ns\n", B.PostRefreshToReadyToBootNs);
    Print (L"LoadPath (RTB->Dump entry):    %Lu ns\n", B.LoadPathNs);
    Print (L"Flags:                         0x%x\n", (UINT32)B.Flags);
  }
}

// ---------------------------------------------------------------------
// 函式：DumpHotspot
// 用途：讀 "BootTimeHotspotLog"（最慢項目的排行榜）並印出。
// 資料結構：H.Hotspot[] 是 struct 陣列，H.Count 是「有效幾筆」，
//          最多不超過 BOOT_TIME_HOTSPOT_MAX_RANK（目前是 5 名）。
// 來源（H.Source）：
//   GAUGE          → 來自較細的效能表（逐項模組／動作的耗時）
//   其他（PHASE）  → 只有大階段（fallback：沒有細表時的備案）
// ---------------------------------------------------------------------
STATIC
VOID
DumpHotspot (
  IN BOOLEAN  Csv
  )
{
  EFI_STATUS             Status;
  BOOT_TIME_HOTSPOT_LOG  H;
  UINTN                  Idx;

  Status = LoadHotspot (&H);
  if (EFI_ERROR (Status)) {
    if (Csv) {
      Print (L"hotspot,error,%r\n", Status);
    } else {
      Print (L"BootTimeHotspotLog: not found (%r)\n", Status);
    }
    return;
  }

  // 先印表頭
  if (Csv) {
    Print (L"rank,source,duration_ns,module,token\n");
  } else {
    Print (L"=== BootTimeHotspotLog ===\n");
  }

  // 逐筆印出。條件寫兩個是保險：Count 若被寫壞，也不會超出陣列範圍（避免越界）
  for (Idx = 0; Idx < H.Count && Idx < BOOT_TIME_HOTSPOT_MAX_RANK; Idx++) {
    if (Csv) {
      // %a：印 CHAR8 字串（Module、Token 是 char 陣列）
      Print (
        L"%u,%a,%Lu,%a,%a\n",
        (UINT32)H.Hotspot[Idx].Rank,
        (H.Source == BOOT_TIME_HOTSPOT_SOURCE_GAUGE) ? "gauge" : "phase",
        H.Hotspot[Idx].DurationNs,
        H.Hotspot[Idx].Module,
        H.Hotspot[Idx].Token
        );
    } else {
      Print (
        L"#%u %Lu ns [%a] %a\n",
        (UINT32)H.Hotspot[Idx].Rank,
        H.Hotspot[Idx].DurationNs,
        H.Hotspot[Idx].Module,
        H.Hotspot[Idx].Token
        );
    }
  }
}

// ---------------------------------------------------------------------
// 函式：DumpLastBootOption
// 用途：讀 "LastSuccessfulBootOption"（上次／這次使用的開機選項編號）並印出。
// 這個變數只有一個 UINT16，所以不需要 struct。
// ---------------------------------------------------------------------
STATIC
VOID
DumpLastBootOption (
  IN BOOLEAN  Csv
  )
{
  EFI_STATUS  Status;
  UINT16      BootCurrent;   // 開機選項編號，例如 1 代表 Boot0001
  UINTN       Size;
  UINT32      Attr;

  Size = sizeof (BootCurrent);
  Status = gRT->GetVariable (
                  BOOT_TIME_LAST_BOOT_OPTION_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  &Attr,
                  &Size,
                  &BootCurrent
                  );
  if (EFI_ERROR (Status)) {
    if (Csv) {
      Print (L"last_boot,error,%r\n", Status);
    } else {
      Print (L"LastSuccessfulBootOption: not found (%r)\n", Status);
    }
    return;
  }

  if (Csv) {
    Print (L"last_successful_bootoption,%u\n", (UINT32)BootCurrent);
  } else {
    Print (L"LastSuccessfulBootOption: Boot%04x\n", BootCurrent);
  }
}

/**
  Call ExitBootServices. BootTimeProfilerDxe's notify updates NVRAM and emits
  final EBS CSV lines on the serial port (Print is unsafe after this returns).
**/
/*
  ---------------------------------------------------------------------
  函式：CallExitBootServices
  用途：呼叫 ExitBootServices，讓 Profiler 的 callback 被觸發，記錄最後階段。

  為什麼這麼麻煩？ExitBootServices(ImageHandle, MapKey) 需要「記憶體地圖的鑰匙 MapKey」：
    - 記憶體地圖（Memory Map）= 韌體目前「哪段記憶體做什麼用」的清單
    - MapKey 像是這份清單的「版本號」；清單一旦改變（例如又配置了記憶體），
      舊的 MapKey 就會失效，ExitBootServices 會失敗。
  所以流程是：
    1. 第一次 GetMemoryMap(大小=0)：故意讓它失敗（EFI_BUFFER_TOO_SMALL），
       藉此得知「大概需要多大的緩衝區」與每筆描述的大小 DescriptorSize
    2. 配置緩衝區（多留一些空間，因為配置本身也可能讓地圖變大）
    3. 再次 GetMemoryMap 取得地圖與最新 MapKey
    4. 馬上呼叫 ExitBootServices；若因 MapKey 過期而失敗就重試（最多 8 次）

  參數：ImageHandle —— 本應用程式的把手（ExitBootServices 要求要傳）
  回傳：EFI_SUCCESS 成功；其他為錯誤碼（EFI_NOT_READY = 重試用完仍失敗）
  注意：成功之後 Boot Services 失效，不可再 Print、不可再 AllocatePool，
        所以成功時「刻意不 FreePool」（FreePool 屬於 Boot Services 的功能）。
  ---------------------------------------------------------------------
*/
STATIC
EFI_STATUS
CallExitBootServices (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS             Status;
  UINTN                  MemoryMapSize;     // 緩衝區大小（IN/OUT）
  EFI_MEMORY_DESCRIPTOR  *MemoryMap;        // 指向「記憶體地圖」緩衝區的指標（動態配置）
  UINTN                  MapKey;            // 地圖版本鑰匙
  UINTN                  DescriptorSize;    // 每一筆描述的位元組數（不一定等於 sizeof(EFI_MEMORY_DESCRIPTOR)）
  UINT32                 DescriptorVersion; // 描述格式版本
  UINTN                  Retry;             // 重試次數計數

  MemoryMap     = NULL;   // 還沒配置，先設 NULL（C 好習慣：指標一律初始化）
  MemoryMapSize = 0;
  // 第一次呼叫：緩衝區給 NULL、大小 0 → 預期回傳 BUFFER_TOO_SMALL，並把「需要的大小」寫回 MemoryMapSize
  Status        = gBS->GetMemoryMap (
                         &MemoryMapSize,
                         MemoryMap,
                         &MapKey,
                         &DescriptorSize,
                         &DescriptorVersion
                         );
  // 如果不是預期的 BUFFER_TOO_SMALL，代表出了別的狀況，直接回報
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return Status;
  }

  for (Retry = 0; Retry < 8; Retry++) {
    // 多加 32 筆描述的空間當作緩衝（因為接下來 AllocatePool 本身可能讓地圖多出幾筆）
    MemoryMapSize += DescriptorSize * 32;
    // 若上一輪已配置過，先釋放，免得記憶體洩漏（memory leak）
    if (MemoryMap != NULL) {
      FreePool (MemoryMap);
      MemoryMap = NULL;
    }

    // AllocatePool 類似 malloc：配置 MemoryMapSize 位元組
    MemoryMap = AllocatePool (MemoryMapSize);
    if (MemoryMap == NULL) {
      return EFI_OUT_OF_RESOURCES;   // 配置失敗（記憶體不足）
    }

    // 第二次呼叫：這次有緩衝區，取得真正的地圖與最新 MapKey
    Status = gBS->GetMemoryMap (
                    &MemoryMapSize,
                    MemoryMap,
                    &MapKey,
                    &DescriptorSize,
                    &DescriptorVersion
                    );
    if (EFI_ERROR (Status)) {
      // 緩衝區還是不夠 → 回到 for 迴圈開頭，加大後再試
      if (Status == EFI_BUFFER_TOO_SMALL) {
        continue;
      }

      // 其他錯誤：釋放記憶體後離開
      FreePool (MemoryMap);
      return Status;
    }

    // 拿到最新 MapKey 後「立刻」呼叫（中間不能再做會改變記憶體配置的事）
    Status = gBS->ExitBootServices (ImageHandle, MapKey);
    if (!EFI_ERROR (Status)) {
      // 成功！此時 Boot Services 已結束，不能再 FreePool，直接回傳
      return EFI_SUCCESS;
    }
    // 失敗（多半是 MapKey 過期）→ for 迴圈下一輪重新取得地圖
  }

  // 重試 8 次都失敗：釋放緩衝區並回報「尚未就緒」
  if (MemoryMap != NULL) {
    FreePool (MemoryMap);
  }

  return EFI_NOT_READY;
}

/*
  ---------------------------------------------------------------------
  函式：UefiMain ── 本應用程式的「main()」
  參數：
    ImageHandle ：本程式映像檔的把手（身分證）
    SystemTable ：系統表指標（gBS、gRT 已由函式庫設定好，所以這裡沒直接用）
  回傳：EFI_STATUS 狀態碼
  ---------------------------------------------------------------------
*/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  BOOLEAN     Csv;
  EFI_STATUS  Status;

  Csv = TRUE;   // 固定輸出 CSV；若要人類好讀格式可改成 FALSE（這裡不改程式行為，僅說明）

  //
  // Capture ReadyToBoot -> Dump entry before printing (LoadImage/StartImage approx).
  //
  // 說明：先記下「現在」這個時間點，之後可算出
  // ReadyToBoot → 本程式開始執行 的時間（約等於載入並啟動這支 .efi 的耗時）。
  // 一定要在 Print 之前呼叫，否則印字的時間會被算進去。
  BootTimeBdsMarkLoadPathAtAppEntry ();

  //
  // Print while Boot Services / ConOut are still valid. EBS fields are still
  // zero here; BootTimeProfilerDxe appends the final EBS CSV lines on serial
  // from its ExitBootServices notify (SerialPortLib, not Print).
  //
  // 說明：此時 Boot Services 還有效，所以可以用 Print。
  // 此時 ExitBootServices 還沒發生，所以 ExitBootServices 相關欄位仍是 0；
  // 最終數值會由 Profiler 在 ExitBootServices 時，直接透過序列埠補印。
  DumpSummary (Csv);
  DumpBdsBreakdown (Csv);
  DumpHotspot (Csv);
  DumpLastBootOption (Csv);

  // 呼叫 ExitBootServices（會觸發 Profiler 的 OnExitBootServices callback）
  Status = CallExitBootServices (ImageHandle);
  if (EFI_ERROR (Status)) {
    // 失敗時 Boot Services 仍有效，所以還能用 Print 回報
    Print (L"summary,ExitBootServicesError,%r\n", Status);
    return Status;
  }

  // 成功後韌體服務已結束，沒有「回到 Shell」的安全方式，所以停在這裡不再往下執行。
  // CpuDeadLoop：無限迴圈（類似 while(1);），讓 CPU 原地等待。
  CpuDeadLoop ();
  return EFI_SUCCESS;   // 理論上不會執行到，但 C 編譯器需要函式有 return
}

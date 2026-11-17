/** @file
  Collects PerformanceEx gauge data and persists the Top-N hotspots.
  When gauge data is unavailable, records the DXE phases that actually ran.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

/*
  =====================================================================
  BootTimeHotspotDxe.c ── 開機「最慢項目排行榜」驅動（教學註解版）
  =====================================================================

  【這個檔案在做什麼？】
  BootTimeProfilerDxe 只知道「大階段」各花多久；
  本驅動想回答更細的問題：「到底是哪個模組／動作最慢？」
  做法像是：開機結束前，把所有「小計時紀錄」拿出來，依耗時由長到短排序，
  取前 N 名（N = BOOT_TIME_HOTSPOT_MAX_RANK，目前是 5），
  經 BootTimeMeasurement Protocol Publish（即時副本 + NVRAM 快照）。
  之後 BootTimeDump／Dashboard 可 LocateProtocol 或讀 NVRAM。

  【兩種資料來源（有備案）】
    1. GAUGE（理想）：從 PerformanceEx Protocol 取得韌體內建的逐項計時紀錄
       （每筆有：模組名、動作名 Token、開始與結束時間戳）。
    2. PHASE_FALLBACK（備案）：如果取不到上面的資料（韌體沒開效能記錄功能），
       就改用本驅動自己記的「大階段」時間湊成排行榜。

  【流程】
    進入點 → 登記兩個事件（EndOfDxe、ReadyToBoot）
      EndOfDxe 發生   → 只記下當下 tick
      ReadyToBoot 發生 → 檢查開關 → 取資料 → 排序 → Publish 到量測 Protocol

  【名詞對照（給只學過 C 的你）】
  - DXE driver：韌體啟動時載入的小程式，沒有 main()，用「進入點函式」開始。
  - NVRAM / UEFI Variable：韌體裡「關機後還在」的小倉庫；用「名稱 + GUID」存取。
  - tick：硬體計時器的滴答數；要換算成奈秒（ns）才有意義。
  - Protocol：韌體提供的「功能介面」，用 LocateProtocol 找到後，
              會得到一個 struct 指標，裡面有函式指標可呼叫（很像 C 的 vtable）。
  - PCD：編譯期設定值，這裡當「功能開關」用，像 #define ENABLE_XXX。
  - EFIAPI：UEFI 的呼叫慣例標記，callback 與進入點函式都要寫。
  - STATIC：等同 C 的 static；函式／全域變數只在本檔可見。
  - IN / OUT / IN OUT：說明用空巨集。IN 輸入、OUT 輸出、IN OUT 讀了又改。
  - CONST：等同 C 的 const。
  - CHAR8：8 位元字元，等同 char。UINT8/16/32/64、UINTN：固定寬度整數／原生字長整數。
  =====================================================================
*/

#include <Uefi.h>                              // UEFI 基本型別

#include <Guid/EventGroup.h>                   // 事件群組 GUID（EndOfDxe、ReadyToBoot）
#include <Guid/BootTimeOptPkgVariable.h>       // 本專案：NVRAM 變數名稱與 struct 定義
#include <Guid/Performance.h>                  // 效能記錄（PerformanceEx）相關型別與 GUID
#include <Protocol/BootTimeMeasurement.h>      // 同開機量測服務
#include <Guid/GlobalVariable.h>               // UEFI 標準全域變數 GUID（BootCurrent 用）

#include <Library/BaseLib.h>                   // AsciiStrCpyS（安全字串複製）等
#include <Library/BaseMemoryLib.h>             // ZeroMem、CopyMem（類似 memset、memcpy）
#include <Library/DebugLib.h>                  // DEBUG() 除錯輸出
#include <Library/MemoryAllocationLib.h>       // AllocateZeroPool、FreePool（類似 calloc、free）
#include <Library/PcdLib.h>                    // FixedPcdGetBool：讀編譯期設定開關
#include <Library/TimerLib.h>                  // GetPerformanceCounter、GetTimeInNanoSecond
#include <Library/UefiBootServicesTableLib.h>  // gBS
#include <Library/UefiRuntimeServicesTableLib.h> // gRT

// 候選池最多放 800 筆：從效能表撈出來的項目先放進記憶體排序，
// 設上限避免資料太多時把記憶體吃光。
#define HOTSPOT_POOL_MAX  800

// 一筆「候選項目」：名字 + 耗時。
// 後面會把很多筆放進陣列、依 DurationNs 排序，再取前幾名。
// 字串欄位是固定長度的 char 陣列（不是指標），所以 struct 可以整包複製（CopyMem）。
typedef struct {
  UINT64  DurationNs;                         // 這個項目花了幾奈秒
  CHAR8   Module[DXE_PERFORMANCE_STRING_SIZE]; // 模組名稱（char 陣列）
  CHAR8   Token[DXE_PERFORMANCE_STRING_SIZE];  // 動作／標記名稱（char 陣列）
} HOTSPOT_CANDIDATE;

// 全域變數（STATIC = 只有本檔看得到）
STATIC EFI_EVENT  mEndOfDxeEvent     = NULL;  // EndOfDxe 事件把手（可想成事件 ID）
STATIC EFI_EVENT  mReadyToBootEvent  = NULL;  // ReadyToBoot 事件把手
STATIC UINT64     mEntryTick         = 0;     // 驅動進入點的 tick（起跑點）
STATIC UINT64     mEndOfDxeTick      = 0;     // EndOfDxe 發生時的 tick；0 = 還沒發生
STATIC UINT64     mReadyToBootTick   = 0;     // ReadyToBoot 發生時的 tick；0 = 還沒發生

// ---------------------------------------------------------------------
// 函式：GetDurationNsSafe
// 用途：「安全地」把兩個 tick 轉成間隔奈秒。
// 若起點／終點為 0（沒量到）或終點比起點小（異常），回傳 0，
// 避免 UINT64 無號數相減「下溢」變成超大數字。
// ---------------------------------------------------------------------
STATIC
UINT64
GetDurationNsSafe (
  IN UINT64  StartTick,
  IN UINT64  EndTick
  )
{
  // 無效資料 → 0
  if ((StartTick == 0) || (EndTick == 0) || (EndTick < StartTick)) {
    return 0;
  }

  // 正常：tick 差 → 奈秒
  return GetTimeInNanoSecond (EndTick - StartTick);
}

// ---------------------------------------------------------------------
// 函式：AddPhase
// 用途：在排行榜（Log）最後面「新增一列」：模組名、標記名、耗時。
//       類似對陣列做 append：先檢查有沒有滿，再寫到 Hotspot[Count]，最後 Count++。
// 參數：
//   Log        ：IN OUT，指向要被修改的排行榜 struct（傳指標才能改到原本的資料）
//   Module     ：模組名稱字串（CONST = 函式內不會修改它）
//   Token      ：標記字串
//   DurationNs ：耗時（奈秒）
// ---------------------------------------------------------------------
STATIC
VOID
AddPhase (
  IN OUT BOOT_TIME_HOTSPOT_LOG  *Log,
  IN CONST CHAR8                *Module,
  IN CONST CHAR8                *Token,
  IN UINT64                     DurationNs
  )
{
  UINTN  Index;

  // 陣列已滿就不再加（避免寫到陣列外面，也就是「越界」）
  if (Log->Count >= BOOT_TIME_HOTSPOT_MAX_RANK) {
    return;
  }

  Index = Log->Count;  // 下一個空位的索引
  Log->Hotspot[Index].Rank       = (UINT32)(Index + 1);  // 暫時名次（之後排序會重算）
  Log->Hotspot[Index].DurationNs = DurationNs;
  // AsciiStrCpyS：安全版 strcpy，會帶上目的緩衝區大小，避免超出長度
  AsciiStrCpyS (Log->Hotspot[Index].Module, sizeof (Log->Hotspot[Index].Module), Module);
  AsciiStrCpyS (Log->Hotspot[Index].Token, sizeof (Log->Hotspot[Index].Token), Token);
  Log->Count++;  // 有效筆數加 1
}

// ---------------------------------------------------------------------
// 函式：SortPhaseDesc
// 用途：把排行榜依耗時「由大到小」排序（Desc = descending 遞減），最後重新編號名次。
// 演算法：選擇排序（selection sort 的簡單變體）：
//   對每個位置 Index，往後找比它大的項目，若有就交換。資料很少（最多 5 筆），夠用。
// 交換：這裡用 CopyMem（像 memcpy）整塊複製 struct，
//       三步驟交換：Swap←A、A←B、B←Swap（跟交換兩個 int 的方法一樣）。
// ---------------------------------------------------------------------
STATIC
VOID
SortPhaseDesc (
  IN OUT BOOT_TIME_HOTSPOT_LOG  *Log
  )
{
  UINTN  Index;
  UINTN  Other;

  for (Index = 0; Index < Log->Count; Index++) {
    for (Other = Index + 1; Other < Log->Count; Other++) {
      // 後面的比前面的大 → 交換，讓大的排前面
      if (Log->Hotspot[Other].DurationNs > Log->Hotspot[Index].DurationNs) {
        BOOT_TIME_HOTSPOT_ENTRY  Swap;  // 暫存用 struct（區域變數）
        CopyMem (&Swap, &Log->Hotspot[Index], sizeof (Swap));
        CopyMem (&Log->Hotspot[Index], &Log->Hotspot[Other], sizeof (Swap));
        CopyMem (&Log->Hotspot[Other], &Swap, sizeof (Swap));
      }
    }
  }

  // 排完序後，依新順序重新給名次：1 = 最久
  for (Index = 0; Index < Log->Count; Index++) {
    Log->Hotspot[Index].Rank = (UINT32)(Index + 1);
  }
}

// ---------------------------------------------------------------------
// 函式：FillPhaseFallback
// 用途：「備案」── 取不到細部效能資料時，用本驅動自己記的大階段時間做排行榜。
// 輸出：整個 Log 被清空並重新填好（OUT：函式負責把結果寫進呼叫者的 struct）。
// 內容：
//   - 若 EndOfDxe 有發生：列出 Entry→EndOfDxe 與 EndOfDxe→ReadyToBoot 兩段
//   - 沒發生：列出 NOT_REACHED 標記，再列 Entry→ReadyToBoot 整段
//   - ExitBootServices 此時一定還沒發生，所以誠實標成 NOT_REACHED（耗時 0），不亂編數字
// ---------------------------------------------------------------------
STATIC
VOID
FillPhaseFallback (
  OUT BOOT_TIME_HOTSPOT_LOG  *Log
  )
{
  UINT64  ReadyTick;

  // 三元運算子：若已經記過 ReadyToBoot 的 tick 就用它；否則用「現在」的讀數
  ReadyTick = (mReadyToBootTick != 0) ? mReadyToBootTick : GetPerformanceCounter ();
  ZeroMem (Log, sizeof (*Log));  // 整個 struct 清 0（Count 也變 0）
  Log->Signature       = BOOT_TIME_HOTSPOT_SIGNATURE;           // 簽名：讓讀取端確認是我們的資料
  Log->Version         = BOOT_TIME_HOTSPOT_VERSION;             // 版本
  Log->Source          = BOOT_TIME_HOTSPOT_SOURCE_PHASE_FALLBACK; // 標記「資料來源 = 備案」
  Log->GaugeEntryCount = 0;                                     // 沒有細部資料，所以是 0 筆

  if (mEndOfDxeTick != 0) {
    // EndOfDxe 有量到：分成兩段
    AddPhase (Log, "PHASE", "Entry->EndOfDxe", GetDurationNsSafe (mEntryTick, mEndOfDxeTick));
    AddPhase (Log, "PHASE", "EndOfDxe->ReadyToBoot", GetDurationNsSafe (mEndOfDxeTick, ReadyTick));
  } else {
    // EndOfDxe 沒量到：標記「沒到達」，並改用整段 Entry→ReadyToBoot
    AddPhase (Log, "NOT_REACHED", "Entry->EndOfDxe", 0);
    AddPhase (Log, "PHASE", "Entry->ReadyToBoot", GetDurationNsSafe (mEntryTick, ReadyTick));
  }

  //
  // This log is written at ReadyToBoot. ExitBootServices has not run yet,
  // so record that explicitly instead of inventing a duration.
  //
  // 說明：本記錄在 ReadyToBoot 時寫入，此時 ExitBootServices 還沒發生，
  // 所以明確標成「未到達」，而不是編造一個耗時。
  AddPhase (Log, "NOT_REACHED", "ExitBootServices", 0);
  SortPhaseDesc (Log);  // 依耗時由大到小排序並重編名次
}

// ---------------------------------------------------------------------
// 函式：CollectGauge
// 用途：從 PerformanceEx Protocol 逐筆撈出「計時紀錄（gauge）」，
//       算出耗時後存進候選池 Pool（陣列）。
// 參數：
//   Perf ：Protocol 指標；裡面的 GetGaugeEx 是「函式指標」，
//          用法像 Perf->GetGaugeEx(...)，與 C 的 callback 結構體成員一樣
//   Pool ：OUT，呼叫者準備好的候選陣列（本函式往裡面填）
//   Cap  ：Pool 最多能放幾筆（容量上限）
// 回傳：實際放進 Pool 的筆數
// 迴圈：Key 從 0 開始一路加 1，向 Protocol 要第 Key 筆；
//       要不到（回傳錯誤）就表示已經沒資料，break 離開。
// ---------------------------------------------------------------------
STATIC
UINTN
CollectGauge (
  IN  PERFORMANCE_EX_PROTOCOL  *Perf,
  OUT HOTSPOT_CANDIDATE        *Pool,
  IN  UINTN                    Cap
  )
{
  UINTN  Key;    // 要第幾筆紀錄
  UINTN  Count;  // 已放進 Pool 的筆數

  Count = 0;
  for (Key = 0; Count < Cap; Key++) {
    EFI_STATUS           Status;
    GAUGE_DATA_ENTRY_EX  *Entry;   // 指向 Protocol 內部資料的指標（我們只讀，不要 free）
    UINT64               DurationNs;

    // 取第 Key 筆；Entry 是「指標的指標」的用法：傳 &Entry 讓函式把位址寫進 Entry
    Status = Perf->GetGaugeEx (Key, &Entry);
    if (EFI_ERROR (Status)) {
      break;  // 沒有更多資料了
    }

    // 用「結束時間戳 - 開始時間戳」算耗時（單位已換成奈秒）
    DurationNs = GetDurationNsSafe (Entry->StartTimeStamp, Entry->EndTimeStamp);
    if (DurationNs == 0) {
      continue;  // 耗時 0 或無效的項目沒有參考價值，跳過，看下一筆
    }

    // 把這筆資料複製到 Pool[Count]
    ZeroMem (&Pool[Count], sizeof (Pool[Count]));
    Pool[Count].DurationNs = DurationNs;
    CopyMem (Pool[Count].Module, Entry->Module, sizeof (Entry->Module));
    CopyMem (Pool[Count].Token, Entry->Token, sizeof (Entry->Token));
    // 強制在字串長度位置補結尾字元 '\0'，確保它一定是合法的 C 字串（防止原資料沒結尾）
    Pool[Count].Module[DXE_PERFORMANCE_STRING_LENGTH] = '\0';
    Pool[Count].Token[DXE_PERFORMANCE_STRING_LENGTH]  = '\0';

    // 名稱是空字串（第一個字元就是 '\0'）→ 填 "UNKNOWN"，避免輸出空白
    if (Pool[Count].Module[0] == '\0') {
      AsciiStrCpyS (Pool[Count].Module, sizeof (Pool[Count].Module), "UNKNOWN");
    }

    if (Pool[Count].Token[0] == '\0') {
      AsciiStrCpyS (Pool[Count].Token, sizeof (Pool[Count].Token), "UNKNOWN");
    }

    Count++;  // 成功收進 1 筆
  }

  return Count;
}

// ---------------------------------------------------------------------
// 函式：SortByDurationDesc
// 用途：把候選池 Pool 依耗時「由大到小」排序（跟上面 SortPhaseDesc 同樣的做法，
//       只是這裡操作的是 HOTSPOT_CANDIDATE 陣列）。
// 參數：Pool 陣列首位址（IN OUT：直接改原陣列）；Count 有效筆數
// ---------------------------------------------------------------------
STATIC
VOID
SortByDurationDesc (
  IN OUT HOTSPOT_CANDIDATE  *Pool,
  IN UINTN                  Count
  )
{
  UINTN  Index;
  UINTN  Other;

  for (Index = 0; Index < Count; Index++) {
    for (Other = Index + 1; Other < Count; Other++) {
      if (Pool[Other].DurationNs > Pool[Index].DurationNs) {
        HOTSPOT_CANDIDATE  Swap;   // 暫存用，交換兩筆 struct
        CopyMem (&Swap, &Pool[Index], sizeof (Swap));
        CopyMem (&Pool[Index], &Pool[Other], sizeof (Swap));
        CopyMem (&Pool[Other], &Swap, sizeof (Swap));
      }
    }
  }
}

// ---------------------------------------------------------------------
// 函式：FillFromGaugeOrFallback
// 用途：總指揮 ── 嘗試用「細部資料」做排行榜；任何一步失敗就退回備案。
// 流程（每個失敗點都呼叫 FillPhaseFallback 後 return）：
//   1. LocateProtocol：向韌體要 PerformanceEx 的介面（找不到＝韌體沒開效能記錄）
//   2. AllocateZeroPool：配置候選池記憶體（像 calloc；失敗回 NULL）
//   3. CollectGauge：把資料撈進候選池（一筆都沒有就備案）
//   4. 排序，取前 N 名寫入 Log
//   5. FreePool：釋放候選池（有 malloc 就要有 free，避免記憶體洩漏）
// 輸出：Log（OUT）
// ---------------------------------------------------------------------
STATIC
VOID
FillFromGaugeOrFallback (
  OUT BOOT_TIME_HOTSPOT_LOG  *Log
  )
{
  EFI_STATUS               Status;
  PERFORMANCE_EX_PROTOCOL  *Perf;        // Protocol 介面指標
  HOTSPOT_CANDIDATE        *Pool;        // 動態配置的候選陣列
  UINTN                    Count;        // 候選筆數
  UINTN                    RankCount;    // 實際要放進排行榜的筆數（不超過 N）
  UINTN                    Index;

  // LocateProtocol：用 GUID 找某個功能介面。
  // (VOID **)&Perf 是型別轉換：函式用 void ** 當通用的「指標的指標」，
  // 我們把 Perf（指標）的位址傳進去，讓函式把介面位址填回 Perf。
  Status = gBS->LocateProtocol (
                  &gPerformanceExProtocolGuid,
                  NULL,
                  (VOID **)&Perf
                  );
  // 找不到，或位址是 NULL → 沒有細部資料可用
  if (EFI_ERROR (Status) || (Perf == NULL)) {
    FillPhaseFallback (Log);
    return;
  }

  // AllocateZeroPool：配置並清成 0；總大小 = 單筆大小 × 800 筆
  // 註：sizeof (*Pool) 是「Pool 指向的型別」的大小，這種寫法換型別時不用改
  Pool = AllocateZeroPool (sizeof (*Pool) * HOTSPOT_POOL_MAX);
  if (Pool == NULL) {
    // 記憶體不足 → 備案
    FillPhaseFallback (Log);
    return;
  }

  Count = CollectGauge (Perf, Pool, HOTSPOT_POOL_MAX);
  if (Count == 0) {
    // 一筆都沒撈到：先釋放記憶體再用備案（不能忘記 free）
    FreePool (Pool);
    FillPhaseFallback (Log);
    return;
  }

  // 依耗時由大到小排序，這樣 Pool[0] 就是最慢的
  SortByDurationDesc (Pool, Count);

  // 開始填寫排行榜表頭
  ZeroMem (Log, sizeof (*Log));
  Log->Signature       = BOOT_TIME_HOTSPOT_SIGNATURE;
  Log->Version         = BOOT_TIME_HOTSPOT_VERSION;
  Log->Source          = BOOT_TIME_HOTSPOT_SOURCE_GAUGE;  // 標記「資料來源 = 細部效能表」
  Log->GaugeEntryCount = (UINT32)Count;                   // 原始撈到幾筆（轉成 32 位元存）

  // 取「候選筆數」與「最大名次數」較小者，等於 min(Count, MAX_RANK)
  RankCount  = (Count < BOOT_TIME_HOTSPOT_MAX_RANK) ? Count : BOOT_TIME_HOTSPOT_MAX_RANK;
  Log->Count = (UINT8)RankCount;   // 強制縮成 8 位元（因為最多 5，不會爆）
  for (Index = 0; Index < RankCount; Index++) {
    Log->Hotspot[Index].Rank       = (UINT32)(Index + 1);        // 名次 1..N
    Log->Hotspot[Index].DurationNs = Pool[Index].DurationNs;
    CopyMem (Log->Hotspot[Index].Module, Pool[Index].Module, sizeof (Pool[Index].Module));
    CopyMem (Log->Hotspot[Index].Token, Pool[Index].Token, sizeof (Pool[Index].Token));
  }

  FreePool (Pool);  // 釋放候選池，避免洩漏
}

/*
  找到量測 Protocol；找不到回 NULL。
*/
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

// ---------------------------------------------------------------------
// 函式：EnsureSummaryFallback
// 用途：確保量測服務／NVRAM 裡有 Summary。
//       正常情況下 Profiler 已 Publish；沒有才補一份簡化版。
// ---------------------------------------------------------------------
STATIC
VOID
EnsureSummaryFallback (
  VOID
  )
{
  EFI_STATUS                          Status;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;
  BOOT_TIME_SUMMARY                   Summary;
  UINTN                               Size;
  UINT32                              Attr;
  UINT16                              BootCurrent;
  UINT64                              NowTick;

  Meas = LocateMeasurement ();
  if ((Meas != NULL) && (Meas->GetSummary != NULL)) {
    Status = Meas->GetSummary (Meas, &Summary);
    if (!EFI_ERROR (Status)) {
      return;
    }
  }

  Size   = sizeof (Summary);
  Status = gRT->GetVariable (
                  BOOT_TIME_SUMMARY_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  &Attr,
                  &Size,
                  &Summary
                  );
  if (!EFI_ERROR (Status)) {
    return;
  }

  NowTick = GetPerformanceCounter ();
  ZeroMem (&Summary, sizeof (Summary));
  Summary.Signature = BOOT_TIME_SUMMARY_SIGNATURE;
  Summary.Version   = BOOT_TIME_SUMMARY_VERSION;
  Summary.Reserved0 = BOOT_TIME_PHASE_READY_TO_BOOT;
  if (mEndOfDxeTick != 0) {
    Summary.Reserved0 |= BOOT_TIME_PHASE_END_OF_DXE;
  }

  Summary.ReadyToBootTimeNs            = GetDurationNsSafe (mEntryTick, NowTick);
  Summary.DeltaEntryToEndOfDxeNs       = GetDurationNsSafe (mEntryTick, mEndOfDxeTick);
  Summary.DeltaEndOfDxeToReadyToBootNs = (mEndOfDxeTick != 0) ?
                                         GetDurationNsSafe (mEndOfDxeTick, NowTick) :
                                         Summary.ReadyToBootTimeNs;

  Size   = sizeof (BootCurrent);
  Status = gRT->GetVariable (
                  L"BootCurrent",
                  &gEfiGlobalVariableGuid,
                  &Attr,
                  &Size,
                  &BootCurrent
                  );
  if (!EFI_ERROR (Status) && (Size == sizeof (BootCurrent))) {
    Summary.LastBootCurrent = BootCurrent;
  }

  if ((Meas != NULL) && (Meas->PublishSummary != NULL)) {
    Status = Meas->PublishSummary (Meas, &Summary);
    DEBUG ((DEBUG_INFO, "BootTimeHotspotDxe: Summary fallback Publish -> %r\n", Status));
    return;
  }

  Status = gRT->SetVariable (
                  BOOT_TIME_SUMMARY_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE |
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (Summary),
                  &Summary
                  );
  DEBUG ((DEBUG_INFO, "BootTimeHotspotDxe: Summary fallback SetVariable(%s) -> %r\n", BOOT_TIME_SUMMARY_VAR_NAME, Status));
}

// ---------------------------------------------------------------------
// 函式：OnEndOfDxe（事件通知函式 = callback）
// 何時被呼叫：韌體宣告 DXE 階段結束時，由韌體自己呼叫。
// 工作：只記下「這時候的 tick」。其餘事情留到 ReadyToBoot 再一次做完。
// 參數（韌體固定傳入）：Event 觸發的事件；Context 我們登記時附帶的資料（這裡是 NULL，沒用到）
// STATIC + EFIAPI：僅本檔可見；使用 UEFI 呼叫慣例（callback 一定要寫）
// ---------------------------------------------------------------------
STATIC
VOID
EFIAPI
OnEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mEndOfDxeTick = GetPerformanceCounter ();  // 讀硬體計數器 = 按下碼表
}

// ---------------------------------------------------------------------
// 函式：OnReadyToBoot（事件通知函式）
// 何時被呼叫：韌體即將啟動開機選項之前。
// 工作（只有「功能開關」打開時才做）：
//   1. 記下 ReadyToBoot 的 tick
//   2. EnsureSummaryFallback：確保 NVRAM 有摘要資料
//   3. FillFromGaugeOrFallback：產生排行榜（細部資料或備案）
//   4. PublishHotspotLog：交給量測 Protocol（即時副本 + NVRAM）
// ---------------------------------------------------------------------
STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                          Status;
  BOOT_TIME_HOTSPOT_LOG               Log;
  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *Meas;

  //
  // Policy is the A/B arm switch and must actually change behavior.
  // Hotspot is the fine-grained collector enable. Collection runs only when both are TRUE.
  //
  if (!FixedPcdGetBool (PcdBootTimeOptEnablePolicy) ||
      !FixedPcdGetBool (PcdBootTimeOptEnableHotspot))
  {
    return;
  }

  mReadyToBootTick = GetPerformanceCounter ();
  EnsureSummaryFallback ();
  FillFromGaugeOrFallback (&Log);

  Meas = LocateMeasurement ();
  if ((Meas != NULL) && (Meas->PublishHotspotLog != NULL)) {
    Status = Meas->PublishHotspotLog (Meas, &Log);
    DEBUG ((DEBUG_INFO, "BootTimeHotspotDxe: PublishHotspot -> %r\n", Status));
    return;
  }

  Status = gRT->SetVariable (
                  BOOT_TIME_HOTSPOT_LOG_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE |
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (Log),
                  &Log
                  );
  DEBUG ((DEBUG_INFO, "BootTimeHotspotDxe: SetVariable(%s) -> %r\n", BOOT_TIME_HOTSPOT_LOG_VAR_NAME, Status));
}

// ---------------------------------------------------------------------
// 函式：BootTimeHotspotDxeEntryPoint ── 本驅動的「main()」
// 何時被呼叫：韌體載入本驅動後，第一個呼叫的函式（名稱在 .inf 的 ENTRY_POINT 指定）。
// 參數：
//   ImageHandle ：代表本驅動映像檔的把手（像程式的身分證）
//   SystemTable ：韌體系統表指標（gBS、gRT 由函式庫自動設定，這裡不用直接碰）
// 回傳：EFI_SUCCESS 代表載入成功；錯誤碼則韌體會把驅動卸載。
// 工作：記下起跑 tick，並向韌體「登記」兩個事件：EndOfDxe、ReadyToBoot。
//       登記只是「預約」，callback 要等事件真的發生才會被呼叫。
// ---------------------------------------------------------------------
EFI_STATUS
EFIAPI
BootTimeHotspotDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  mEntryTick = GetPerformanceCounter ();   // 起跑，記下 tick
  // CreateEventEx 參數依序：
  //   EVT_NOTIFY_SIGNAL ：事件被發出信號時，執行通知函式
  //   TPL_CALLBACK      ：通知函式執行時的優先權等級（類似中斷優先權，CALLBACK 為一般等級）
  //   OnEndOfDxe        ：通知函式（函式名稱本身就是函式指標，跟 C 的 callback 一樣）
  //   NULL              ：傳給 callback 的 Context，不需要
  //   &gEfiEndOfDxeEventGroupGuid：用 GUID 指定訂閱哪一種事件
  //   &mEndOfDxeEvent   ：OUT，事件把手寫進這個全域變數
  Status     = gBS->CreateEventEx (
                      EVT_NOTIFY_SIGNAL,
                      TPL_CALLBACK,
                      OnEndOfDxe,
                      NULL,
                      &gEfiEndOfDxeEventGroupGuid,
                      &mEndOfDxeEvent
                      );
  // 登記失敗就把錯誤碼回傳給韌體（EFI_ERROR 判斷是否為錯誤）
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // 同樣方式登記 ReadyToBoot 事件；直接把結果當作回傳值（成功或失敗都交給韌體處理）
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );
  return Status;
}

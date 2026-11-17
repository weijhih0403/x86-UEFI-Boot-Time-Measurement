/*
  =====================================================================
  BootTimeProfilerDxe.c ── 開機「碼表」驅動程式（教學註解版）
  =====================================================================

  【這個檔案在做什麼？】
  想像你在跑 100 公尺，手上拿著碼表，在三個「檢查點」按下計時：
    1. 起跑   ：本驅動被韌體載入、開始執行（Entry）
    2. 檢查點 A：EndOfDxe        （DXE 階段結束，驅動大多載入完畢）
    3. 檢查點 B：ReadyToBoot     （準備啟動作業系統／應用程式）
    4. 終點   ：ExitBootServices （韌體把控制權交給 OS，Boot Services 即將消失）

  每到一個檢查點，我們就讀一次硬體計時器（得到 tick），
  最後把「各段花了幾奈秒」算出來，寫進 NVRAM。

  【名詞對照（給只學過 C 的你）】
  - DXE driver：UEFI 韌體啟動時載入的「小程式」，沒有 main()，
                改由「進入點函式」（下面的 BootTimeProfilerDxeEntryPoint）開始。
  - NVRAM / UEFI Variable：韌體裡「關機後還在」的小倉庫，
                可以想成「用名字存取的檔案」或「很小的永久全域變數」。
                SetVariable = 寫入；GetVariable = 讀出。
  - tick：硬體計時器的「滴答數」，還不是秒；要經 GetTimeInNanoSecond 換算成奈秒。
  - Event（事件）：類似「訂閱通知」。我們先登記「某事發生時，請呼叫我的函式」，
                韌體到時就會幫我們呼叫（類似 C 的 callback / 函式指標）。
  - EFIAPI：UEFI 規定的「呼叫慣例」標記，讓韌體與我們的函式在
            參數怎麼傳遞上格式一致。你只要知道：callback 與進入點都要寫上它。
  - STATIC：等同 C 的 static。在函式前 = 只有本 .c 看得到；
            在全域變數前 = 本檔內全域、生命期為整個程式。
  - IN / OUT：只是「說明用的空巨集」，編譯後等於什麼都沒有。
              IN = 傳進來給函式用；OUT = 函式會寫回結果給呼叫者。
  - gRT / gBS：兩個全域「指標」，分別指向 Runtime Services 與 Boot Services
              （韌體提供的函式表，像 C 標準函式庫，但由韌體實作）。
  - UINT64 / UINT16 / CHAR8 / VOID / BOOLEAN：UEFI 自訂的固定寬度型別，
              意義分別是 64 位元無號整數、16 位元無號整數、8 位元字元、void、布林。

  【資料怎麼流動？】
    本驅動安裝 BootTimeMeasurement Protocol（同開機「量測服務」）
           ├─ 記憶體即時副本 ← Publish／Get（Dashboard／BdsProbe／Dump 可 Locate）
           └─ 順便 SetVariable → NVRAM 快照（給跨重開／主機腳本）
    BootTimeDump.efi 優先走 Protocol，找不到再 GetVariable。

  SPDX 授權行：本檔原本沒有，這裡不新增（授權標示請依專案統一規定）。
  =====================================================================
*/

// ---------------------------------------------------------------------
// 標頭檔（#include）：就像 C 的 #include <stdio.h>，這裡引入 UEFI 的定義
// ---------------------------------------------------------------------
#include <Uefi.h>                          // UEFI 基本型別（EFI_STATUS、EFI_EVENT…）

#include <Guid/EventGroup.h>               // 事件群組 GUID：EndOfDxe、ExitBootServices 等
#include <Guid/GlobalVariable.h>           // UEFI 標準全域變數的 GUID（例如 BootCurrent 所屬）
#include <Guid/BootTimeOptPkgVariable.h>   // 本專案自訂：NVRAM 變數名稱與 struct 定義
#include <Protocol/BootTimeMeasurement.h>  // 同開機量測服務 Protocol

#include <Library/BaseMemoryLib.h>         // ZeroMem 等（類似 memset）
#include <Library/BootTimeBdsProbeLib.h>   // 本專案自訂：BDS 階段細分計時的輔助函式庫
#include <Library/DebugLib.h>              // DEBUG()：除錯訊息（類似 printf 到 debug 輸出）
#include <Library/PrintLib.h>              // AsciiSPrint：類似 sprintf
#include <Library/SerialPortLib.h>         // SerialPortWrite：直接寫序列埠
#include <Library/TimerLib.h>              // GetPerformanceCounter、GetTimeInNanoSecond
#include <Library/UefiBootServicesTableLib.h>     // 提供全域指標 gBS
#include <Library/UefiRuntimeServicesTableLib.h>  // 提供全域指標 gRT

// ---------------------------------------------------------------------
// 全域變數區（都是 STATIC：只有這個 .c 檔看得到）
// ---------------------------------------------------------------------

// 階段事件 handle
// EFI_EVENT 可想成「事件的把手（ID）」，CreateEventEx 會把它填好，
// 之後若要關閉／查詢事件就靠它。這裡存起來但沒再用到。
STATIC EFI_EVENT  mEndOfDxeEvent          = NULL;  // EndOfDxe 事件把手
STATIC EFI_EVENT  mReadyToBootEvent       = NULL;  // ReadyToBoot 事件把手
STATIC EFI_EVENT  mExitBootServicesEvent  = NULL;  // ExitBootServices 事件把手

// 各階段的效能計數器讀數（tick）
// 值為 0 代表「還沒走到那個階段」（所以後面算差時會特別檢查 0）。
STATIC UINT64     mEntryTick              = 0;  // 驅動進入點被執行時的 tick（起跑點）
STATIC UINT64     mEndOfDxeTick           = 0;  // EndOfDxe 發生時的 tick
STATIC UINT64     mReadyToBootTick        = 0;  // ReadyToBoot 發生時的 tick
STATIC UINT64     mExitBootServicesTick   = 0;  // ExitBootServices 發生時的 tick
STATIC UINT16     mPhaseMask              = 0;  // 哪些階段已到達
                                                // 這是「位元旗標」：每個 bit 代表一個階段，
                                                // 例如 bit0 = EndOfDxe 到了（見 .h 的 BOOT_TIME_PHASE_*）

// 量測服務：記憶體即時副本（同開機 LocateProtocol 可讀）
STATIC BOOLEAN                 mHasSummary  = FALSE;
STATIC BOOLEAN                 mHasBds      = FALSE;
STATIC BOOLEAN                 mHasHotspot  = FALSE;
STATIC BOOT_TIME_SUMMARY       mLiveSummary;
STATIC BOOT_TIME_BDS_BREAKDOWN mLiveBds;
STATIC BOOT_TIME_HOTSPOT_LOG   mLiveHotspot;
STATIC EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  mMeasurementProtocol;
STATIC EFI_HANDLE                          mMeasurementHandle = NULL;

// ---------------------------------------------------------------------
// 函式：TicksToNanoSeconds
// 用途：把「tick 差」換成「奈秒」。
// 為什麼要換？tick 的單位取決於 CPU/硬體頻率，直接比較沒有意義；
//       奈秒（1 秒 = 10^9 奈秒）才是人類可讀、跨平台一致的單位。
// 參數：DeltaTicks —— 兩次讀數相減得到的 tick 差值（IN：只讀不改）
// 回傳：對應的奈秒數
// ---------------------------------------------------------------------

// tick 差 → 奈秒
STATIC
UINT64
TicksToNanoSeconds (
  IN UINT64  DeltaTicks
  )
{
  // 交給 TimerLib 的 GetTimeInNanoSecond 去換算
  return GetTimeInNanoSecond (DeltaTicks);
}

// ---------------------------------------------------------------------
// 函式：GetDeltaNsSafe
// 用途：「安全地」算出 Start 到 End 之間花了幾奈秒。
// 「安全」是指：如果資料不合理，就回傳 0，而不是算出奇怪的數字。
//   - StartTick == 0 或 EndTick == 0 → 代表那個階段根本還沒發生
//   - EndTick < StartTick            → 時間倒退？一定有問題
//     （UINT64 是無號數，若直接相減會「下溢」變成超大正數，所以要先擋掉）
// 參數：StartTick 起點 tick；EndTick 終點 tick
// 回傳：間隔奈秒；無效時為 0
// ---------------------------------------------------------------------

// 起訖無效則回 0，否則回間隔（ns）
STATIC
UINT64
GetDeltaNsSafe (
  IN UINT64  StartTick,
  IN UINT64  EndTick
  )
{
  // 任一端為 0（沒量到）或終點比起點小（異常）→ 回報 0
  if ((StartTick == 0) || (EndTick == 0) || (EndTick < StartTick)) {
    return 0;
  }

  // 正常情況：先做 tick 相減，再換成奈秒
  return TicksToNanoSeconds (EndTick - StartTick);
}

// ---------------------------------------------------------------------
// 函式：ReadBootCurrent
// 用途：讀 UEFI 標準全域變數 "BootCurrent"（這次開機用的是 Boot000x 的哪個編號）。
// 重點：這裡示範「用指標回傳多個結果」──
//       C 函式只能 return 一個值，所以用 OUT 指標參數帶出第二個結果。
//       BootCurrent：讀到的編號寫到 *BootCurrent
//       Valid      ：有沒有真的讀成功寫到 *Valid
// 讀不到時：*Valid = FALSE，*BootCurrent = 0。
// ---------------------------------------------------------------------

// 讀 UEFI 全域變數 BootCurrent
STATIC
VOID
ReadBootCurrent (
  OUT UINT16  *BootCurrent,   // 呼叫者傳入變數的位址，我們把答案寫進去
  OUT BOOLEAN *Valid          // 同上：回報「讀取是否成功」
  )
{
  EFI_STATUS  Status;      // UEFI 函式的回傳碼（EFI_SUCCESS 代表成功，錯誤用 EFI_ERROR() 判斷）
  UINTN       DataSize;    // 資料大小（位元組）。GetVariable 會用它進出：傳入緩衝區大小、傳出實際大小
  UINT32      Attributes;  // 變數屬性（是否永久、可否在 OS 讀…）；這裡不關心，但函式要求要給
  UINT16      Value;       // 暫存讀到的值

  // 先假設失敗，成功再改（防禦式寫法）
  *Valid       = FALSE;
  *BootCurrent = 0;

  DataSize = sizeof (Value);  // 告訴韌體：我的緩衝區有 2 位元組
  // gRT->GetVariable：從 NVRAM 讀一筆變數。
  // 「名稱 + GUID」合在一起才是唯一鑰匙（GUID 像是變數的「擁有者／命名空間」）。
  // L"..." 是寬字元字串（Unicode），UEFI 變數名稱都用這種。
  Status   = gRT->GetVariable (
                    L"BootCurrent",             // 變數名稱
                    &gEfiGlobalVariableGuid,    // 命名空間 GUID（取位址，所以是指標）
                    &Attributes,                // OUT：屬性
                    &DataSize,                  // IN/OUT：緩衝區大小／實際大小
                    &Value                      // OUT：資料寫到這裡
                    );
  // 成功「而且」實際大小剛好是 2 位元組，才當作有效
  if (!EFI_ERROR (Status) && (DataSize == sizeof (Value))) {
    *BootCurrent = Value;
    *Valid       = TRUE;
  }
}

// ---------------------------------------------------------------------
// BootTimeMeasurement Protocol 實作（本驅動當「量測服務櫃檯」）
// Get*：回傳記憶體即時副本；Publish*：更新副本並寫 NVRAM 快照。
// ---------------------------------------------------------------------

STATIC
EFI_STATUS
WriteSummaryNvram (
  IN BOOT_TIME_SUMMARY  *Summary
  )
{
  return gRT->SetVariable (
                BOOT_TIME_SUMMARY_VAR_NAME,
                &gBootTimeOptPkgVariableGuid,
                EFI_VARIABLE_NON_VOLATILE |
                EFI_VARIABLE_BOOTSERVICE_ACCESS |
                EFI_VARIABLE_RUNTIME_ACCESS,
                sizeof (*Summary),
                Summary
                );
}

STATIC
EFI_STATUS
WriteBdsNvram (
  IN BOOT_TIME_BDS_BREAKDOWN  *Breakdown
  )
{
  return gRT->SetVariable (
                BOOT_TIME_BDS_BREAKDOWN_VAR_NAME,
                &gBootTimeOptPkgVariableGuid,
                EFI_VARIABLE_NON_VOLATILE |
                EFI_VARIABLE_BOOTSERVICE_ACCESS |
                EFI_VARIABLE_RUNTIME_ACCESS,
                sizeof (*Breakdown),
                Breakdown
                );
}

STATIC
EFI_STATUS
WriteHotspotNvram (
  IN BOOT_TIME_HOTSPOT_LOG  *Log
  )
{
  return gRT->SetVariable (
                BOOT_TIME_HOTSPOT_LOG_VAR_NAME,
                &gBootTimeOptPkgVariableGuid,
                EFI_VARIABLE_NON_VOLATILE |
                EFI_VARIABLE_BOOTSERVICE_ACCESS |
                EFI_VARIABLE_RUNTIME_ACCESS,
                sizeof (*Log),
                Log
                );
}

STATIC
EFI_STATUS
EFIAPI
BootTimeMeasurementGetSummary (
  IN  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  OUT BOOT_TIME_SUMMARY                   *Summary
  )
{
  if ((This == NULL) || (Summary == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  if (!mHasSummary) {
    return EFI_NOT_FOUND;
  }
  CopyMem (Summary, &mLiveSummary, sizeof (*Summary));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
BootTimeMeasurementPublishSummary (
  IN EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  IN BOOT_TIME_SUMMARY                   *Summary
  )
{
  EFI_STATUS  Status;

  if ((This == NULL) || (Summary == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (&mLiveSummary, Summary, sizeof (mLiveSummary));
  mHasSummary = TRUE;
  Status = WriteSummaryNvram (&mLiveSummary);
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: PublishSummary -> %r\n", Status));
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
BootTimeMeasurementGetBdsBreakdown (
  IN  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  OUT BOOT_TIME_BDS_BREAKDOWN             *Breakdown
  )
{
  if ((This == NULL) || (Breakdown == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  if (!mHasBds) {
    return EFI_NOT_FOUND;
  }
  CopyMem (Breakdown, &mLiveBds, sizeof (*Breakdown));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
BootTimeMeasurementPublishBdsBreakdown (
  IN EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  IN BOOT_TIME_BDS_BREAKDOWN             *Breakdown
  )
{
  EFI_STATUS  Status;

  if ((This == NULL) || (Breakdown == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (&mLiveBds, Breakdown, sizeof (mLiveBds));
  mHasBds = TRUE;
  Status = WriteBdsNvram (&mLiveBds);
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: PublishBds -> %r\n", Status));
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
BootTimeMeasurementGetHotspotLog (
  IN  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  OUT BOOT_TIME_HOTSPOT_LOG               *Log
  )
{
  if ((This == NULL) || (Log == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  if (!mHasHotspot) {
    return EFI_NOT_FOUND;
  }
  CopyMem (Log, &mLiveHotspot, sizeof (*Log));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
BootTimeMeasurementPublishHotspotLog (
  IN EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  IN BOOT_TIME_HOTSPOT_LOG               *Log
  )
{
  EFI_STATUS  Status;

  if ((This == NULL) || (Log == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (&mLiveHotspot, Log, sizeof (mLiveHotspot));
  mHasHotspot = TRUE;
  Status = WriteHotspotNvram (&mLiveHotspot);
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: PublishHotspot -> %r\n", Status));
  return Status;
}

// ---------------------------------------------------------------------
// 函式：PersistSummary
// 用途：把目前量到的所有階段時間整理成 BOOT_TIME_SUMMARY，
//       經 Protocol Publish（記憶體即時副本 + NVRAM 快照）。
// ---------------------------------------------------------------------

STATIC
VOID
PersistSummary (
  VOID
  )
{
  EFI_STATUS         Status;
  BOOT_TIME_SUMMARY  Summary;
  UINT16             BootCurrent;
  BOOLEAN            BootCurrentValid;

  ZeroMem (&Summary, sizeof (Summary));

  Summary.Signature              = BOOT_TIME_SUMMARY_SIGNATURE;
  Summary.Version                = BOOT_TIME_SUMMARY_VERSION;
  Summary.Reserved0              = mPhaseMask;
  Summary.EntryTimeNs            = 0;
  Summary.EndOfDxeTimeNs         = GetDeltaNsSafe (mEntryTick, mEndOfDxeTick);
  Summary.ReadyToBootTimeNs      = GetDeltaNsSafe (mEntryTick, mReadyToBootTick);
  Summary.ExitBootServicesTimeNs = GetDeltaNsSafe (mEntryTick, mExitBootServicesTick);

  Summary.DeltaEntryToEndOfDxeNs = Summary.EndOfDxeTimeNs;
  Summary.DeltaEndOfDxeToReadyToBootNs =
    GetDeltaNsSafe (mEndOfDxeTick, mReadyToBootTick);
  Summary.DeltaReadyToBootToExitBootServicesNs =
    GetDeltaNsSafe (mReadyToBootTick, mExitBootServicesTick);
  Summary.DeltaEntryToExitBootServicesNs =
    Summary.ExitBootServicesTimeNs;

  ReadBootCurrent (&BootCurrent, &BootCurrentValid);
  if (BootCurrentValid) {
    Summary.LastBootCurrent = BootCurrent;
  }

  // 經 Publish：更新服務記憶體 + 寫 NVRAM（不經 Locate，直接呼叫本驅動實作）
  Status = BootTimeMeasurementPublishSummary (&mMeasurementProtocol, &Summary);
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: PersistSummary -> %r\n", Status));
}

// ---------------------------------------------------------------------
// 函式：SaveLastSuccessfulBootOption
// 用途：把這次的 BootCurrent（開機選項編號）另存一份到我們自己的變數
//       "LastSuccessfulBootOption"，方便之後分析「上次是從哪個選項開機成功」。
// 失敗處理：讀不到 BootCurrent 就直接 return，不寫入（寧可不存也不存錯）。
// ---------------------------------------------------------------------

// 把目前 BootCurrent 另存為 LastBootOption
STATIC
VOID
SaveLastSuccessfulBootOption (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       DataSize;
  UINT32      Attributes;
  UINT16      BootCurrent;

  // 先從 UEFI 標準變數讀出 BootCurrent（流程同 ReadBootCurrent，只是這裡直接內嵌寫出）
  DataSize = sizeof (BootCurrent);
  Status   = gRT->GetVariable (
                    L"BootCurrent",
                    &gEfiGlobalVariableGuid,
                    &Attributes,
                    &DataSize,
                    &BootCurrent
                    );
  // 讀取失敗，或大小不是預期的 2 位元組 → 記錄訊息後離開
  if (EFI_ERROR (Status) || (DataSize != sizeof (BootCurrent))) {
    DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: BootCurrent unavailable, skip save (%r)\n", Status));
    return;
  }

  // 寫進 NVRAM（屬性同上：永久 + 開機服務可讀 + 執行期可讀）
  Status = gRT->SetVariable (
                  BOOT_TIME_LAST_BOOT_OPTION_VAR_NAME,
                  &gBootTimeOptPkgVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE |
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (BootCurrent),
                  &BootCurrent
                  );
  // %04x：印成 4 位十六進位並補 0，例如 Boot0001
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: Saved %s=Boot%04x (%r)\n", BOOT_TIME_LAST_BOOT_OPTION_VAR_NAME, BootCurrent, Status));
}

// ---------------------------------------------------------------------
// 函式：OnEndOfDxe（事件通知函式，也就是 callback）
// 何時被呼叫：韌體宣告「DXE 階段結束」時，由韌體自己呼叫，不是我們主動呼叫。
// 工作：按下碼表（記 tick）→ 設定旗標 → 印除錯訊息 → 更新 NVRAM 摘要。
// 參數（韌體固定傳入，格式不能改）：
//   Event   ：觸發的事件把手（本函式沒用到）
//   Context ：我們登記事件時附帶的自訂資料（登記時傳 NULL，所以沒用到）
// 註：函式名稱前的 STATIC 表示僅本檔可見；EFIAPI 是呼叫慣例，callback 必須帶。
// ---------------------------------------------------------------------

// EndOfDxe：記 tick、更新摘要
STATIC
VOID
EFIAPI
OnEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mEndOfDxeTick = GetPerformanceCounter ();      // 讀目前硬體計數器 = 按下碼表
  mPhaseMask   |= BOOT_TIME_PHASE_END_OF_DXE;    // 用「OR」把 bit0 設成 1：標記這階段發生過
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: EndOfDxe at %Lu ns from entry\n", TicksToNanoSeconds (mEndOfDxeTick - mEntryTick)));
  PersistSummary ();  // 先存一次，就算後面開機卡住，這階段的資料也不會遺失
}

// ---------------------------------------------------------------------
// 函式：OnReadyToBoot（事件通知函式）
// 何時被呼叫：韌體即將啟動開機選項（OS loader 或 Shell 應用程式）之前。
// 工作：
//   1. 記下 ReadyToBoot 的 tick、設旗標
//   2. BootTimeBdsOnReadyToBoot()：通知 BDS 細分計時函式庫「到這點了」
//      （它會把 EndOfDxe→ReadyToBoot 這大段再切細並存起來）
//   3. 存「最後成功開機選項」
//   4. 更新摘要
// ExitBootServices 此時還沒發生，所以那欄位仍為 0（GetDeltaNsSafe 會處理）。
// ---------------------------------------------------------------------

// ReadyToBoot：記 tick；EBS 未到則保持 0
STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mReadyToBootTick = GetPerformanceCounter ();     // 按下碼表
  mPhaseMask      |= BOOT_TIME_PHASE_READY_TO_BOOT; // bit1：ReadyToBoot 發生過
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: ReadyToBoot at %Lu ns from entry\n", TicksToNanoSeconds (mReadyToBootTick - mEntryTick)));

  BootTimeBdsOnReadyToBoot ();      // 呼叫別的函式庫：記錄 BDS 細分資料
  SaveLastSuccessfulBootOption ();  // 另存開機選項編號
  PersistSummary ();                // 更新 NVRAM 摘要
}

// ---------------------------------------------------------------------
// 函式：OnExitBootServices（事件通知函式）
// 何時被呼叫：有人（例如 BootTimeDump 或 OS loader）呼叫 ExitBootServices() 時。
// ExitBootServices 是什麼？韌體宣告「開機服務結束」：
//   之後記憶體配置、Print、事件…等 Boot Services 都不能再用，
//   就像「搭火車時下車，車站（韌體）的設施不再提供服務」。
// 因此本函式很特別：
//   - 仍可用 Runtime Services（SetVariable），所以還是能更新 NVRAM
//   - 但不要用 Print（會透過 Boot Services 的 Console，不安全）
//     → 改用 SerialPortWrite 直接寫序列埠（硬體 I/O，不靠 Boot Services）
// ---------------------------------------------------------------------
STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  CHAR8   Line[128];   // 暫存要輸出的一行字串（窄字元 char 陣列，在堆疊上）
  UINTN   Len;         // 字串長度（位元組）
  UINT64  ReadyToEbsNs;   // ReadyToBoot → ExitBootServices 的奈秒數（EBS = ExitBootServices 縮寫）
  UINT64  EntryToEbsNs;   // Entry → ExitBootServices 的總奈秒數

  mExitBootServicesTick = GetPerformanceCounter ();           // 終點，按下碼表
  mPhaseMask           |= BOOT_TIME_PHASE_EXIT_BOOT_SERVICES; // bit2：EBS 發生過

  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: ExitBootServices at %Lu ns from entry\n", TicksToNanoSeconds (mExitBootServicesTick - mEntryTick)));

  SaveLastSuccessfulBootOption ();  // 再存一次（此階段仍可呼叫 Runtime Services）
  PersistSummary ();                // 這次摘要最完整：四個時間點都有了

  // EBS 時 Print 不安全，改串列輸出最終 CSV
  // 先把兩個要輸出的間隔算好
  ReadyToEbsNs = GetDeltaNsSafe (mReadyToBootTick, mExitBootServicesTick);
  EntryToEbsNs = GetDeltaNsSafe (mEntryTick, mExitBootServicesTick);

  // AsciiSPrint：類似 sprintf，把格式化結果寫進 Line，回傳寫了幾個字元。
  // %Lu = 64 位元無號整數（UINT64 專用的格式）。
  // SerialPortWrite：把位元組直接丟到序列埠。(UINT8 *)Line 是「型別轉換」：
  //   把 char 指標當成 byte 指標，因為函式要求 UINT8 *，記憶體內容並未改變。
  Len = AsciiSPrint (Line, sizeof (Line), "summary,ReadyToBootToExitBootServicesNs,%Lu\n", ReadyToEbsNs);
  SerialPortWrite ((UINT8 *)Line, Len);
  Len = AsciiSPrint (Line, sizeof (Line), "summary,EntryToExitBootServicesNs,%Lu\n", EntryToEbsNs);
  SerialPortWrite ((UINT8 *)Line, Len);
  Len = AsciiSPrint (Line, sizeof (Line), "summary,ExitBootServicesReached,1\n");
  SerialPortWrite ((UINT8 *)Line, Len);

  // 最後再用 DEBUG 輸出一行完整摘要（DEBUG 不一定走 Boot Services，通常也導向序列埠）
  DEBUG ((
    DEBUG_INFO,
    "BootTimeProfilerDxe: Summary(ns): E->EndDxe=%Lu, EndDxe->Ready=%Lu, Ready->EBS=%Lu, E->EBS=%Lu\n",
    TicksToNanoSeconds (mEndOfDxeTick - mEntryTick),
    TicksToNanoSeconds (mReadyToBootTick - mEndOfDxeTick),
    ReadyToEbsNs,
    EntryToEbsNs
    ));
}

// ---------------------------------------------------------------------
// 函式：BootTimeProfilerDxeEntryPoint ── 本驅動的「main()」
// 用途：驅動被載入後，韌體第一個呼叫的函式（名稱在 .inf 的 ENTRY_POINT 指定）。
// 參數：
//   ImageHandle ：代表「本驅動映像檔」的把手（可想成這支程式的身分證）
//   SystemTable ：韌體系統表指標（gBS、gRT 等就是由它取得；函式庫已幫我們處理好）
// 回傳：EFI_SUCCESS 表示載入成功；若回傳錯誤，韌體會把驅動卸載。
// 流程：
//   1. 記下起跑 tick，並把所有狀態清成 0
//   2. 立刻寫一筆初始資料到 NVRAM（讓 Dump 確定驅動有被載入）
//   3. 用 CreateEventEx 登記三個階段 callback（只登記，不會立刻執行）
// ---------------------------------------------------------------------

// 驅動進入點：記 entry、先寫一筆摘要、註冊三個階段事件
EFI_STATUS
EFIAPI
BootTimeProfilerDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  // 起跑：記下 tick，並把其他狀態歸零（重新初始化，保險起見）
  mEntryTick            = GetPerformanceCounter ();
  mEndOfDxeTick         = 0;
  mReadyToBootTick      = 0;
  mExitBootServicesTick = 0;
  mPhaseMask            = 0;
  mHasSummary           = FALSE;
  mHasBds               = FALSE;
  mHasHotspot           = FALSE;
  ZeroMem (&mLiveSummary, sizeof (mLiveSummary));
  ZeroMem (&mLiveBds, sizeof (mLiveBds));
  ZeroMem (&mLiveHotspot, sizeof (mLiveHotspot));
  DEBUG ((DEBUG_INFO, "BootTimeProfilerDxe: Entry\n"));

  // 安裝量測 Protocol：之後 BdsProbe／Hotspot／Dashboard／Dump 可用 LocateProtocol
  ZeroMem (&mMeasurementProtocol, sizeof (mMeasurementProtocol));
  mMeasurementProtocol.Revision            = EFI_BOOT_TIME_MEASUREMENT_PROTOCOL_REVISION;
  mMeasurementProtocol.GetSummary          = BootTimeMeasurementGetSummary;
  mMeasurementProtocol.PublishSummary      = BootTimeMeasurementPublishSummary;
  mMeasurementProtocol.GetBdsBreakdown     = BootTimeMeasurementGetBdsBreakdown;
  mMeasurementProtocol.PublishBdsBreakdown = BootTimeMeasurementPublishBdsBreakdown;
  mMeasurementProtocol.GetHotspotLog       = BootTimeMeasurementGetHotspotLog;
  mMeasurementProtocol.PublishHotspotLog   = BootTimeMeasurementPublishHotspotLog;
  mMeasurementHandle = ImageHandle;
  Status = gBS->InstallProtocolInterface (
                  &mMeasurementHandle,
                  &gEfiBootTimeMeasurementProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mMeasurementProtocol
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "BootTimeProfilerDxe: InstallProtocol failed %r\n", Status));
    return Status;
  }

  // 立刻寫一筆，方便 Dump 確認驅動有載入
  SaveLastSuccessfulBootOption ();
  PersistSummary ();

  // gBS->CreateEventEx：登記「某事件發生時請呼叫我的函式」。
  // 參數依序：
  //   EVT_NOTIFY_SIGNAL：事件被「發出信號」時，執行通知函式
  //   TPL_CALLBACK     ：通知函式執行時的優先權等級（TPL，類似中斷優先權；
  //                      CALLBACK 是一般等級，較不會卡住系統其他工作）
  //   OnEndOfDxe       ：通知函式（函式名稱 = 函式指標，跟 C 的 callback 一樣）
  //   NULL             ：傳給 callback 的 Context，這裡不需要
  //   &gEfiEndOfDxeEventGroupGuid：要訂閱「哪一種」事件（用 GUID 指定 EndOfDxe）
  //   &mEndOfDxeEvent  ：OUT，把事件把手填進這個全域變數
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnEndOfDxe,
                  NULL,
                  &gEfiEndOfDxeEventGroupGuid,
                  &mEndOfDxeEvent
                  );
  // EFI_ERROR(Status)：判斷是否為錯誤碼。失敗就直接回傳，讓韌體知道載入失敗
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // 同樣方式登記 ReadyToBoot 事件
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // 同樣方式登記 ExitBootServices 事件
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // 三個事件都登記好了。進入點結束，但驅動「留在記憶體」，之後韌體會自動呼叫上面三個 callback。
  return EFI_SUCCESS;
}

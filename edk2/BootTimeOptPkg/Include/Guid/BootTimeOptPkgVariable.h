#ifndef BOOT_TIME_OPT_PKG_VARIABLE_H_
#define BOOT_TIME_OPT_PKG_VARIABLE_H_

/*
  =====================================================================
  這份檔案在做什麼？（給只會 C 的讀者）
  ---------------------------------------------------------------------
  想像開機過程要「考試計時」：誰在哪個階段花了多久。
  計時結果不能只放在一般變數裡（重開機就沒了），要存到韌體的
  「關機後還在」的小倉庫，術語叫 NVRAM／UEFI Variable。

  這份 .h 不寫「怎麼計時」的程式，只定義：
    1. 倉庫裡每個資料的「檔名」（字串常數）
    2. 每筆資料長什麼樣（struct，跟 C 的 struct 一樣）
    3. 一些旗標數字（哪個階段有發生）

  真正寫入／讀取的程式在別的 .c（例如 BootTimeBdsProbeLib.c）。
  =====================================================================
*/

#include <Uefi.h>
#include <Base.h>

/*
  ----- 資料「檔名」（寬字元字串，前面的 L 表示 Unicode）-----
  之後 SetVariable / GetVariable 會用「檔名 + GUID」當成唯一鑰匙。
  類比：開檔要用路徑；這裡用 名稱 + 擁有者 GUID。
*/
#define BOOT_TIME_SUMMARY_VAR_NAME            L"BootTimeSummary"
#define BOOT_TIME_LAST_BOOT_OPTION_VAR_NAME   L"LastSuccessfulBootOption"
#define BOOT_TIME_HOTSPOT_LOG_VAR_NAME        L"BootTimeHotspotLog"
#define BOOT_TIME_BDS_BREAKDOWN_VAR_NAME      L"BootTimeBdsBreakdown"

/*
  ----- 簽名與版本 -----
  Signature：四個字元組成的魔術數字，讀回來先檢查「是不是我們的資料」。
  Version：結構若以後加欄位，可靠版本判斷舊資料要不要丟棄。
  SIGNATURE_32('B','T','B','D') 類似把 "BTBD" 編成一個 UINT32。
*/
#define BOOT_TIME_SUMMARY_SIGNATURE  SIGNATURE_32('B','T','O','1')
#define BOOT_TIME_SUMMARY_VERSION    2
#define BOOT_TIME_HOTSPOT_SIGNATURE  SIGNATURE_32('B','T','H','S')
#define BOOT_TIME_HOTSPOT_VERSION    1
#define BOOT_TIME_HOTSPOT_MAX_RANK   5   /* Hotspot 最多排名前幾名 */
#define BOOT_TIME_BDS_BREAKDOWN_SIGNATURE  SIGNATURE_32('B','T','B','D')
#define BOOT_TIME_BDS_BREAKDOWN_VERSION    1

/*
  ----- 大階段有沒有「發生過」-----
  存在 BOOT_TIME_SUMMARY 的 Reserved0 裡，用「位元旗標」：
    一個 UINT16 的某幾位，每位代表一件事是否發生。

  為什麼需要？因為時間欄位是 0 有兩種意思：
    A) 真的量到 0（幾乎不可能）
    B) 根本還沒走到那個階段（沒量到）
  有設旗標才代表「有走到」。
*/
#define BOOT_TIME_PHASE_END_OF_DXE          0x0001  /* bit0：EndOfDxe 發生了 */
#define BOOT_TIME_PHASE_READY_TO_BOOT       0x0002  /* bit1：ReadyToBoot 發生了 */
#define BOOT_TIME_PHASE_EXIT_BOOT_SERVICES  0x0004  /* bit2：ExitBootServices 發生了 */

/* Hotspot 資料來源：1=只有大階段；2=有較細的效能表 */
#define BOOT_TIME_HOTSPOT_SOURCE_PHASE_FALLBACK  1
#define BOOT_TIME_HOTSPOT_SOURCE_GAUGE           2

/*
  ---------------------------------------------------------------------
  BOOT_TIME_SUMMARY：開機「大區間」時間表
  ---------------------------------------------------------------------
  單位多半是奈秒（ns）。1 毫秒 = 1,000,000 奈秒。

  時間軸（簡化）：
    Profiler 載入 → EndOfDxe → ReadyToBoot → ExitBootServices
*/
typedef struct {
  UINT32  Signature;   /* 應等於 BOOT_TIME_SUMMARY_SIGNATURE */
  UINT16  Version;     /* 應等於 BOOT_TIME_SUMMARY_VERSION */
  UINT16  Reserved0;   /* 階段旗標：上面的 BOOT_TIME_PHASE_* 用 | 組合 */

  /* 從entry point算到該事件的累積時間（實作細節見 Profiler） */
  UINT64  EntryTimeNs;
  UINT64  EndOfDxeTimeNs;
  UINT64  ReadyToBootTimeNs;
  UINT64  ExitBootServicesTimeNs;

  /* 比較好讀的「兩點之間差多久」 */
  UINT64  DeltaEntryToEndOfDxeNs;                 /* 載入 → EndOfDxe */
  UINT64  DeltaEndOfDxeToReadyToBootNs;           /* EndOfDxe → ReadyToBoot（常是最長的一段） */
  UINT64  DeltaReadyToBootToExitBootServicesNs;   /* ReadyToBoot → ExitBootServices */
  UINT64  DeltaEntryToExitBootServicesNs;         /* 載入 → ExitBootServices（總長） */

  UINT16  LastBootCurrent;  /* 這次用哪個開機選項編號（Boot000x） */
  UINT16  Reserved1;        /* 對齊／保留，暫時沒用 */
} BOOT_TIME_SUMMARY;

/*
  ---------------------------------------------------------------------
  BOOT_TIME_HOTSPOT_*：誰比較「慢」的排行榜（可選）
  ---------------------------------------------------------------------
  一列 = 一個項目：名稱 + 花多久。
*/
typedef struct {
  UINT32  Rank;          /* 名次：1 最久 */
  UINT32  Reserved;
  UINT64  DurationNs;    /* 這個項目花多少奈秒 */
  CHAR8   Module[32];    /* 模組名（窄字元字串，類似 char[]） */
  CHAR8   Token[32];     /* 標記／說明字串 */
} BOOT_TIME_HOTSPOT_ENTRY;

typedef struct {
  UINT32                  Signature;
  UINT16                  Version;
  UINT8                   Source;           /* PHASE_FALLBACK 或 GAUGE */
  UINT8                   Count;            /* Hotspot[] 裡有效幾筆 */
  UINT32                  GaugeEntryCount;  /* 掃到幾筆原始資料（若有） */
  UINT32                  Reserved;
  BOOT_TIME_HOTSPOT_ENTRY Hotspot[BOOT_TIME_HOTSPOT_MAX_RANK];
} BOOT_TIME_HOTSPOT_LOG;

/*
  ---------------------------------------------------------------------
  BOOT_TIME_BDS_BREAKDOWN：把「EndOfDxe → ReadyToBoot」再切細
  ---------------------------------------------------------------------
  為什麼要切？大區間可能七百多毫秒，但不知道是：
    - 連線裝置慢？
    - 刷新開機選項慢？
    - 還是中間其他事慢？

  時間軸：
    EndOfDxe
       |  PreConnectNs
       v
    Connect 開始 ──ConnectAllNs──> Connect 結束
       |  RefreshBootOptionsNs
       v
    Refresh 結束
       |  PostRefreshToReadyToBootNs
       v
    ReadyToBoot
       |  LoadPathNs（這段屬於「之後」載入 Dump，不是上面那四大段）
       v
    Dump 程式開始執行

  Flags：跟 PHASE 一樣是位元旗標，某位有設 = 那段有量到。
*/
#define BOOT_TIME_BDS_FLAG_END_OF_DXE              0x0001
#define BOOT_TIME_BDS_FLAG_CONNECT                 0x0002
#define BOOT_TIME_BDS_FLAG_REFRESH                 0x0004
#define BOOT_TIME_BDS_FLAG_READY_TO_BOOT           0x0008
#define BOOT_TIME_BDS_FLAG_LOAD_PATH               0x0010

typedef struct {
  UINT32  Signature;
  UINT16  Version;
  UINT16  Flags;   /* BOOT_TIME_BDS_FLAG_* 的組合 */

  /* ----- 已經換算好的時間（奈秒），給人／CSV 直接看 ----- */
  UINT64  PreConnectNs;                 /* EndOfDxe到開始連線裝置 */
  UINT64  ConnectAllNs;                 /* 連線裝置本身 */
  UINT64  RefreshBootOptionsNs;         /* 刷新開機選項清單 */
  UINT64  PostRefreshToReadyToBootNs;   /* 刷新完到ReadyToBoot */
  UINT64  LoadPathNs;                   /* ReadyToBoot到Dump進入應用程式*/

  /*
    ----- 原始計數器讀數（tick）-----
    硬體計時器的目前讀數，還不是秒／奈秒。
    為什麼要存 tick？因為 UEFI 裡「記時間的驅動」和「印 CSV 的 Dump」
    是不同的 .efi 程式，記憶體裡的全域變數互不相通。
    把 tick 存進 NVRAM，另一支程式才能拿出來繼續算差。
  */
  UINT64  EndOfDxePerfTick;
  UINT64  ConnectStartPerfTick;
  UINT64  ConnectEndPerfTick;
  UINT64  RefreshEndPerfTick;
  UINT64  ReadyToBootPerfTick;
} BOOT_TIME_BDS_BREAKDOWN;

/*
  這個 GUID 的「數值」定義在 BootTimeOptPkg.dec；
  這裡只宣告：別的 .c 會用到這個全域符號。
  類比：extern 告訴編譯器「別處有定義這個變數」。
*/
extern EFI_GUID  gBootTimeOptPkgVariableGuid;

#endif

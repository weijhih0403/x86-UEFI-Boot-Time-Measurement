/** @file
  BDS 碼表工具的「函式目錄」（只有宣告，沒有實作）。

  實作在 Library/BootTimeBdsProbeLib/BootTimeBdsProbeLib.c。
  其他模組 #include 本檔後，就可以呼叫下面這些函式。

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef BOOT_TIME_BDS_PROBE_LIB_H_
#define BOOT_TIME_BDS_PROBE_LIB_H_

/** EndOfDxe 送出後呼叫：細分量測的起點。 */
VOID
EFIAPI
BootTimeBdsMarkEndOfDxe (VOID);

/** 連線裝置開始前呼叫。 */
VOID
EFIAPI
BootTimeBdsBeginConnect (
  VOID
  );

/** 連線裝置結束後呼叫。 */
VOID
EFIAPI
BootTimeBdsEndConnect (
  VOID
  );

/** 刷新開機選項開始前呼叫。 */
VOID
EFIAPI
BootTimeBdsBeginRefresh (
  VOID
  );

/** 刷新開機選項結束後呼叫。 */
VOID
EFIAPI
BootTimeBdsEndRefresh (
  VOID
  );

/** ReadyToBoot 時呼叫：補「刷新完 → ReadyToBoot」這段。 */
VOID
EFIAPI
BootTimeBdsOnReadyToBoot (
  VOID
  );

/** Dump 程式一進入就呼叫：補「ReadyToBoot → Dump 進入」這段。 */
VOID
EFIAPI
BootTimeBdsMarkLoadPathAtAppEntry (
  VOID
  );

#endif

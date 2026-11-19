/** @file
  BDS 碼表工具的函式目錄（只有宣告）。

  實作在Library/BootTimeBdsProbeLib/BootTimeBdsProbeLib.c。
  其他模組 #include 本檔後，就可以呼叫下面這些函式。

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef BOOT_TIME_BDS_PROBE_LIB_H_
#define BOOT_TIME_BDS_PROBE_LIB_H_

/** EndOfDxe 送出後呼叫：細分測量的起點。 */
VOID
EFIAPI
BootTimeBdsMarkEndOfDxe (VOID);

/**
  BeforeConsole 結束時呼叫：ConsolePrepNs = EndOfDxe → 現在。
**/
VOID
EFIAPI
BootTimeBdsMarkConsolePrepDone (
  VOID
  );

/** 包住一段 PreConnect 工作的開始（與 End 成對）。 */
VOID
EFIAPI
BootTimeBdsBeginPreConnectWork (
  VOID
  );

/**
  包住一段 PreConnect 工作的結束。
  Step：BOOT_TIME_BDS_PRE_LOGO／PCI_ACPI／BOOT_ORDER。
**/
VOID
EFIAPI
BootTimeBdsEndPreConnectWork (
  IN UINT32  Step
  );

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

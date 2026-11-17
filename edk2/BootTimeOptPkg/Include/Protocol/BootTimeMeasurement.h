/** @file
  BootTimeMeasurement Protocol（開機期間的量測服務介面）

  【為什麼要有這個 Protocol？】
  以前各模組各自 SetVariable／GetVariable 同一個 NVRAM「信箱」：
    - 同一次開機裡，跨 .efi 要交換最新量測，只能反覆讀寫 Variable
    - Dashboard 無法清楚分辨「記憶體裡這次開機的即時資料」與「信箱殘留」

  Protocol = 開機期間還活著的「電話線／服務櫃檯」：
    Publish*：寫入端把最新結構交給服務（服務會更新記憶體，並順便寫 NVRAM 快照）
    Get*    ：同開機內的讀取端（Dashboard／Dump／BdsProbe）直接問服務

  NVRAM Variable 仍保留：給跨重開、主機腳本、ExitBootServices 之後的輸出通道。
  ExitBootServices 之後 Boot Services（含 LocateProtocol）不可再用，所以自動測試
  的最終 CSV 仍以 Variable／序列埠為準。

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef BOOT_TIME_MEASUREMENT_H_
#define BOOT_TIME_MEASUREMENT_H_

#include <Uefi.h>
#include <Guid/BootTimeOptPkgVariable.h>

#define EFI_BOOT_TIME_MEASUREMENT_PROTOCOL_REVISION  0x00010000

typedef struct _EFI_BOOT_TIME_MEASUREMENT_PROTOCOL EFI_BOOT_TIME_MEASUREMENT_PROTOCOL;

/**
  讀取目前記憶體中的 Summary（這次開機即時資料）。
  尚未 Publish 過則回 EFI_NOT_FOUND。
**/
typedef
EFI_STATUS
(EFIAPI *EFI_BOOT_TIME_MEASUREMENT_GET_SUMMARY)(
  IN  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  OUT BOOT_TIME_SUMMARY                   *Summary
  );

/**
  發布 Summary：更新服務內部記憶體，並寫入 NVRAM 快照。
**/
typedef
EFI_STATUS
(EFIAPI *EFI_BOOT_TIME_MEASUREMENT_PUBLISH_SUMMARY)(
  IN EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  IN BOOT_TIME_SUMMARY                   *Summary
  );

typedef
EFI_STATUS
(EFIAPI *EFI_BOOT_TIME_MEASUREMENT_GET_BDS_BREAKDOWN)(
  IN  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  OUT BOOT_TIME_BDS_BREAKDOWN             *Breakdown
  );

typedef
EFI_STATUS
(EFIAPI *EFI_BOOT_TIME_MEASUREMENT_PUBLISH_BDS_BREAKDOWN)(
  IN EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  IN BOOT_TIME_BDS_BREAKDOWN             *Breakdown
  );

typedef
EFI_STATUS
(EFIAPI *EFI_BOOT_TIME_MEASUREMENT_GET_HOTSPOT_LOG)(
  IN  EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  OUT BOOT_TIME_HOTSPOT_LOG               *Log
  );

typedef
EFI_STATUS
(EFIAPI *EFI_BOOT_TIME_MEASUREMENT_PUBLISH_HOTSPOT_LOG)(
  IN EFI_BOOT_TIME_MEASUREMENT_PROTOCOL  *This,
  IN BOOT_TIME_HOTSPOT_LOG               *Log
  );

struct _EFI_BOOT_TIME_MEASUREMENT_PROTOCOL {
  UINT64                                          Revision;
  EFI_BOOT_TIME_MEASUREMENT_GET_SUMMARY           GetSummary;
  EFI_BOOT_TIME_MEASUREMENT_PUBLISH_SUMMARY       PublishSummary;
  EFI_BOOT_TIME_MEASUREMENT_GET_BDS_BREAKDOWN     GetBdsBreakdown;
  EFI_BOOT_TIME_MEASUREMENT_PUBLISH_BDS_BREAKDOWN PublishBdsBreakdown;
  EFI_BOOT_TIME_MEASUREMENT_GET_HOTSPOT_LOG       GetHotspotLog;
  EFI_BOOT_TIME_MEASUREMENT_PUBLISH_HOTSPOT_LOG   PublishHotspotLog;
};

/*
  GUID 本體在 BootTimeOptPkg.dec 的 [Protocols] 宣告；
  建置系統會產生 gEfiBootTimeMeasurementProtocolGuid。
*/
extern EFI_GUID  gEfiBootTimeMeasurementProtocolGuid;

#endif

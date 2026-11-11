/** @file
  BDS checkpoint probes for EndOfDxe -> ReadyToBoot breakdown.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef BOOT_TIME_BDS_PROBE_LIB_H_
#define BOOT_TIME_BDS_PROBE_LIB_H_

/**
  Call immediately after EndOfDxe is signaled.
**/
VOID
EFIAPI
BootTimeBdsMarkEndOfDxe (
  VOID
  );

/**
  Call immediately before PlatformBdsConnectSequence / ConnectAll.
**/
VOID
EFIAPI
BootTimeBdsBeginConnect (
  VOID
  );

/**
  Call immediately after PlatformBdsConnectSequence / ConnectAll.
**/
VOID
EFIAPI
BootTimeBdsEndConnect (
  VOID
  );

/**
  Call immediately before EfiBootManagerRefreshAllBootOption.
**/
VOID
EFIAPI
BootTimeBdsBeginRefresh (
  VOID
  );

/**
  Call immediately after EfiBootManagerRefreshAllBootOption.
**/
VOID
EFIAPI
BootTimeBdsEndRefresh (
  VOID
  );

/**
  Call from ReadyToBoot notify: fills PostRefreshToReadyToBootNs and persists.
**/
VOID
EFIAPI
BootTimeBdsOnReadyToBoot (
  VOID
  );

/**
  Call at BootTimeDump entry: fills LoadPathNs (ReadyToBoot -> now) and persists.
**/
VOID
EFIAPI
BootTimeBdsMarkLoadPathAtAppEntry (
  VOID
  );

#endif

/** @file
  Keyboard input helpers (WaitForEvent, no busy polling).

  Adapted from FirmwareDashboardPkg (https://github.com/weijhih0403/FirmwareDashboardPkg).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef BD_INPUT_H_
#define BD_INPUT_H_

#include <Uefi.h>

EFI_STATUS
EFIAPI
BdWaitForKey (
  OUT EFI_INPUT_KEY  *Key
  );

VOID
EFIAPI
BdFlushKeys (
  VOID
  );

#endif

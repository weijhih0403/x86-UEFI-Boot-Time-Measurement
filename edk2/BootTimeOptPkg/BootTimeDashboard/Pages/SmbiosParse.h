/** @file
  Bounds-checked SMBIOS helpers.

  Adapted from FirmwareDashboardPkg.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef BD_SMBIOS_PARSE_H_
#define BD_SMBIOS_PARSE_H_

#include <Uefi.h>
#include <IndustryStandard/SmBios.h>

typedef struct {
  UINT8    *Table;
  UINTN     TableSize;
  UINT8     Major;
  UINT8     Minor;
  BOOLEAN   Is30;
} BD_SMBIOS_VIEW;

EFI_STATUS
EFIAPI
BdSmbiosLocate (
  OUT BD_SMBIOS_VIEW  *View
  );

BOOLEAN
EFIAPI
BdSmbiosNext (
  IN     CONST BD_SMBIOS_VIEW  *View,
  IN OUT UINT8                **Current
  );

CONST CHAR8 *
EFIAPI
BdSmbiosGetString (
  IN CONST SMBIOS_STRUCTURE  *Hdr,
  IN CONST BD_SMBIOS_VIEW    *View,
  IN UINT8                    StringIndex
  );

BOOLEAN
EFIAPI
BdSmbiosHasLength (
  IN CONST SMBIOS_STRUCTURE  *Hdr,
  IN CONST BD_SMBIOS_VIEW    *View,
  IN UINT8                    RequiredLength
  );

UINT64
EFIAPI
BdSmbiosMemoryDeviceBytes (
  IN CONST SMBIOS_TABLE_TYPE17  *Type17
  );

#endif

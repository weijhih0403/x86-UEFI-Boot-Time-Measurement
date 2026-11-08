/** @file
  Safe SMBIOS walking — interview talking point:
  never trust Length / string index without checking against TableSize.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "SmbiosParse.h"

#include <Guid/SmBios.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

EFI_STATUS
EFIAPI
BdSmbiosLocate (
  OUT BD_SMBIOS_VIEW  *View
  )
{
  UINTN                        Index;
  SMBIOS_TABLE_3_0_ENTRY_POINT  *Ep3;
  SMBIOS_TABLE_ENTRY_POINT      *Ep2;

  if ((View == NULL) || (gST == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (View, sizeof (*View));

  for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    if (CompareGuid (&gST->ConfigurationTable[Index].VendorGuid, &gEfiSmbios3TableGuid)) {
      Ep3 = (SMBIOS_TABLE_3_0_ENTRY_POINT *)gST->ConfigurationTable[Index].VendorTable;
      if ((Ep3 == NULL) || (Ep3->EntryPointLength < sizeof (SMBIOS_TABLE_3_0_ENTRY_POINT))) {
        continue;
      }

      if (CompareMem (Ep3->AnchorString, "_SM3_", SMBIOS_3_0_ANCHOR_STRING_LENGTH) != 0) {
        continue;
      }

      View->Table     = (UINT8 *)(UINTN)Ep3->TableAddress;
      View->TableSize = Ep3->TableMaximumSize;
      View->Major     = Ep3->MajorVersion;
      View->Minor     = Ep3->MinorVersion;
      View->Is30      = TRUE;
      if ((View->Table == NULL) || (View->TableSize == 0)) {
        return EFI_NOT_FOUND;
      }

      return EFI_SUCCESS;
    }
  }

  for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    if (CompareGuid (&gST->ConfigurationTable[Index].VendorGuid, &gEfiSmbiosTableGuid)) {
      Ep2 = (SMBIOS_TABLE_ENTRY_POINT *)gST->ConfigurationTable[Index].VendorTable;
      if ((Ep2 == NULL) || (Ep2->EntryPointLength < sizeof (SMBIOS_TABLE_ENTRY_POINT))) {
        continue;
      }

      if (CompareMem (Ep2->AnchorString, "_SM_", SMBIOS_ANCHOR_STRING_LENGTH) != 0) {
        continue;
      }

      View->Table     = (UINT8 *)(UINTN)Ep2->TableAddress;
      View->TableSize = Ep2->TableLength;
      View->Major     = Ep2->MajorVersion;
      View->Minor     = Ep2->MinorVersion;
      View->Is30      = FALSE;
      if ((View->Table == NULL) || (View->TableSize == 0)) {
        return EFI_NOT_FOUND;
      }

      return EFI_SUCCESS;
    }
  }

  return EFI_NOT_FOUND;
}

STATIC
BOOLEAN
FdPtrInTable (
  IN CONST BD_SMBIOS_VIEW  *View,
  IN CONST UINT8           *Ptr,
  IN UINTN                  Need
  )
{
  UINTN  Off;

  if ((View == NULL) || (View->Table == NULL) || (Ptr == NULL)) {
    return FALSE;
  }

  if (Ptr < View->Table) {
    return FALSE;
  }

  Off = (UINTN)(Ptr - View->Table);
  if (Off > View->TableSize) {
    return FALSE;
  }

  return (View->TableSize - Off) >= Need;
}

BOOLEAN
EFIAPI
BdSmbiosHasLength (
  IN CONST SMBIOS_STRUCTURE  *Hdr,
  IN CONST BD_SMBIOS_VIEW    *View,
  IN UINT8                    RequiredLength
  )
{
  if ((Hdr == NULL) || !FdPtrInTable (View, (CONST UINT8 *)Hdr, sizeof (SMBIOS_STRUCTURE))) {
    return FALSE;
  }

  if (Hdr->Length < RequiredLength) {
    return FALSE;
  }

  return FdPtrInTable (View, (CONST UINT8 *)Hdr, Hdr->Length);
}

CONST CHAR8 *
EFIAPI
BdSmbiosGetString (
  IN CONST SMBIOS_STRUCTURE  *Hdr,
  IN CONST BD_SMBIOS_VIEW    *View,
  IN UINT8                    StringIndex
  )
{
  CONST UINT8  *End;
  CONST CHAR8  *Cursor;
  UINT8         Current;

  if ((StringIndex == 0) || (Hdr == NULL) || (View == NULL)) {
    return NULL;
  }

  if (!BdSmbiosHasLength (Hdr, View, Hdr->Length)) {
    return NULL;
  }

  End    = View->Table + View->TableSize;
  Cursor = (CONST CHAR8 *)((CONST UINT8 *)Hdr + Hdr->Length);
  if ((CONST UINT8 *)Cursor >= End) {
    return NULL;
  }

  //
  // No strings at all: double NUL immediately.
  //
  if ((Cursor[0] == '\0') && (((CONST UINT8 *)Cursor + 1) < End) && (Cursor[1] == '\0')) {
    return NULL;
  }

  Current = 1;
  while ((CONST UINT8 *)Cursor < End) {
    if (Current == StringIndex) {
      //
      // Ensure the whole C string stays inside the table.
      //
      {
        CONST CHAR8  *Scan;

        for (Scan = Cursor; (CONST UINT8 *)Scan < End; Scan++) {
          if (*Scan == '\0') {
            return Cursor;
          }
        }
      }
      return NULL;
    }

    while (((CONST UINT8 *)Cursor < End) && (*Cursor != '\0')) {
      Cursor++;
    }

    if ((CONST UINT8 *)Cursor >= End) {
      return NULL;
    }

    Cursor++;
    if ((CONST UINT8 *)Cursor >= End) {
      return NULL;
    }

    if (*Cursor == '\0') {
      return NULL;
    }

    Current++;
  }

  return NULL;
}

BOOLEAN
EFIAPI
BdSmbiosNext (
  IN     CONST BD_SMBIOS_VIEW  *View,
  IN OUT UINT8                **Current
  )
{
  SMBIOS_STRUCTURE  *Hdr;
  UINT8             *Cursor;
  UINT8             *End;

  if ((View == NULL) || (Current == NULL) || (*Current == NULL)) {
    return FALSE;
  }

  Hdr = (SMBIOS_STRUCTURE *)*Current;
  if (!BdSmbiosHasLength (Hdr, View, sizeof (SMBIOS_STRUCTURE))) {
    return FALSE;
  }

  if (Hdr->Type == 127) {
    return FALSE;
  }

  if (Hdr->Length < sizeof (SMBIOS_STRUCTURE)) {
    return FALSE;
  }

  End    = View->Table + View->TableSize;
  Cursor = *Current + Hdr->Length;

  while ((Cursor + 1) < End) {
    if ((Cursor[0] == 0) && (Cursor[1] == 0)) {
      *Current = Cursor + 2;
      if (*Current >= End) {
        return FALSE;
      }

      if (!FdPtrInTable (View, *Current, sizeof (SMBIOS_STRUCTURE))) {
        return FALSE;
      }

      return TRUE;
    }

    Cursor++;
  }

  return FALSE;
}

UINT64
EFIAPI
BdSmbiosMemoryDeviceBytes (
  IN CONST SMBIOS_TABLE_TYPE17  *Type17
  )
{
  UINT64  Mega;

  if (Type17 == NULL) {
    return 0;
  }

  //
  // Size == 0  -> not installed
  // Size == 0xFFFF -> unknown
  //
  if ((Type17->Size == 0) || (Type17->Size == 0xFFFF)) {
    return 0;
  }

  if (Type17->Size == 0x7FFF) {
    //
    // ExtendedSize is in MiB when Size is the special 0x7FFF sentinel.
    // Only use it if the structure is long enough (SMBIOS 2.7+).
    //
    if (Type17->Hdr.Length < (OFFSET_OF (SMBIOS_TABLE_TYPE17, ExtendedSize) + sizeof (UINT32))) {
      return 0;
    }

    Mega = Type17->ExtendedSize;
  } else if ((Type17->Size & BIT15) != 0) {
    //
    // Bit15 set: value in KiB.
    //
    return ((UINT64)(Type17->Size & 0x7FFF)) * 1024ULL;
  } else {
    Mega = Type17->Size;
  }

  return Mega * 1024ULL * 1024ULL;
}

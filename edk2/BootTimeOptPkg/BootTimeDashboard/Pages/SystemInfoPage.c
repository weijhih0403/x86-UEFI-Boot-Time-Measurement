/** @file
  System Information page (firmware / RTC / SMBIOS).

  Adapted from FirmwareDashboardPkg. QEMU shows guest firmware inventory,
  not host hardware.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "SystemInfoPage.h"
#include "SmbiosParse.h"

#include <IndustryStandard/SmBios.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "Ui/TextUi.h"

STATIC
VOID
BdAddAsciiOrUnavailable (
  IN OUT BD_PAGE_BUFFER  *Buf,
  IN     CONST CHAR16    *Label,
  IN     CONST CHAR8     *Ascii
  )
{
  if ((Ascii == NULL) || (Ascii[0] == '\0')) {
    BdPageBufferAdd (Buf, L"  %-22s unavailable", Label);
  } else {
    BdPageBufferAdd (Buf, L"  %-22s %a", Label, Ascii);
  }
}

STATIC
VOID
BdFillFirmware (
  IN OUT BD_PAGE_BUFFER  *Buf
  )
{
  BdPageBufferAdd (Buf, L"[ Firmware / UEFI ]");
  BdPageBufferAdd (Buf, L"  NOTE: Under QEMU this is guest OVMF info, not the host PC.");

  if ((gST == NULL) || (gST->FirmwareVendor == NULL)) {
    BdPageBufferAdd (Buf, L"  FirmwareVendor        unavailable");
  } else {
    BdPageBufferAdd (Buf, L"  FirmwareVendor        %s", gST->FirmwareVendor);
  }

  if (gST != NULL) {
    BdPageBufferAdd (Buf, L"  FirmwareRevision      0x%08x", gST->FirmwareRevision);
    BdPageBufferAdd (
      Buf,
      L"  UEFI Revision         %d.%02d",
      (UINTN)(gST->Hdr.Revision >> 16),
      (UINTN)(gST->Hdr.Revision & 0xFFFF)
      );
  } else {
    BdPageBufferAdd (Buf, L"  FirmwareRevision      unavailable");
    BdPageBufferAdd (Buf, L"  UEFI Revision         unavailable");
  }
}

STATIC
VOID
BdFillRtc (
  IN OUT BD_PAGE_BUFFER  *Buf
  )
{
  EFI_STATUS  Status;
  EFI_TIME    Time;

  BdPageBufferAdd (Buf, L"");
  BdPageBufferAdd (Buf, L"[ Real-Time Clock  gRT->GetTime() ]");
  if (gRT == NULL) {
    BdPageBufferAdd (Buf, L"  Current time          unavailable");
    return;
  }

  Status = gRT->GetTime (&Time, NULL);
  if (EFI_ERROR (Status)) {
    BdPageBufferAdd (Buf, L"  Current time          unavailable (%r)", Status);
    return;
  }

  BdPageBufferAdd (
    Buf,
    L"  Current time          %04d-%02d-%02d %02d:%02d:%02d",
    Time.Year,
    Time.Month,
    Time.Day,
    Time.Hour,
    Time.Minute,
    Time.Second
    );
}

STATIC
VOID
BdFillSmbios (
  IN OUT BD_PAGE_BUFFER  *Buf
  )
{
  EFI_STATUS            Status;
  BD_SMBIOS_VIEW        View;
  UINT8                *Cursor;
  SMBIOS_STRUCTURE     *Hdr;
  SMBIOS_TABLE_TYPE0   *Type0;
  SMBIOS_TABLE_TYPE1   *Type1;
  SMBIOS_TABLE_TYPE4   *Type4;
  SMBIOS_TABLE_TYPE17  *Type17;
  UINTN                 ProcShown;
  UINTN                 MemShown;
  UINT64                SmbiosBytes;
  UINT64                DeviceBytes;

  BdPageBufferAdd (Buf, L"");
  BdPageBufferAdd (Buf, L"[ SMBIOS inventory ]");
  Status = BdSmbiosLocate (&View);
  if (EFI_ERROR (Status)) {
    BdPageBufferAdd (Buf, L"  Table                 unavailable (%r)", Status);
    return;
  }

  BdPageBufferAdd (
    Buf,
    L"  Spec                  %u.%u (%a)",
    View.Major,
    View.Minor,
    View.Is30 ? "3.x entry" : "2.x entry"
    );

  Cursor      = View.Table;
  ProcShown   = 0;
  MemShown    = 0;
  SmbiosBytes = 0;

  for ( ; ;) {
    Hdr = (SMBIOS_STRUCTURE *)Cursor;
    if (!BdSmbiosHasLength (Hdr, &View, sizeof (SMBIOS_STRUCTURE))) {
      break;
    }

    if (Hdr->Type == SMBIOS_TYPE_BIOS_INFORMATION) {
      Type0 = (SMBIOS_TABLE_TYPE0 *)Hdr;
      if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE0, BiosCharacteristics))) {
        BdPageBufferAdd (Buf, L"  -- BIOS (Type 0) --");
        BdAddAsciiOrUnavailable (Buf, L"Vendor", BdSmbiosGetString (Hdr, &View, Type0->Vendor));
        BdAddAsciiOrUnavailable (Buf, L"Version", BdSmbiosGetString (Hdr, &View, Type0->BiosVersion));
        BdAddAsciiOrUnavailable (Buf, L"Release Date", BdSmbiosGetString (Hdr, &View, Type0->BiosReleaseDate));
      }
    } else if (Hdr->Type == SMBIOS_TYPE_SYSTEM_INFORMATION) {
      Type1 = (SMBIOS_TABLE_TYPE1 *)Hdr;
      if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE1, Uuid))) {
        BdPageBufferAdd (Buf, L"  -- System (Type 1) --");
        BdAddAsciiOrUnavailable (Buf, L"Manufacturer", BdSmbiosGetString (Hdr, &View, Type1->Manufacturer));
        BdAddAsciiOrUnavailable (Buf, L"Product Name", BdSmbiosGetString (Hdr, &View, Type1->ProductName));
        BdAddAsciiOrUnavailable (Buf, L"Version", BdSmbiosGetString (Hdr, &View, Type1->Version));
        BdAddAsciiOrUnavailable (Buf, L"Serial Number", BdSmbiosGetString (Hdr, &View, Type1->SerialNumber));
      }
    } else if (Hdr->Type == SMBIOS_TYPE_PROCESSOR_INFORMATION) {
      Type4 = (SMBIOS_TABLE_TYPE4 *)Hdr;
      if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE4, CurrentSpeed) + sizeof (UINT16)) &&
          (ProcShown < 2))
      {
        BdPageBufferAdd (Buf, L"  -- Processor (Type 4) #%u --", (UINT32)(ProcShown + 1));
        BdAddAsciiOrUnavailable (Buf, L"Socket", BdSmbiosGetString (Hdr, &View, Type4->Socket));
        BdAddAsciiOrUnavailable (Buf, L"Manufacturer", BdSmbiosGetString (Hdr, &View, Type4->ProcessorManufacturer));
        BdAddAsciiOrUnavailable (Buf, L"Version", BdSmbiosGetString (Hdr, &View, Type4->ProcessorVersion));
        BdPageBufferAdd (
          Buf,
          L"  %-22s %u MHz (max %u)",
          L"Current Speed",
          Type4->CurrentSpeed,
          Type4->MaxSpeed
          );
        if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE4, CoreCount) + sizeof (UINT8))) {
          BdPageBufferAdd (
            Buf,
            L"  %-22s cores=%u enabled=%u threads=%u",
            L"Topology",
            Type4->CoreCount,
            Type4->EnabledCoreCount,
            Type4->ThreadCount
            );
        }

        ProcShown++;
      }
    } else if (Hdr->Type == SMBIOS_TYPE_MEMORY_DEVICE) {
      Type17 = (SMBIOS_TABLE_TYPE17 *)Hdr;
      if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE17, Size) + sizeof (UINT16))) {
        DeviceBytes = BdSmbiosMemoryDeviceBytes (Type17);
        if (DeviceBytes > 0) {
          SmbiosBytes += DeviceBytes;
          if (MemShown < 4) {
            BdPageBufferAdd (Buf, L"  -- Memory Device (Type 17) #%u --", (UINT32)(MemShown + 1));
            BdAddAsciiOrUnavailable (Buf, L"Locator", BdSmbiosGetString (Hdr, &View, Type17->DeviceLocator));
            BdAddAsciiOrUnavailable (Buf, L"Manufacturer", BdSmbiosGetString (Hdr, &View, Type17->Manufacturer));
            BdAddAsciiOrUnavailable (Buf, L"Part Number", BdSmbiosGetString (Hdr, &View, Type17->PartNumber));
            if (BdSmbiosHasLength (Hdr, &View, OFFSET_OF (SMBIOS_TABLE_TYPE17, Speed) + sizeof (UINT16)) &&
                (Type17->Speed != 0))
            {
              BdPageBufferAdd (
                Buf,
                L"  %-22s %u MiB @ %u MT/s",
                L"Size",
                (UINT32)(DeviceBytes / (1024ULL * 1024ULL)),
                Type17->Speed
                );
            } else {
              BdPageBufferAdd (
                Buf,
                L"  %-22s %u MiB",
                L"Size",
                (UINT32)(DeviceBytes / (1024ULL * 1024ULL))
                );
            }
          }

          MemShown++;
        }
      }
    }

    if (!BdSmbiosNext (&View, &Cursor)) {
      break;
    }
  }

  if (MemShown == 0) {
    BdPageBufferAdd (Buf, L"  Memory Device         unavailable / empty");
  } else {
    BdPageBufferAdd (
      Buf,
      L"  SMBIOS DIMM total     %u MiB  (%u populated module(s))",
      (UINT32)(SmbiosBytes / (1024ULL * 1024ULL)),
      (UINT32)MemShown
      );
    BdPageBufferAdd (Buf, L"  NOTE: Physical inventory from Type 17; not UEFI Memory Map.");
  }
}

EFI_STATUS
EFIAPI
BdSystemInfoPage (
  VOID
  )
{
  BD_PAGE_BUFFER  Buf;

  BdPageBufferReset (&Buf);
  BdFillFirmware (&Buf);
  BdFillRtc (&Buf);
  BdFillSmbios (&Buf);
  return BdShowScrollablePage (L"System Information", &Buf);
}

/** @file
  GetMemoryMap snapshot while this application is running.

  Adapted from FirmwareDashboardPkg SystemInfo memory-map block.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "MemoryMapPage.h"

#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "Ui/TextUi.h"

EFI_STATUS
EFIAPI
BdMemoryMapPage (
  VOID
  )
{
  BD_PAGE_BUFFER         Buf;
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVer;
  UINTN                  Index;
  UINTN                  Count;
  UINTN                  Retry;
  UINT64                 Conventional;
  UINT64                 BootServices;
  UINT64                 Runtime;
  UINT64                 Reserved;
  UINT64                 Acpi;
  UINT64                 Other;
  UINT64                 Pages;

  BdPageBufferReset (&Buf);
  BdPageBufferAdd (&Buf, L"[ UEFI Memory Map  GetMemoryMap() ]");
  BdPageBufferAdd (&Buf, L"  Snapshot while BootTimeDashboard is running.");
  BdPageBufferAdd (&Buf, L"  NOT the final map after ExitBootServices.");
  BdPageBufferAdd (&Buf, L"  SMBIOS Type17 capacity != these category totals.");
  BdPageBufferAdd (&Buf, L"");

  if (gBS == NULL) {
    BdPageBufferAdd (&Buf, L"  Memory map            unavailable");
    return BdShowScrollablePage (L"Memory Map", &Buf);
  }

  Map     = NULL;
  MapSize = 0;
  Status  = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVer);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    BdPageBufferAdd (&Buf, L"  Memory map            unavailable (%r)", Status);
    return BdShowScrollablePage (L"Memory Map", &Buf);
  }

  for (Retry = 0; Retry < 6; Retry++) {
    MapSize += DescSize * 16;
    if (Map != NULL) {
      FreePool (Map);
      Map = NULL;
    }

    Map = AllocatePool (MapSize);
    if (Map == NULL) {
      BdPageBufferAdd (&Buf, L"  Memory map            unavailable (OOM)");
      return BdShowScrollablePage (L"Memory Map", &Buf);
    }

    Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (!EFI_ERROR (Status)) {
      break;
    }

    if (Status != EFI_BUFFER_TOO_SMALL) {
      BdPageBufferAdd (&Buf, L"  Memory map            unavailable (%r)", Status);
      FreePool (Map);
      return BdShowScrollablePage (L"Memory Map", &Buf);
    }
  }

  if (EFI_ERROR (Status) || (DescSize == 0)) {
    BdPageBufferAdd (&Buf, L"  Memory map            unavailable (%r)", Status);
    if (Map != NULL) {
      FreePool (Map);
    }

    return BdShowScrollablePage (L"Memory Map", &Buf);
  }

  Conventional = 0;
  BootServices = 0;
  Runtime      = 0;
  Reserved     = 0;
  Acpi         = 0;
  Other        = 0;
  Count        = MapSize / DescSize;

  for (Index = 0; Index < Count; Index++) {
    Desc  = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescSize);
    Pages = Desc->NumberOfPages;
    switch (Desc->Type) {
      case EfiConventionalMemory:
        Conventional += Pages;
        break;
      case EfiBootServicesCode:
      case EfiBootServicesData:
        BootServices += Pages;
        break;
      case EfiRuntimeServicesCode:
      case EfiRuntimeServicesData:
        Runtime += Pages;
        break;
      case EfiReservedMemoryType:
        Reserved += Pages;
        break;
      case EfiACPIReclaimMemory:
      case EfiACPIMemoryNVS:
        Acpi += Pages;
        break;
      default:
        Other += Pages;
        break;
    }
  }

  BdPageBufferAdd (&Buf, L"  Descriptors           %u", (UINT32)Count);
  BdPageBufferAdd (&Buf, L"  Conventional Memory   %u MiB", (UINT32)((Conventional * EFI_PAGE_SIZE) / (1024 * 1024)));
  BdPageBufferAdd (&Buf, L"  Boot Services Memory  %u MiB", (UINT32)((BootServices * EFI_PAGE_SIZE) / (1024 * 1024)));
  BdPageBufferAdd (&Buf, L"  Runtime Services Mem  %u MiB", (UINT32)((Runtime * EFI_PAGE_SIZE) / (1024 * 1024)));
  BdPageBufferAdd (&Buf, L"  Reserved Memory       %u MiB", (UINT32)((Reserved * EFI_PAGE_SIZE) / (1024 * 1024)));
  BdPageBufferAdd (&Buf, L"  ACPI Memory           %u MiB", (UINT32)((Acpi * EFI_PAGE_SIZE) / (1024 * 1024)));
  BdPageBufferAdd (&Buf, L"  Other Types           %u MiB", (UINT32)((Other * EFI_PAGE_SIZE) / (1024 * 1024)));

  FreePool (Map);
  return BdShowScrollablePage (L"Memory Map", &Buf);
}

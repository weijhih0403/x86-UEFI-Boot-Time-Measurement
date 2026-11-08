/** @file
  Event-driven keyboard input.

  Adapted from FirmwareDashboardPkg.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Input.h"

#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS
EFIAPI
BdWaitForKey (
  OUT EFI_INPUT_KEY  *Key
  )
{
  EFI_STATUS  Status;
  UINTN       Index;

  if (Key == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((gST == NULL) || (gST->ConIn == NULL) || (gBS == NULL)) {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return gST->ConIn->ReadKeyStroke (gST->ConIn, Key);
}

VOID
EFIAPI
BdFlushKeys (
  VOID
  )
{
  EFI_INPUT_KEY  Key;
  EFI_STATUS     Status;

  if ((gST == NULL) || (gST->ConIn == NULL)) {
    return;
  }

  do {
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  } while (!EFI_ERROR (Status));
}

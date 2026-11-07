/** @file
  NX741J physical key driver (VOL+/FOCUS/SHOOT) for the UEFI boot menu.

  The device tree maps the keys to TLMM lines (VOL+ = 101, FOCUS = 179,
  SHOOT = 95, all active low).  Only the first 64 KiB of the TLMM window is
  mapped, so the driver self-calibrates inside that window: it samples the
  tile/slot registers and records which (tile, slot, bit) drops when a key is
  pressed.  The result is stored in a UEFI variable and reused on later boots.
**/

#include <Uefi.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/DevicePath.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>

#define TLMM_BASE        0x0F000000ULL
#define TLMM_SIZE        0x202000ULL
#define KEY_COUNT        3
#define KEY_QUEUE_SIZE   16
#define CALIBRATION_MS   4000
#define POLL_MS          20
#define KEY_REGS         6
STATIC CONST UINT32  mKeyPin[KEY_COUNT] = { 101, 179, 95 };   /* VOL+, FOCUS, SHOOT (DTB) */
STATIC CONST UINT32  mRegOff[KEY_REGS]  = { 0x0, 0x4, 0x8, 0xC, 0x10, 0x14 };

STATIC EFI_GUID  mKeyVarGuid = { 0x4e2c1a77, 0x8b3d, 0x4f10, { 0x9a, 0x61, 0x5c, 0x2d, 0x7e, 0x88, 0x13, 0x90 } };

typedef struct {
  UINT32  Magic;
  UINT32  Offset[KEY_COUNT];
  UINT8   Bit[KEY_COUNT];
  UINT8   Valid;
} KEY_CALIBRATION;

#define KEY_MAGIC  0x4B45594E

typedef struct {
  EFI_KEY_DATA  Data[KEY_QUEUE_SIZE];
  UINTN         Head;
  UINTN         Tail;
} KEY_QUEUE;

typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} KEY_DEVICE_PATH;

STATIC KEY_CALIBRATION              mCal;
STATIC KEY_QUEUE                    mQueue;
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  mConInEx;
STATIC EFI_EVENT                    mTimer;
STATIC EFI_EVENT                    mWaitEvent;
STATIC BOOLEAN                      mPressed[KEY_COUNT];
STATIC UINT8                        mStable[KEY_COUNT];
STATIC UINT32                       mHits[KEY_REGS][32];

STATIC KEY_DEVICE_PATH  mDevicePath = {
  { { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 } },
    { 0x0b6f4c52, 0x21d4, 0x4b8e, { 0x8f, 0x30, 0x6a, 0x19, 0xc4, 0x55, 0x72, 0x01 } } },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
};

STATIC UINT16  mScanCode[KEY_COUNT] = { SCAN_UP, SCAN_DOWN, 0 };


STATIC BOOLEAN  mTlmmMapped = FALSE;

STATIC
BOOLEAN
EnsureTlmmMapped (
  VOID
  )
{
  EFI_STATUS                     Status;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Desc;

  Status = gDS->GetMemorySpaceDescriptor ((EFI_PHYSICAL_ADDRESS)TLMM_BASE, &Desc);
  if (EFI_ERROR (Status) || (Desc.GcdMemoryType == EfiGcdMemoryTypeNonExistent)) {
    Status = gDS->AddMemorySpace (
                     EfiGcdMemoryTypeMemoryMappedIo,
                     (EFI_PHYSICAL_ADDRESS)TLMM_BASE,
                     TLMM_SIZE,
                     EFI_MEMORY_UC
                     );
    DEBUG ((DEBUG_ERROR, "KEYS: AddMemorySpace %r\n", Status));
    if (EFI_ERROR (Status)) {
      return FALSE;
    }
  }

  Status = gDS->SetMemorySpaceAttributes (
                   (EFI_PHYSICAL_ADDRESS)TLMM_BASE,
                   TLMM_SIZE,
                   EFI_MEMORY_UC
                   );
  DEBUG ((DEBUG_ERROR, "KEYS: SetMemorySpaceAttributes %r\n", Status));
  return !EFI_ERROR (Status);
}

STATIC
UINT32
TlmmRead (
  IN UINT32  Offset
  )
{
  return *(volatile UINT32 *)(UINTN)(TLMM_BASE + Offset);
}

STATIC
VOID
QueuePush (
  IN EFI_KEY_DATA  *Data
  )
{
  UINTN  Next = (mQueue.Tail + 1) % KEY_QUEUE_SIZE;

  if (Next == mQueue.Head) {
    return;
  }

  mQueue.Data[mQueue.Tail] = *Data;
  mQueue.Tail             = Next;
  gBS->SignalEvent (mWaitEvent);
}

STATIC
EFI_STATUS
EFIAPI
KeyReset (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN BOOLEAN                            ExtendedVerification
  )
{
  mQueue.Head = mQueue.Tail = 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
KeyRead (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  OUT EFI_KEY_DATA                       *KeyData
  )
{
  if (mQueue.Head == mQueue.Tail) {
    return EFI_NOT_READY;
  }

  *KeyData    = mQueue.Data[mQueue.Head];
  mQueue.Head = (mQueue.Head + 1) % KEY_QUEUE_SIZE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
KeySetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN UINT8                              *KeyToggleState
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
KeyRegisterNotify (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN  EFI_KEY_DATA                       *KeyData,
  IN  EFI_KEY_NOTIFY_FUNCTION            KeyNotificationFunction,
  OUT VOID                               **NotifyHandle
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
KeyUnregisterNotify (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN VOID                               *NotificationHandle
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
VOID
EFIAPI
PollKeys (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN        Index;
  UINT32       Value;
  BOOLEAN      Pressed;
  EFI_KEY_DATA Key;

  if (!mCal.Valid) {
    return;
  }

  for (Index = 0; Index < KEY_COUNT; Index++) {
    Value   = TlmmRead (mCal.Offset[Index]);
    Pressed = ((Value >> mCal.Bit[Index]) & 1) == 0;

    if (Pressed == mPressed[Index]) {
      mStable[Index] = 0;
      continue;
    }

    if (++mStable[Index] < 2) {
      continue;
    }

    mStable[Index]  = 0;
    mPressed[Index] = Pressed;

    if (Pressed) {
      ZeroMem (&Key, sizeof (Key));
      if (Index == 2) {
        Key.Key.UnicodeChar = CHAR_CARRIAGE_RETURN;
      } else {
        Key.Key.ScanCode = mScanCode[Index];
      }

      QueuePush (&Key);
    }
  }
}

STATIC
VOID
WaitNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
}

STATIC
UINT32
BestHit (
  OUT UINTN  *Reg,
  OUT UINTN  *Bit
  )
{
  UINTN   R;
  UINTN   B;
  UINT32  Best = 0;

  *Reg = *Bit = 0;
  for (R = 0; R < KEY_REGS; R++) {
    for (B = 0; B < 32; B++) {
      if (mHits[R][B] > Best) {
        Best = mHits[R][B];
        *Reg = R;
        *Bit = B;
      }
    }
  }

  return Best;
}

STATIC
BOOLEAN
CalibrateKey (
  IN UINTN   Index,
  IN CHAR16  *Name
  )
{
  UINT32   Baseline[KEY_REGS];
  UINT32   Value;
  UINTN    Reg;
  UINTN    Bit;
  UINTN    Elapsed;
  UINTN    BestReg;
  UINTN    BestBit;
  UINT32   Best;
  UINT32   Base = mKeyPin[Index] * 0x1000;
  DEBUG ((DEBUG_ERROR, "\r\nKEYS: %S: hold the key for 2 seconds, starting now...\r\n", Name));

  for (Reg = 0; Reg < KEY_REGS; Reg++) {
    Baseline[Reg] = TlmmRead (Base + mRegOff[Reg]);
  }

  ZeroMem (mHits, sizeof (mHits));

  for (Elapsed = 0; Elapsed < CALIBRATION_MS; Elapsed += POLL_MS) {
    gBS->Stall (POLL_MS * 1000);

    for (Reg = 0; Reg < KEY_REGS; Reg++) {
      Value = TlmmRead (Base + mRegOff[Reg]);
      for (Bit = 0; Bit < 32; Bit++) {
        if (((Baseline[Reg] >> Bit) & 1) != 0 && ((Value >> Bit) & 1) == 0) {
          mHits[Reg][Bit]++;
        }
      }
    }
  }

  Best = BestHit (&BestReg, &BestBit);
  if (Best >= 5) {
    mCal.Offset[Index] = Base + mRegOff[BestReg];
    mCal.Bit[Index]    = (UINT8)BestBit;
    DEBUG ((DEBUG_ERROR, "KEYS: %S -> off 0x%x bit %u (hits %u)\n", Name, mCal.Offset[Index], mCal.Bit[Index], Best));
    gBS->Stall (1 * 1000 * 1000);
    return TRUE;
  }

  DEBUG ((DEBUG_ERROR, "KEYS: %S -> NOT DETECTED (best hits %u)\n", Name, Best));
  gBS->Stall (1 * 1000 * 1000);
  return FALSE;
}

STATIC
VOID
LoadCalibration (
  VOID
  )
{
  UINTN        Size = sizeof (mCal);
  EFI_STATUS   Status;

  ZeroMem (&mCal, sizeof (mCal));
  mCal.Magic = KEY_MAGIC;

  Status = gRT->GetVariable (L"Nx741jKeys", &mKeyVarGuid, NULL, &Size, &mCal);
  if (EFI_ERROR (Status) || (Size != sizeof (mCal)) || (mCal.Magic != KEY_MAGIC)) {
    ZeroMem (&mCal, sizeof (mCal));
    mCal.Magic = KEY_MAGIC;
  }
}

STATIC
VOID
SaveCalibration (
  VOID
  )
{
  gRT->SetVariable (
         L"Nx741jKeys",
         &mKeyVarGuid,
         EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
         sizeof (mCal),
         &mCal
         );
}

EFI_STATUS
EFIAPI
Nx741jKeysDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINTN       Index;
  BOOLEAN     VolOk;
  BOOLEAN     FocusOk;
  BOOLEAN     ShootOk;

  LoadCalibration ();

  if (!mCal.Valid) {
    mTlmmMapped = EnsureTlmmMapped ();
    DEBUG ((DEBUG_ERROR, "KEYS: begin calibration (mapped=%u)\n", mTlmmMapped));
    if (!mTlmmMapped) {
      DEBUG ((DEBUG_ERROR, "KEYS: TLMM window not mappable, keys unavailable\n"));
      mCal.Valid = 0;
    } else {
    VolOk   = CalibrateKey (0, L"VOL+");
    FocusOk = CalibrateKey (1, L"FOCUS");
    ShootOk = CalibrateKey (2, L"SHOOT");

    if (VolOk) {
      mCal.Valid = 1;
      SaveCalibration ();
      DEBUG ((DEBUG_ERROR, "KEYS: VOL+  -> off 0x%x bit %u\n", mCal.Offset[0], mCal.Bit[0]));
      gBS->Stall (1 * 1000 * 1000);
      DEBUG ((DEBUG_ERROR, "KEYS: FOCUS -> off 0x%x bit %u\n", mCal.Offset[1], mCal.Bit[1]));
      gBS->Stall (1 * 1000 * 1000);
      DEBUG ((DEBUG_ERROR, "KEYS: SHOOT -> off 0x%x bit %u\n", mCal.Offset[2], mCal.Bit[2]));
      gBS->Stall (1 * 1000 * 1000);
      DEBUG ((DEBUG_ERROR, "KEYS: calibrated and saved\n"));
      gBS->Stall (4 * 1000 * 1000);
    } else {
      DEBUG ((DEBUG_ERROR, "KEYS: calibration failed, menu will use timeout\n"));
      gBS->Stall (3 * 1000 * 1000);
    }
    }

    DEBUG ((
      DEBUG_ERROR,
      "NX741JKEYS: vol=%u focus=%u shoot=%u valid=%u\n",
      VolOk,
      FocusOk,
      ShootOk,
      mCal.Valid
      ));
  }

  for (Index = 0; Index < KEY_COUNT; Index++) {
    mPressed[Index] = FALSE;
    mStable[Index]  = 0;
  }

  mConInEx.Reset               = KeyReset;
  mConInEx.ReadKeyStrokeEx     = KeyRead;
  mConInEx.SetState            = KeySetState;
  mConInEx.RegisterKeyNotify   = KeyRegisterNotify;
  mConInEx.UnregisterKeyNotify = KeyUnregisterNotify;
  mConInEx.WaitForKeyEx        = &mWaitEvent;

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, WaitNotify, NULL, &mWaitEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEvent (EVT_TIMER, TPL_NOTIFY, PollKeys, NULL, &mTimer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->SetTimer (mTimer, TimerPeriodic, POLL_MS * 10000);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEfiSimpleTextInputExProtocolGuid,
                  &mConInEx,
                  &gEfiDevicePathProtocolGuid,
                  &mDevicePath,
                  NULL
                  );
  DEBUG ((DEBUG_ERROR, "NX741JKEYS: install %r valid=%u\n", Status, mCal.Valid));
  return Status;
}

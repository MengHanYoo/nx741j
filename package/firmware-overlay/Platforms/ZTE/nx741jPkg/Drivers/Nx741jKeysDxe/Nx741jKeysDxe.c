/** @file
  NX741J physical key driver for the UEFI boot menu (VOL+/SHOOT/FOCUS).

  Two jobs, in this order of importance:

  1. Publish a SimpleTextInput(Ex) device unconditionally. BDS connects the
     ConIn console variable during boot; with no input device at all it fails
     with "Connect ConIn failed - Not Found".
  2. Poll the TLMM lines the device tree assigns to the keys (VOL+ = 101,
     SHOOT = 95, FOCUS = 179, active low) and translate them to the menu keys
     the UniXNU menu understands: VOL+ = 'k' (up), SHOOT = 'j' (down),
     FOCUS = carriage return (enter).

  The poll only reads GPIO_IN_OUT for each line, after checking the GCD
  descriptor: pages the firmware does not describe as memory mapped I/O are
  never touched, so a board without TLMM (QEMU) still publishes the input
  device and simply reports no keys.
**/

#include <Uefi.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/DevicePath.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#define TLMM_BASE        0x0F000000ULL
#define TLMM_WINDOW      0x100000ULL
#define TLMM_GPIO_STRIDE 0x1000ULL
#define TLMM_IN_OUT      0x4ULL

#define KEY_COUNT        3
#define KEY_QUEUE_SIZE   16
#define POLL_MS          20
#define STABLE_SAMPLES   2

STATIC CONST UINT32  mKeyPin[KEY_COUNT] = { 101, 95, 179 };   /* VOL+, SHOOT, FOCUS */
STATIC CONST CHAR16  mKeyChar[KEY_COUNT] = { L'k', L'j', L'\r' };

typedef struct {
  EFI_KEY_DATA  Data[KEY_QUEUE_SIZE];
  UINTN         Head;
  UINTN         Tail;
} KEY_QUEUE;

typedef struct {
  VENDOR_DEVICE_PATH        Vendor;
  EFI_DEVICE_PATH_PROTOCOL  End;
} KEY_DEVICE_PATH;

STATIC KEY_QUEUE                          mQueue;
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  mConInEx;
STATIC EFI_EVENT                          mTimer;
STATIC EFI_EVENT                          mWaitEvent;
STATIC BOOLEAN                            mTlmmMapped;
STATIC BOOLEAN                            mPressed[KEY_COUNT];
STATIC UINT8                              mStable[KEY_COUNT];

STATIC KEY_DEVICE_PATH  mDevicePath = {
  { { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 } },
    { 0x0b6f4c52, 0x21d4, 0x4b8e, { 0x8f, 0x30, 0x6a, 0x19, 0xc4, 0x55, 0x72, 0x01 } } },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
};

STATIC
BOOLEAN
TlmmMapped (
  VOID
  )
{
  EFI_STATUS                     Status;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Desc;

  if (mTlmmMapped) {
    return TRUE;
  }

  ZeroMem (&Desc, sizeof (Desc));
  Status = gDS->GetMemorySpaceDescriptor ((EFI_PHYSICAL_ADDRESS)TLMM_BASE, &Desc);
  if (EFI_ERROR (Status) ||
      (Desc.GcdMemoryType != EfiGcdMemoryTypeMemoryMappedIo) ||
      (Desc.Length < TLMM_WINDOW)) {
    DEBUG ((DEBUG_WARN, "KEYS: TLMM not described (type %u len 0x%lx); keys off\n",
            EFI_ERROR (Status) ? 0xFFu : (UINT32)Desc.GcdMemoryType,
            EFI_ERROR (Status) ? 0ULL : (UINT64)Desc.Length));
    return FALSE;
  }

  mTlmmMapped = TRUE;
  DEBUG ((DEBUG_INFO, "KEYS: TLMM mapped, polling pins 101/95/179\n"));
  return TRUE;
}

STATIC
UINT32
TlmmInOut (
  IN UINT32  Pin
  )
{
  return *(volatile UINT32 *)(UINTN)(TLMM_BASE + (UINT64)Pin * TLMM_GPIO_STRIDE + TLMM_IN_OUT);
}

STATIC
VOID
QueueChar (
  IN CHAR16  Ch
  )
{
  UINTN         Next = (mQueue.Tail + 1) % KEY_QUEUE_SIZE;
  EFI_KEY_DATA  Key;

  if (Next == mQueue.Head) {
    return;
  }

  ZeroMem (&Key, sizeof (Key));
  Key.Key.UnicodeChar         = Ch;
  Key.Key.ScanCode            = SCAN_NULL;
  Key.KeyState.KeyShiftState  = 0;
  Key.KeyState.KeyToggleState = 0;
  mQueue.Data[mQueue.Tail]    = Key;
  mQueue.Tail                 = Next;
  gBS->SignalEvent (mWaitEvent);
}

STATIC
VOID
EFIAPI
PollKeys (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN    Index;
  BOOLEAN  Down;

  if (!TlmmMapped ()) {
    return;
  }

  for (Index = 0; Index < KEY_COUNT; Index++) {
    Down = (TlmmInOut (mKeyPin[Index]) & 1U) == 0;   /* active low */
    if (Down == mPressed[Index]) {
      mStable[Index] = 0;
      continue;
    }

    if (++mStable[Index] < STABLE_SAMPLES) {
      continue;
    }

    mStable[Index]  = 0;
    mPressed[Index] = Down;
    if (Down) {
      QueueChar (mKeyChar[Index]);
      DEBUG ((DEBUG_INFO, "KEYS: pin %u pressed\n", mKeyPin[Index]));
    }
  }
}

STATIC
EFI_STATUS
EFIAPI
ConInResetEx (
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
ConInReadKeyStrokeEx (
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
ConInSetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN UINT8                              *KeyToggleState
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ConInRegisterKeyNotify (
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
ConInUnregisterKeyNotify (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN VOID                               *NotificationHandle
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
Nx741jKeysDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  ZeroMem (&mQueue, sizeof (mQueue));
  ZeroMem (mPressed, sizeof (mPressed));
  ZeroMem (mStable, sizeof (mStable));

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, EfiEventEmptyFunction,
                             NULL, &mWaitEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mConInEx.Reset               = ConInResetEx;
  mConInEx.ReadKeyStrokeEx     = ConInReadKeyStrokeEx;
  mConInEx.WaitForKeyEx        = mWaitEvent;
  mConInEx.SetState            = ConInSetState;
  mConInEx.RegisterKeyNotify   = ConInRegisterKeyNotify;
  mConInEx.UnregisterKeyNotify = ConInUnregisterKeyNotify;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEfiDevicePathProtocolGuid, &mDevicePath,
                  &gEfiSimpleTextInputExProtocolGuid, &mConInEx,
                  NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "KEYS: install %r\n", Status));
    return Status;
  }

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, PollKeys, NULL, &mTimer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->SetTimer (mTimer, TimerPeriodic, (UINT64)POLL_MS * 10000ULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, "KEYS: input device published\n"));
  return EFI_SUCCESS;
}

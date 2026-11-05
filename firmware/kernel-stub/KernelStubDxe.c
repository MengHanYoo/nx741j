/** @file
  Stub EFI_KERNEL_PROTOCOL for the nx741j BootShim bring-up.

  The Qualcomm scheduler protocol (gEfiKernelProtocolGuid) is normally
  supplied by XBL and handed to UEFI through the Scheduler Interface HOB.
  When the FV is entered directly from BootShim that interface does not
  exist, and drivers that need scheduler services fail:

    - DALSYSDxe asserts in its scheduler constructor if the HOB is missing
      (handled by the PlatformPeiLib fallback HOB);
    - ChipInfoDxe calls Kernel->Lock->InitRecursiveLock() and asserts in its
      own spinlock.c when the protocol is absent.

  This driver publishes a minimal protocol: working Lock/Event/Mutex/Sem/
  Spinlock interfaces backed by no-op objects, and NULL for the interfaces
  that would require a real scheduler (interrupts, timers, threads, MP CPU,
  watchdog).  Drivers that check those pointers before use fall back to
  their local implementations; the ones reached during bring-up only need
  the lock-like primitives.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/EFIKernelInterface.h>

//
// Report the version that introduced the Lock API; drivers gate the protocol
// path on Version >= 0x00010005.
//
#define KERNEL_STUB_VERSION  EFI_KERNEL_PROTOCOL_VER_LOCK_API

STATIC UINT64  mDummyObject;

//
// Lock interface
//
STATIC
EFI_STATUS
EFIAPI
StubInitLock (
  IN  CONST CHAR8  *LockStrId OPTIONAL,
  OUT LockHandle   **Handle
  )
{
  if (Handle == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Handle = (LockHandle *)&mDummyObject;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
StubDestroyLock (
  IN LockHandle  *Handle
  )
{
  (VOID)Handle;
}

STATIC
VOID
EFIAPI
StubAcquireLock (
  IN LockHandle  *Handle
  )
{
  (VOID)Handle;
}

STATIC
VOID
EFIAPI
StubReleaseLock (
  IN LockHandle  *Handle
  )
{
  (VOID)Handle;
}

STATIC LockIntf  mLockIntf = {
  StubInitLock,
  StubInitLock,
  StubDestroyLock,
  StubAcquireLock,
  StubReleaseLock
};

//
// Event interface
//
STATIC
Event *
EFIAPI
StubEventInit (
  IN UINT64  Id OPTIONAL,
  IN _Bool   Initial,
  IN UINT32  Flags
  )
{
  return (Event *)&mDummyObject;
}

STATIC
VOID
EFIAPI
StubEventDestroy (
  IN Event  *Evt
  )
{
  (VOID)Evt;
}

STATIC
_Bool
EFIAPI
StubEventInitialized (
  IN Event  *Evt
  )
{
  (VOID)Evt;
  return (_Bool)1;
}

STATIC
KStatus
EFIAPI
StubEventWait (
  IN Event  *Evt
  )
{
  (VOID)Evt;
  return NO_ERROR;
}

STATIC
KStatus
EFIAPI
StubEventWaitTimeout (
  IN Event         *Evt,
  IN TimeDuration  Timeout
  )
{
  (VOID)Evt;
  (VOID)Timeout;
  return NO_ERROR;
}

STATIC
KStatus
EFIAPI
StubEventSignal (
  IN Event  *Evt,
  IN _Bool  Reschedule
  )
{
  (VOID)Evt;
  (VOID)Reschedule;
  return NO_ERROR;
}

STATIC
KStatus
EFIAPI
StubEventUnsignal (
  IN Event  *Evt
  )
{
  (VOID)Evt;
  return NO_ERROR;
}

STATIC
_Bool
EFIAPI
StubEventGetSignalState (
  IN Event  *Evt
  )
{
  (VOID)Evt;
  return (_Bool)1;
}

STATIC EventIntf  mEventIntf = {
  StubEventInit,
  StubEventDestroy,
  StubEventInitialized,
  StubEventWait,
  StubEventWaitTimeout,
  StubEventSignal,
  StubEventUnsignal,
  StubEventGetSignalState
};

//
// Mutex interface
//
STATIC
Mutex *
EFIAPI
StubMutexInit (
  IN UINT64  Id OPTIONAL
  )
{
  return (Mutex *)&mDummyObject;
}

STATIC
VOID
EFIAPI
StubMutexDestroy (
  IN Mutex  *Mtx
  )
{
  (VOID)Mtx;
}

STATIC
KStatus
EFIAPI
StubMutexAcquire (
  IN Mutex  *Mtx
  )
{
  (VOID)Mtx;
  return NO_ERROR;
}

STATIC
KStatus
EFIAPI
StubMutexAcquireTimeout (
  IN Mutex         *Mtx,
  IN TimeDuration  Timeout
  )
{
  (VOID)Mtx;
  (VOID)Timeout;
  return NO_ERROR;
}

STATIC
KStatus
EFIAPI
StubMutexRelease (
  IN Mutex  *Mtx
  )
{
  (VOID)Mtx;
  return NO_ERROR;
}

STATIC
_Bool
EFIAPI
StubIsMutexHeld (
  IN Mutex  *Mtx
  )
{
  (VOID)Mtx;
  return (_Bool)0;
}

STATIC MutexIntf  mMutexIntf = {
  StubMutexInit,
  StubMutexDestroy,
  StubMutexAcquire,
  StubMutexAcquireTimeout,
  StubMutexRelease,
  StubIsMutexHeld
};

//
// Semaphore interface
//
STATIC
Semaphore *
EFIAPI
StubSemInit (
  IN UINT64  Id OPTIONAL,
  IN UINT32  Cnt
  )
{
  (VOID)Cnt;
  return (Semaphore *)&mDummyObject;
}

STATIC
VOID
EFIAPI
StubSemDestroy (
  IN Semaphore  *Sem
  )
{
  (VOID)Sem;
}

STATIC
INT32
EFIAPI
StubSemPost (
  IN Semaphore  *Sem,
  IN _Bool      Resched
  )
{
  (VOID)Sem;
  (VOID)Resched;
  return 0;
}

STATIC
KStatus
EFIAPI
StubSemWait (
  IN Semaphore  *Sem
  )
{
  (VOID)Sem;
  return NO_ERROR;
}

STATIC
KStatus
EFIAPI
StubSemTryWait (
  IN Semaphore  *Sem
  )
{
  (VOID)Sem;
  return NO_ERROR;
}

STATIC
KStatus
EFIAPI
StubSemTimedWait (
  IN Semaphore    *Sem,
  IN TimeDuration  Timeout
  )
{
  (VOID)Sem;
  (VOID)Timeout;
  return NO_ERROR;
}

STATIC SemIntf  mSemIntf = {
  StubSemInit,
  StubSemDestroy,
  StubSemPost,
  StubSemWait,
  StubSemTryWait,
  StubSemTimedWait
};

//
// Spinlock interface
//
STATIC
Spinlock *
EFIAPI
StubSpinLockInit (
  IN UINT64  Id OPTIONAL
  )
{
  return (Spinlock *)&mDummyObject;
}

STATIC
_Bool
EFIAPI
StubSpinLockHeld (
  IN Spinlock  *Spin
  )
{
  (VOID)Spin;
  return (_Bool)0;
}

STATIC
INTN
EFIAPI
StubSpinTryLock (
  IN Spinlock  *Spin
  )
{
  (VOID)Spin;
  return 1;
}

STATIC
VOID
EFIAPI
StubSpinLock (
  IN Spinlock  *Spin
  )
{
  (VOID)Spin;
}

STATIC
VOID
EFIAPI
StubSpinUnlock (
  IN Spinlock  *Spin
  )
{
  (VOID)Spin;
}

STATIC SpinlockIntf  mSpinlockIntf = {
  StubSpinLockInit,
  StubSpinLockHeld,
  StubSpinTryLock,
  StubSpinLock,
  StubSpinUnlock
};

//
// Version query
//
STATIC
UINT32
EFIAPI
StubGetLibVersion (
  VOID
  )
{
  return (UINT32)KERNEL_STUB_VERSION;
}

STATIC EFI_KERNEL_PROTOCOL  mKernelProtocol = {
  KERNEL_STUB_VERSION,   // Version
  NULL,                  // Interrupt
  NULL,                  // Timer
  NULL,                  // Thread
  &mEventIntf,           // Event
  &mMutexIntf,           // Mutex
  &mSemIntf,             // Sem
  &mSpinlockIntf,        // Spinlock
  NULL,                  // MpCpu
  NULL,                  // WDog
  StubGetLibVersion,     // GetLibVersion
  &mLockIntf             // Lock
};

/**
  Publish the stub EFI_KERNEL_PROTOCOL.

  @param[in] ImageHandle  Image handle of this driver.
  @param[in] SystemTable  Pointer to the EFI System Table.

  @retval EFI_SUCCESS  The protocol was installed.
  @return              Status of InstallMultipleProtocolInterfaces().
**/
EFI_STATUS
EFIAPI
KernelStubEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  Handle = NULL;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiKernelProtocolGuid,
                  &mKernelProtocol,
                  NULL
                  );

  DEBUG ((DEBUG_ERROR, "KernelStubDxe: EFI_KERNEL_PROTOCOL install -> %r\n", Status));

  return Status;
}

/*
 * Built-in IOMobileFramebuffer for SWEET: the ABL's continuous-splash
 * framebuffer (1080x2400 a8r8g8b8 in RAM, the panel scans it out by
 * itself) as the main display, driven by CoreAnimation's CPU renderer.
 *
 * IOMobileFramebuffer.framework (iOS 16.7) counts /arm-io children with
 * device_type "display-subsystem", waits for that many services matching
 * IOServiceMatching("IOMobileFramebuffer"), opens each with type 0 and
 * reads canvas-width/canvas-height from the service's provider. The class
 * here (AppleCLCD, an IOMobileFramebuffer) matches the /arm-io/disp0 nub.
 *
 * User client selectors used by CoreAnimation:
 *   3  GetLayerDefaultSurface [layer, ?] -> [IOSurface port name]
 *   4  SwapBegin -> [swap id]
 *   5  SwapEnd, struct 0x46c: +0x50 swap id, +0x54 IOSurface id per layer
 *      (4 layers), +0x64 rect a / +0xc4 rect b per layer (int x,y,w,h),
 *      +0x104 enabled layer mask
 *   6  SwapWait [swap id, options, timeout ms] (PANDORA, 0218: as iOS 16's swap_wait_gated, swapWait()),
 *   7  GetID, 8 GetDisplaySize -> [w, h],
 *   12 RequestPowerChange [state]: 0 = display off (backboardd, power
 *      button / auto-lock), else on. Off fades the panel out over a few
 *      vsyncs (halving each pixel) and keeps it black until on again;
 *      the panel itself stays powered (AMOLED black pixels are off).
 *   18 IsMainDisplay,
 *   28 GetDotPitch, 72 Enable/DisableNotifications [func, refcon, type, 0]
 * Everything else is accepted and logged once.
 *
 * SwapEnd copies the enabled layers' surfaces into the panel memory (no
 * scaling, CPU copy). Vsync (notification type 5) is a 60 Hz timer that
 * sends (count, mach_absolute_time, 0) as 32-bit halves to clients that
 * enabled it, through the port they registered (IOConnectSetNotificationPort).
 *
 * The panel memory itself is published as an IOSurface and returned as the
 * default framebuffer surface. The kernel text console keeps drawing over
 * the GUI by default: boot-arg sweetfb_noconsole=1 stops drawing it from
 * the first swap on (it still reaches the persistent log; panics draw).
 * PANDORA stops it by default from the first swap that shows a surface
 * (empty swaps come first); sweetfb_noconsole=0 keeps drawing.
 *
 * FUXI (0093): frames are shown at up to 60 Hz by default (7 fast cores;
 * sweetfb_hz still overrides), the tick runs on absolute deadlines so the
 * copy time does not stretch the period, every 600 presented frames log
 * "SWEET FB: stats" (copy time, swaps, dropped swaps), and display off is a
 * single black fill (write-only: the panel memory is write-combined, the
 * read-modify-write fade read it back uncached). sweet_fb_display_off tells
 * the touch driver (bsd/dev/fuxi_touch.c) that the display is off.
 */
#include "SweetSurface.h"
#if defined(FUXI)
extern "C" {
#include "../../../bsd/dev/fuxi_adreno.h"
void fuxi_perf_start(void);
void fuxi_perf_swap(void);
}
#endif

extern "C" int sweet_console_muted;     /* osfmk/console/serial_console.c */
#if defined(PANDORA)
extern "C" uint8_t *pandora_fbdump_begin(uint32_t *cap);       /* bsd/dev/sweet_ufs.c */
extern "C" void pandora_fbdump_commit(uint32_t len);
extern "C" void pandora_dpu_probe(uint64_t fb);                 /* bsd/dev/pandora_dpu.c (0215) */
/* bsd/dev/pandora_dpu.c (0217): the pipes scan out a surface themselves. */
extern "C" int pandora_scanout_init(void);
extern "C" uint32_t pandora_scanout_map(const uint64_t *pages, uint32_t npages);
extern "C" void pandora_scanout_unmap(uint32_t iova, uint32_t npages);
extern "C" bool pandora_scanout_show(uint32_t addr, uint32_t stride, uint32_t w, uint32_t h, bool fill);
extern "C" bool pandora_scanout_panel(void);
extern "C" bool pandora_scanout_current(uint32_t addr);
extern "C" bool pandora_scanout_ready(void);
extern "C" void pandora_scanout_counts(uint64_t *shows, uint64_t *faults);
extern "C" bool pandora_scanout_flushed(void);
#define SWEET_SCAN_MAPS 6

/* Dump header (block 0 of the dump area); frames follow at 4096, BGRA, tightly packed. */
struct pandora_fbdump_head {
	char magic[8];                  /* "PANDFBDM" */
	uint32_t version, seq, frames, scale;
	uint64_t uptime_ms;
	struct {
		char name[16];
		uint32_t width, height, offset, format;
	} f[2];
};

/* Every 3rd pixel of every 3rd row of a 32-bit image into out; returns bytes written. */
static uint32_t
pandora_fbdump_scale(uint8_t *out, uint32_t cap, uintptr_t base, uint32_t rowBytes,
    uint32_t width, uint32_t height, uint32_t *ow, uint32_t *oh)
{
	uint32_t w = width / 3, h = height / 3;
	if ((uint64_t)w * h * 4 > cap) {
		*ow = *oh = 0;
		return 0;
	}
	uint32_t *o = (uint32_t *)out;
	for (uint32_t y = 0; y < h; y++) {
		const uint32_t *row = (const uint32_t *)(base + (uint64_t)y * 3 * rowBytes);
		for (uint32_t x = 0; x < w; x++) {
			*o++ = row[x * 3];
		}
	}
	*ow = w;
	*oh = h;
	return w * h * 4;
}
#endif
extern "C" { int sweet_fb_display_off; }        /* read by bsd/dev/fuxi_touch.c */
#if defined(PANDORA)
/*
 * Brightness from iOS (0136): kern_SwapSetBrightness puts a double and a "set" flag in
 * the SwapEnd record (flag 0x2e7, value 0x2ec; the limit: 0x2e8 / 0x2f4). The panel stays
 * at ABL's level; present() scales each pixel by sweet_fb_dim / 256 instead.
 */
static volatile uint32_t sweet_fb_dim = 256;
static unsigned sweet_fb_fullcopy_logged;
static uint32_t sweet_fb_bright_milli = ~0u, sweet_fb_limit_milli = ~0u;
static unsigned sweet_fb_bright_logs;

/* IEEE 754 double (little endian, maybe unaligned) -> value * 1000, clamped; no FP use. */
static uint32_t
sweet_fb_double_milli(const uint8_t *p)
{
	uint64_t b = 0;
	for (int i = 7; i >= 0; i--) {
		b = (b << 8) | p[i];
	}
	int e = (int)((b >> 52) & 0x7ff);
	if ((b >> 63) || e == 0) {
		return 0;
	}
	if (e == 0x7ff) {
		return 0xffffffffu;
	}
	uint64_t m = (b & ((1ULL << 52) - 1)) | (1ULL << 52);
	int sh = e - 1075;
	if (sh >= 0) {
		return 0xffffffffu;
	}
	if (sh <= -64) {
		return 0;
	}
	uint64_t v = (m * 1000) >> -sh;     /* m < 2^53: m * 1000 < 2^63 */
	return v > 0xffffffffULL ? 0xffffffffu : (uint32_t)v;
}

/* Brightness value -> scale: 0..1 is a fraction; above 1 it is taken as nits of a
 * 500-nit panel. Never below 1/16 so the screen stays readable. */
static void
sweet_fb_brightness(const uint8_t *rec)
{
	uint32_t b = rec[0x2e7] ? sweet_fb_double_milli(rec + 0x2ec) : sweet_fb_bright_milli;
	uint32_t l = rec[0x2e8] ? sweet_fb_double_milli(rec + 0x2f4) : sweet_fb_limit_milli;
	if (b == sweet_fb_bright_milli && l == sweet_fb_limit_milli) {
		return;
	}
	sweet_fb_bright_milli = b;
	sweet_fb_limit_milli = l;
	if (b == ~0u) {
		return;
	}
	uint64_t frac = b <= 1000 ? b : (uint64_t)b / 500;      /* 0..1000 */
	if (l != ~0u && l != 0) {
		uint64_t lf = l <= 1000 ? l : (uint64_t)l / 500;
		frac = frac < lf ? frac : lf;
	}
	frac = frac > 1000 ? 1000 : frac;
	uint32_t dim = (uint32_t)(frac * 256 / 1000);
	sweet_fb_dim = dim < 16 ? 16 : dim;
	if (sweet_fb_bright_logs++ < 40) {
		IOLog("SWEET FB: brightness %u.%03u limit %u.%03u -> scale %u/256\n", b / 1000, b % 1000,
		    l == ~0u ? 0 : l / 1000, l == ~0u ? 0 : l % 1000, sweet_fb_dim);
	}
}

static inline uint32_t
sweet_fb_scale(uint32_t p, uint32_t k)
{
	uint32_t rb = ((p & 0x00ff00ffu) * k >> 8) & 0x00ff00ffu;
	uint32_t g = (((p >> 8) & 0x000000ffu) * k) & 0x0000ff00u;
	return (p & 0xff000000u) | rb | g;
}
#endif

#if defined(SWEET)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/system.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include <sys/proc.h>
#if defined(PANDORA)
extern "C" void flush_dcache(vm_offset_t addr, unsigned count, int phys);
#include <sys/errno.h>
extern "C" uint32_t pandora_gpu_issued(void);                         /* bsd/dev/pandora_gpu.c */
extern "C" int pandora_gpu_wait(uint64_t fence, uint64_t timeout_ns);
#include <kern/thread.h>

/* Patch 0188: with a Metal client the GPU writes IOSurfaces non-cacheably (bsd/dev/pandora_gpu.c);
 * the copy below then cleans and invalidates each source row first, or it can copy stale lines. */
static bool
sweetfb_gpu_frames(void)
{
	static int on = -1;
	if (on < 0) {
		on = 0;
		PE_parse_boot_argn("pandora_gpu_metal", &on, sizeof(on));
	}
	return on != 0;
}
#endif

enum {
	kSweetFBGetLayerDefaultSurface  = 3,
	kSweetFBSwapBegin               = 4,
	kSweetFBSwapEnd                 = 5,
	kSweetFBSwapWait                = 6,
	kSweetFBGetID                   = 7,
	kSweetFBGetDisplaySize          = 8,
	kSweetFBRequestPowerChange      = 12,
	kSweetFBIsMainDisplay           = 18,
	kSweetFBGetDotPitch             = 28,
	kSweetFBNotifications           = 72,
};

#define SWEET_FB_SWAP_REC_SIZE          0x46c
#define SWEET_FB_LAYERS                 4
#define SWEET_FB_NOTIFY_TYPES           8
#define SWEET_FB_NOTIFY_VSYNC           5
#define SWEET_FB_VSYNC_US               16667
#define SWEET_FB_DOT_PITCH              458     /* ppi, as the iPhone X panel */
/*
 * The iOS userland only has status bar / layout data for real iPhone
 * screens (UIKit: "No status bar visual provider for screen", the red
 * rdar://45025538 bar for 360x800 points). The display is therefore
 * presented as the iPhone X panel, 1125x2436 (375x812 points @3x), and the
 * centered 1080x2400 part of it is shown on the 1080x2400 panel.
 * Boot-arg sweetfb_native=1 presents the real panel size instead.
 */
#define SWEET_FB_LOGICAL_W              1125
#define SWEET_FB_LOGICAL_H              2436
#define SWEET_FB_FORMAT_BGRA            0x42475241

class IOMobileFramebuffer : public IOService {
	OSDeclareDefaultStructors(IOMobileFramebuffer);
};

OSDefineMetaClassAndStructors(IOMobileFramebuffer, IOService);

class IOMobileFramebufferUserClient;

class AppleCLCD : public IOMobileFramebuffer {
	OSDeclareDefaultStructors(AppleCLCD);

	IOWorkLoop         *fWorkLoop;
	IOTimerEventSource *fVsync;
	uint64_t            fVsyncCount;

	static void vsyncFired(OSObject *owner, IOTimerEventSource *sender);

public:
	uint64_t      fPhys;
	uint32_t      fWidth, fHeight, fRowBytes;
	uint32_t      fLogicalW, fLogicalH;     /* size reported to userland */
	int32_t       fOffX, fOffY;             /* logical origin of the panel */
#if defined(PANDORA)
	bool          fFill;                    /* sweetfb_fill=1: scale to the whole panel */
	uint16_t      fFillX[2048];             /* panel x -> logical x */
#endif
	uintptr_t     fPixels;          /* kernel mapping of the panel memory */
	uintptr_t     fDraw;            /* NX741J: where present() draws: the panel, or the back buffer */
	uintptr_t     fBack;            /* NX741J: double buffer, sweetfb_doublebuffer=1 */
	size_t        fBackBytes;
	IOMemoryMap  *fMap;
	SweetSurface *fSurface;         /* the panel memory as an IOSurface */
	IOLock       *fLock;            /* swaps and client list */
	OSArray      *fClients;
	uint32_t      fSwapID;
	bool          fConsolePaused;
	uint8_t       fPending[SWEET_FB_SWAP_REC_SIZE];     /* latest swap, not yet shown */
	bool          fDirty;
	bool          fCopying;     /* the vsync timer is copying the frame it took from fPending */
	uint32_t      fFlushTicks;    /* vsync ticks between panel copies */
	bool          fPowerOff;      /* RequestPowerChange(0) */
	uint32_t      fFadeStep;      /* fade-out steps done while off */
	uint64_t      fTickAbs;       /* vsync period, absolute time units */
	uint64_t      fNextTick;      /* next vsync deadline */
	uint64_t      fStatCopy, fStatCopyMax, fStatStart;
	uint32_t      fStatFrames, fStatSwaps, fStatDropped;
	uint32_t      fGpuFrames, fCpuFrames;
	uint64_t      fGpuTime, fCpuTime;
	bool          fGpuPending;
	uint32_t      fGpuBusyTicks, fGpuFailed;
	uint32_t      fPendingSeq;
	uint32_t      fLastSurf[SWEET_FB_LAYERS];   /* NX741J: surface rotation detector */    /* PANDORA: GPU fence issued when fPending was swapped (0: none) */
	uint32_t      fStatGpuWait;   /* PANDORA: frames that waited for their GPU work */
#if defined(PANDORA)
	/* 0217: scanout. Each of shown / queued holds one retain and one IOSurface use count. */
	bool          fScanWanted;    /* Metal on and pandora_scanout not 0: try at the first frame that fits */
	bool          fScanTried;
	bool          fScanOn;
	bool          fScanToPanel;   /* the pipes were sent back to the panel memory; holds go when that shows */
	SweetSurface *fScanShown;     /* the surface the pipes read */
	SweetSurface *fScanQueued;    /* the surface the pipes were just given (not yet seen as current) */
	uint32_t      fScanQueuedAddr;
	struct { SweetSurface *s; uint32_t iova, pages; uint64_t used; } fScanMaps[SWEET_SCAN_MAPS];
	uint64_t      fScanClock;
	uint32_t      fScanFrames, fScanCopies, fScanRefused;
	/* 0218: the swap queue (presentLoop). Queued: fPending while fDirty; active: taken, going on screen (fCopying);
	 * shown: on screen now. */
	uint32_t      fSwapQueued, fSwapActive, fSwapShown;
	bool          fOffDone;       /* display off: the panel black, the pipes on it */
	uint32_t      fSwapWaits, fSwapBusy, fSwapSlept, fSwapTimeouts, fSwapLogged;
	uint64_t      fLatchTime, fLatchMax;  /* flush to latch, absolute time */
	uint32_t      fLatchCount, fLatchLate;
#endif

	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void addClient(IOMobileFramebufferUserClient *client);
	void removeClient(IOMobileFramebufferUserClient *client);
	void swap(const uint8_t *rec);
	void present(const uint8_t *rec);
#if defined(PANDORA)
	uint32_t scanIova(SweetSurface *s);
	bool scanPresent(const uint8_t *rec, uint32_t mask);
	void scanPanel(void);
	void scanTick(void);
	bool scanSettle(void);
	void presentLoop(void);
	void displayOff(void);
	bool swapDone(uint32_t id, uint32_t type);
	IOReturn swapWait(uint32_t id, uint32_t options, uint32_t timeoutMs);
#endif
};

OSDefineMetaClassAndStructors(AppleCLCD, IOMobileFramebuffer);

#if defined(PANDORA)
/*
 * Screen thumbnail into the log (0142), for bsd/dev/pandora_selftest.c: 60 x 130 samples
 * of the panel memory (what is really shown), one hex digit of grey each, one line per
 * row, "SWEET THUMB <tag> <row> <60 digits>". tools/thumb.py turns it into a PNG.
 */
static AppleCLCD *gSweetCLCD;
extern "C" void sweet_fb_thumb(const char *tag);
extern "C" void
sweet_fb_thumb(const char *tag)
{
	AppleCLCD *fb = gSweetCLCD;
	if (!fb || !fb->fPixels) {
		return;
	}
	const uint32_t cols = 60, rows = 130;
	char line[64];
	for (uint32_t r = 0; r < rows; r++) {
		uint32_t y = (uint32_t)(((uint64_t)r * 2 + 1) * fb->fHeight / (2 * rows));
		const volatile uint32_t *row = (const volatile uint32_t *)(fb->fPixels + (uint64_t)y * fb->fRowBytes);
		for (uint32_t c = 0; c < cols; c++) {
			uint32_t x = (uint32_t)(((uint64_t)c * 2 + 1) * fb->fWidth / (2 * cols));
			uint32_t px = row[x];
			uint32_t g = (((px >> 16) & 0xff) * 77 + ((px >> 8) & 0xff) * 150 + (px & 0xff) * 29) >> 12;
			line[c] = "0123456789abcdef"[g & 15];
		}
		line[cols] = 0;
		IOLog("SWEET THUMB %s %03u %s\n", tag, r, line);
	}
}
#endif

class IOMobileFramebufferUserClient : public IOUserClient {
	OSDeclareDefaultStructors(IOMobileFramebufferUserClient);

public:
	AppleCLCD          *fFB;
	task_t              fTask;
	mach_port_t         fPort;
	io_user_reference_t fFunc[SWEET_FB_NOTIFY_TYPES];
	io_user_reference_t fRefcon[SWEET_FB_NOTIFY_TYPES];

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn registerNotificationPort(mach_port_t port, UInt32 type,
	    io_user_reference_t refCon) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	void notify(uint32_t type, const uint64_t *values, uint32_t count);
};

OSDefineMetaClassAndStructors(IOMobileFramebufferUserClient, IOUserClient);
#if defined(FUXI)
extern "C" void fuxi_display_kick(void);
#endif

#if defined(PANDORA)
/*
 * Backlight service for CoreBrightness (0137). BrightnessControl's
 * BCAppleBacklightBrtControl looks up a backlight service (IOServiceMatching
 * "AppleARMBacklight" / IOPropertyMatch "backlight-control"), reads its
 * "IODisplayParameters" and sets brightness with IORegistryEntrySetCFProperty. Every
 * property it writes is logged; numbers under "brightness", "BrightnessMilliNits" or
 * "brightness-nits" (also inside a dictionary, or a parameter dictionary's "value")
 * become the pixel scale used by present() (sweet_fb_dim).
 */
#include <libkern/c++/OSCollectionIterator.h>
class AppleARMBacklight : public IOService {
	OSDeclareDefaultStructors(AppleARMBacklight);
public:
	virtual IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
	void publishParameters(void);
};
OSDefineMetaClassAndStructors(AppleARMBacklight, IOService);

static unsigned sweet_bl_logs;

static void
sweet_bl_param(OSDictionary *params, const char *key, uint32_t min, uint32_t max, uint32_t value)
{
	OSDictionary *d = OSDictionary::withCapacity(3);
	const char *names[3] = {"min", "max", "value"};
	uint32_t vals[3] = {min, max, value};
	for (int i = 0; d && i < 3; i++) {
		OSNumber *n = OSNumber::withNumber(vals[i], 32);
		if (n) {
			d->setObject(names[i], n);
			n->release();
		}
	}
	if (d) {
		params->setObject(key, d);
		d->release();
	}
}

void
AppleARMBacklight::publishParameters(void)
{
	OSDictionary *params = OSDictionary::withCapacity(2);
	if (params) {
		/* brightness: 0..65536 (1.0 = the level ABL left); nits as 16.16, 2..500. */
		sweet_bl_param(params, "brightness", 0, 65536, 65536);
		sweet_bl_param(params, "brightness-nits", 2 << 16, 500 << 16, 500 << 16);
		setProperty("IODisplayParameters", params);
		params->release();
	}
	setProperty("backlight-control", kOSBooleanTrue);
	setProperty("user-accessible-max-nits", 500ULL, 32);
	setProperty("edr-max-nits", 500ULL, 32);
}

/* Returns the number under key (or the "value" of a parameter dictionary there). */
static bool
sweet_bl_number(OSObject *o, uint64_t *out)
{
	OSNumber *n = OSDynamicCast(OSNumber, o);
	OSDictionary *d = OSDynamicCast(OSDictionary, o);
	if (!n && d) {
		n = OSDynamicCast(OSNumber, d->getObject("value"));
	}
	if (!n) {
		return false;
	}
	*out = n->unsigned64BitValue();
	return true;
}

/*
 * Panel brightness over DSI (0139). The main panel (DT "xiaomi q200 42 02 0a cmd mode dsc
 * dsi panel") is on DSI0 (0x09ac0000) and takes DCS 0x51 with two bytes, high byte first
 * (qcom,mdss-dsi-bl-inverted-dbv), 7..0x3fff, 0x1554 = top of the normal range (above it
 * is Xiaomi's high-brightness mode), HS mode. The command goes through the test-pattern
 * FIFO, as dsi_ctrl_hw_cmn_kickoff_fifo_command / reset_cmd_fifo in the SM8850 msm_drm:
 * TPG_CTRL 0x15c = 0x30006, words to 0x17c, COMMAND_MODE_DMA_CTRL 0x3c |= embedded mode,
 * DMA_CMD_LENGTH 0x4c, SW_TRIGGER 0x90, done = INT_CTRL 0x110 bit 0. No memory DMA, so no
 * display SMMU mapping. Boot-arg pandora_dsi_bl=0 keeps the pixel scaling instead.
 */
extern "C" vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
#define SWEET_DSI0_PA           0x09ac0000u
#define SWEET_PANEL_BL_MIN      7u
#define SWEET_PANEL_BL_MAX      0x1554u
static vm_offset_t sweet_dsi;
static int sweet_dsi_bl = -1;           /* -1 unknown, 0 off/failed, 1 working */
static unsigned sweet_dsi_logs;

static inline uint32_t
sweet_dsi_rd(uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(sweet_dsi + off);
	__asm__ volatile ("dmb oshld" ::: "memory");
	return v;
}

static inline void
sweet_dsi_wr(uint32_t off, uint32_t v)
{
	__asm__ volatile ("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(sweet_dsi + off) = v;
	__asm__ volatile ("dsb osh" ::: "memory");
}

/* One command (or several packed) through the test-pattern FIFO; true when DMA done. */
static bool
sweet_dsi_send(const uint32_t *w, unsigned n)
{
	/* clear CMD_MODE_DMA_DONE (write 1), keep the enables */
	sweet_dsi_wr(0x110, (sweet_dsi_rd(0x110) & 0xeeffffffu) | 1u);
	sweet_dsi_wr(0x15c, 0x30006u);
	for (unsigned i = 0; i < n; i++) {
		sweet_dsi_wr(0x17c, w[i]);
	}
	if (n & 1) {
		sweet_dsi_wr(0x17c, 0);         /* odd word count: one zero word more */
	}
	sweet_dsi_wr(0x3c, (sweet_dsi_rd(0x3c) & 0x2bffffffu) | 0x10000000u);
	sweet_dsi_wr(0x390, sweet_dsi_rd(0x390) & ~2u);
	sweet_dsi_wr(0x4c, 4 * n);
	sweet_dsi_wr(0x90, 1);
	bool done = false;
	for (int i = 0; i < 50000 && !done; i++) {
		done = (sweet_dsi_rd(0x110) & 1u) != 0;
		if (!done) {
			IODelay(1);
		}
	}
	sweet_dsi_wr(0x15c, 0);
	sweet_dsi_wr(0x1ec, 1);
	IODelay(1);
	sweet_dsi_wr(0x1ec, 0);
	if (done) {
		sweet_dsi_wr(0x110, (sweet_dsi_rd(0x110) & 0xeeffffffu) | 1u);
	}
	return done;
}

static bool
sweet_dsi_brightness(uint32_t level)
{
	if (sweet_dsi_bl < 0) {
		int on = 1;
		PE_parse_boot_argn("pandora_dsi_bl", &on, sizeof(on));
		sweet_dsi_bl = on ? 1 : 0;
		if (on) {
			sweet_dsi = ml_io_map(SWEET_DSI0_PA, 0x1000);
			if (!sweet_dsi) {
				sweet_dsi_bl = 0;
			} else {
				uint32_t ctrl = sweet_dsi_rd(0x4);
				IOLog("SWEET DSI: DSI0 ctrl %#x dma_ctrl %#x int %#x\n", ctrl,
				    sweet_dsi_rd(0x3c), sweet_dsi_rd(0x110));
				/* DSI_EN (bit 0) and CMD_MODE_EN (bit 2) as ABL left them. */
				if ((ctrl & 5u) != 5u) {
					IOLog("SWEET DSI: DSI0 not enabled in command mode; brightness by pixels\n");
					sweet_dsi_bl = 0;
				}
			}
		}
	}
	if (sweet_dsi_bl != 1) {
		return false;
	}
	level = level < SWEET_PANEL_BL_MIN ? SWEET_PANEL_BL_MIN :
	    (level > SWEET_PANEL_BL_MAX ? SWEET_PANEL_BL_MAX : level);
	/* header: word count 3, 0, DCS long write 0x39, last + long; payload 51 hi lo, pad ff */
	uint32_t cmd[2] = {0xc0390003u,
		           0xff000000u | ((level & 0xffu) << 16) | (((level >> 8) & 0xffu) << 8) | 0x51u};
	bool done = sweet_dsi_send(cmd, 2);
	bool log = sweet_dsi_logs++ < 20 || !done;
	if (log) {
		IOLog("SWEET DSI: brightness %u (%#x) %s\n", level, level, done ? "sent" : "TIMEOUT");
	}
	if (!done) {
		sweet_dsi_bl = 0;       /* fall back to pixel scaling for the rest of the boot */
		return false;
	}
	if (log) {
		/* Read it back: set maximum return packet size 2 (0x37), then DCS read 0x52
		 * (short, last + bus turn-around); the answer lands in RDBK_DATA0..3. */
		/* RDBK_DATA0 then holds 22 hi lo ecc (DCS short read response, 2 bytes). Read
		 * right away and again 100 ms later (frames from the MDP in between): the first
		 * test read the old level 0x394 200 us after the write. */
		uint32_t maxret = 0x80370002u, rd = 0xa0060052u;
		for (int pass = 0; pass < 2; pass++) {
			if (pass) {
				IOSleep(100);
			}
			bool r1 = sweet_dsi_send(&maxret, 1);
			bool r2 = r1 && sweet_dsi_send(&rd, 1);
			IODelay(200);
			uint32_t d0 = sweet_dsi_rd(0x6c);
			IOLog("SWEET DSI: read 0x52 (%s) %d/%d: count %#x ack_err %#x data %08x %08x -> level %u\n",
			    pass ? "+100 ms" : "now", r1, r2, sweet_dsi_rd(0x1d4), sweet_dsi_rd(0x68), d0,
			    sweet_dsi_rd(0x70), (d0 >> 8) & 0xffffu);
		}
	}
	return true;
}

static void
sweet_bl_apply(const OSSymbol *key, OSObject *o, int depth)
{
	uint64_t v = 0;
	bool num = sweet_bl_number(o, &v);
	if (sweet_bl_logs < 60) {
		sweet_bl_logs++;
		if (num) {
			IOLog("SWEET BL: %s%s = %llu (%#llx)\n", depth ? "  " : "", key->getCStringNoCopy(), v, v);
		} else {
			IOLog("SWEET BL: %s%s = <%s>\n", depth ? "  " : "", key->getCStringNoCopy(),
			    o->getMetaClass()->getClassName());
		}
	}
	/* CoreBrightness writes "brightness" 0..65536 (the slider; 32768 at first boot) and
	 * also brightness-nits / backlight-level / DisplayBrightness, which stay 0 or copy it
	 * here: only "brightness" counts. Pixel scale = 256 * sqrt(brightness), at least 32,
	 * so the middle of the slider is not half as bright. */
	uint64_t frac = ~0ULL;      /* 0..65536 */
	if (num && key->isEqualTo("brightness") && depth == 0) {
		frac = v;
	}
	if (frac != ~0ULL) {
		frac = frac > 65536 ? 65536 : frac;
		/* Panel level: square law over the normal range (50 % -> about a quarter). */
		uint32_t level = SWEET_PANEL_BL_MIN + (uint32_t)((uint64_t)(SWEET_PANEL_BL_MAX -
		    SWEET_PANEL_BL_MIN) * frac * frac / (65536ULL * 65536ULL));
		if (sweet_dsi_brightness(level)) {
			sweet_fb_dim = 256;
			return;
		}
		uint32_t r = 0;                 /* isqrt(frac << 16): 0..65536 */
		for (uint64_t bit = 1ULL << 32, x = frac << 16; bit; bit >>= 2) {
			if (x >= r + bit) {
				x -= r + bit;
				r = (uint32_t)((r >> 1) + bit);
			} else {
				r >>= 1;
			}
		}
		uint32_t dim = r >> 8;          /* 0..256 */
		sweet_fb_dim = dim < 32 ? 32 : (dim > 256 ? 256 : dim);
		if (sweet_bl_logs < 60) {
			IOLog("SWEET BL: -> scale %u/256\n", sweet_fb_dim);
		}
		return;
	}
	OSDictionary *d = OSDynamicCast(OSDictionary, o);
	if (d && depth == 0) {
		OSCollectionIterator *it = OSCollectionIterator::withCollection(d);
		const OSSymbol *k;
		while (it && (k = OSDynamicCast(OSSymbol, it->getNextObject()))) {
			sweet_bl_apply(k, d->getObject(k), 1);
		}
		OSSafeReleaseNULL(it);
	}
}

IOReturn
AppleARMBacklight::setProperties(OSObject *properties)
{
	OSDictionary *d = OSDynamicCast(OSDictionary, properties);
	if (!d) {
		return kIOReturnBadArgument;
	}
	OSCollectionIterator *it = OSCollectionIterator::withCollection(d);
	const OSSymbol *k;
	while (it && (k = OSDynamicCast(OSSymbol, it->getNextObject()))) {
		sweet_bl_apply(k, d->getObject(k), 0);
	}
	OSSafeReleaseNULL(it);
	return kIOReturnSuccess;
}
#endif

#if defined(PANDORA)
static void
sweet_fb_present_thread(void *arg, wait_result_t)
{
	static_cast<AppleCLCD *>(arg)->presentLoop();
}
#endif

bool
AppleCLCD::start(IOService *provider)
{
	PE_Video video;

	if (!IOService::start(provider)) {
		return false;
	}
	PE_current_console(&video);
	fPhys = video.v_baseAddr;
	fWidth = (uint32_t)video.v_width;
	fHeight = (uint32_t)video.v_height;
	fRowBytes = (uint32_t)video.v_rowBytes;
	if (fPhys == 0 || fWidth == 0 || fHeight == 0 || fRowBytes < fWidth * 4) {
		IOLog("SWEET FB: no boot framebuffer\n");
		return false;
	}
#if defined(PANDORA)
	pandora_dpu_probe(fPhys);
	/* 0217: with Metal the frames are GPU-written (in memory, not in CPU caches): the pipes can read them.
	 * The display's SMMU route is switched at the first frame that fits (scanPresent), when the log file
	 * is being written, not here. */
	fScanWanted = sweetfb_gpu_frames();
#endif
	int native = 0;
	PE_parse_boot_argn("sweetfb_native", &native, sizeof(native));
	fLogicalW = fWidth;
	fLogicalH = fHeight;
#if defined(PANDORA)
	/*
	 * The 17 Pro panel (1220x2656) is larger than the iPhone X one. Presented at its
	 * own size, backboardd/UIKit have no layout for it and backboardd loops looking
	 * for AppleM2ScalerCSCDriver. Present 1125x2436 and center it (47 px left/right,
	 * 110 px top/bottom black border); the offsets below are then negative.
	 */
	if (!native) {
#else
	if (!native && fWidth <= SWEET_FB_LOGICAL_W && fHeight <= SWEET_FB_LOGICAL_H) {
#endif
		fLogicalW = SWEET_FB_LOGICAL_W;
		fLogicalH = SWEET_FB_LOGICAL_H;
	}
	fOffX = (int32_t)(fLogicalW - fWidth) / 2;
	fOffY = (int32_t)(fLogicalH - fHeight) / 2;
#if defined(PANDORA)
	/*
	 * sweetfb_fill=1: instead of the centered 1125x2436 area with a black border,
	 * scale the presented display to the whole 1220x2656 panel (nearest neighbour,
	 * x 1.084 / y 1.090; the aspect ratios differ by 0.5 %).
	 */
	int fill = 0;
	PE_parse_boot_argn("sweetfb_fill", &fill, sizeof(fill));
	fFill = fill != 0 && fLogicalW != fWidth && fWidth <= 2048;
	for (uint32_t x = 0; fFill && x < fWidth; x++) {
		fFillX[x] = (uint16_t)((uint64_t)x * fLogicalW / fWidth);
	}
#endif
	fLock = IOLockAlloc();
	fClients = OSArray::withCapacity(4);
	fSurface = sweet_surface_create_physical(fPhys, fWidth, fHeight, fRowBytes, SWEET_FB_FORMAT_BGRA);
	fPixels = fSurface != NULL ? fSurface->kernelAddress() : 0;
	fWorkLoop = IOWorkLoop::workLoop();
	if (fLock == NULL || fClients == NULL || fPixels == 0 || fWorkLoop == NULL) {
		IOLog("SWEET FB: setup failed\n");
		OSSafeReleaseNULL(fSurface);    /* stays published: also held by the table */
		OSSafeReleaseNULL(fWorkLoop);
		return false;
	}
	fVsync = IOTimerEventSource::timerEventSource(this, &AppleCLCD::vsyncFired);
	if (fVsync == NULL || fWorkLoop->addEventSource(fVsync) != kIOReturnSuccess) {
		IOLog("SWEET FB: no vsync timer\n");
		return false;
	}
#if defined(FUXI)
	uint32_t hz = 60;
#else
	uint32_t hz = 10;
#endif
	PE_parse_boot_argn("sweetfb_hz", &hz, sizeof(hz));
	fFlushTicks = (hz == 0 || hz >= 60) ? 1 : 60 / hz;
#if defined(FUXI)
	nanoseconds_to_absolutetime(SWEET_FB_VSYNC_US * 1000ULL, &fTickAbs);
	fNextTick = mach_absolute_time() + fTickAbs;
	fStatStart = mach_absolute_time();
	fVsync->wakeAtTime(kIOTimeOptionsWithLeeway, fNextTick, 0);
	IOLog("SWEET FB: present up to %u Hz (every %u ticks)\n", 60 / fFlushTicks, fFlushTicks);
#else
	fVsync->setTimeoutUS(SWEET_FB_VSYNC_US);
#endif

#if defined(PANDORA)
	{
		thread_t t;
		if (kernel_thread_start(sweet_fb_present_thread, this, &t) != KERN_SUCCESS) {
			IOLog("SWEET FB: no present thread\n");
			return false;
		}
		thread_deallocate(t);
	}
#endif
	setProperty(kIOUserClientClassKey, "IOMobileFramebufferUserClient");
	setProperty("external", false);
	setProperty("DisplayPipePlaneBaseAlignment", 64ULL, 32);
	registerService();
#if defined(PANDORA)
	gSweetCLCD = this;
	{
		AppleARMBacklight *bl = OSTypeAlloc(AppleARMBacklight);
		if (bl && bl->init() && bl->attach(this)) {
			bl->publishParameters();
			if (bl->start(this)) {
				bl->registerService();
				IOLog("SWEET BL: AppleARMBacklight published\n");
			} else {
				bl->detach(this);
			}
		}
		OSSafeReleaseNULL(bl);
	}
#endif
	IOLog("SWEET FB: AppleCLCD %ux%u stride %u at 0x%llx, default surface %u, presented as %ux%u\n",
	    fWidth, fHeight, fRowBytes, fPhys, fSurface->id, fLogicalW, fLogicalH);

#if defined(FUXI)
	fuxi_gpu_start(fPhys, fWidth, fHeight, fRowBytes);
	fuxi_perf_start();
#endif
	return true;
}

static void
sweet_fb_rearm(AppleCLCD *fb, IOTimerEventSource *sender)
{
#if defined(FUXI)
	/* Absolute deadlines: the period does not grow by the copy time. */
	uint64_t now = mach_absolute_time();
	fb->fNextTick += fb->fTickAbs;
	if (fb->fNextTick <= now) {
		fb->fNextTick = now + fb->fTickAbs;
	}
	sender->wakeAtTime(kIOTimeOptionsWithLeeway, fb->fNextTick, 0);
#else
	(void)fb;
	sender->setTimeoutUS(SWEET_FB_VSYNC_US);
#endif
}

void
AppleCLCD::vsyncFired(OSObject *owner, IOTimerEventSource *sender)
{
	AppleCLCD *fb = (AppleCLCD *)owner;
	uint64_t now = mach_absolute_time();
	uint64_t count = ++fb->fVsyncCount;
	uint64_t values[6] = {
		(uint32_t)count, (uint32_t)(count >> 32),
		(uint32_t)now, (uint32_t)(now >> 32),
		0, 0,
	};

#if defined(PANDORA)
	/* 0218: frames, the pipes and display off are the present thread's (presentLoop); the tick only notifies. */
	IOLockLock(fb->fLock);
	for (unsigned int i = 0; i < fb->fClients->getCount(); i++) {
		((IOMobileFramebufferUserClient *)fb->fClients->getObject(i))->notify(SWEET_FB_NOTIFY_VSYNC, values, 6);
	}
	IOLockUnlock(fb->fLock);
	sweet_fb_rearm(fb, sender);
	return;
#endif
#if defined(FUXI)
 uint64_t elapsed = 0;
 int completed = sweet_surface_gpu_poll(&elapsed);
 bool gpuBusy = completed == 0;
 if (gpuBusy) fb->fGpuBusyTicks++;
 if (fb->fGpuPending && completed) {
  fb->fGpuPending = false;
  if (completed == 1) {
   fb->fGpuFrames++;
   fb->fGpuTime += elapsed;
   fuxi_display_kick();
  } else {
   fb->fGpuFailed++;
   /* Replay the newest swap through the CPU on this tick. */
   IOLockLock(fb->fLock); fb->fDirty = true; IOLockUnlock(fb->fLock);
  }
 }
#endif
	IOLockLock(fb->fLock);
	for (unsigned int i = 0; i < fb->fClients->getCount(); i++) {
		IOMobileFramebufferUserClient *client =
		    (IOMobileFramebufferUserClient *)fb->fClients->getObject(i);
		client->notify(SWEET_FB_NOTIFY_VSYNC, values, 6);
	}
#if defined(FUXI)
 if (gpuBusy) { /* retain latest pending swap; no copy or black clear yet */
  IOLockUnlock(fb->fLock);
  sweet_fb_rearm(fb, sender);
  return;
 }
#endif
	if (fb->fPowerOff) {
		/* Fade out: halve every channel each vsync, then black, then idle. */
		uint32_t step = fb->fFadeStep;
		if (step <= 6) {
			fb->fFadeStep = step + 1;
		}
		IOLockUnlock(fb->fLock);
#if defined(FUXI)
		if (step == 0) {
			/* Plain stores, no DC ZVA (bzero) on the write-combined mapping. */
			for (uint32_t y = 0; y < fb->fHeight; y++) {
				volatile uint64_t *row = (volatile uint64_t *)(fb->fPixels + (uint64_t)y * fb->fRowBytes);
				for (uint32_t x = 0; x < fb->fWidth / 2; x++) {
					row[x] = 0;
				}
			}
			__asm__ volatile ("dsb sy" ::: "memory");
		}
#else
		if (step <= 6) {
			for (uint32_t y = 0; y < fb->fHeight; y++) {
				volatile uint32_t *row = (volatile uint32_t *)(fb->fPixels + (uint64_t)y * fb->fRowBytes);
				for (uint32_t x = 0; x < fb->fWidth; x++) {
					row[x] = step < 6 ? (row[x] >> 1) & 0x7f7f7f7f : 0;
				}
			}
		}
#endif
		sweet_fb_rearm(fb, sender);
		return;
	}
	bool flush = fb->fDirty && (count % fb->fFlushTicks) == 0;
	uint8_t *rec = NULL;
	if (flush) {
		rec = (uint8_t *)IOMallocData(SWEET_FB_SWAP_REC_SIZE);
		if (rec != NULL) {
			memcpy(rec, fb->fPending, SWEET_FB_SWAP_REC_SIZE);
			fb->fDirty = false;
			fb->fCopying = true;
		}
	}
	IOLockUnlock(fb->fLock);
	if (rec != NULL) {
		fb->present(rec);
		IOFreeData(rec, SWEET_FB_SWAP_REC_SIZE);
		IOLockLock(fb->fLock);
		fb->fCopying = false;
		IOLockWakeup(fb->fLock, &fb->fCopying, false);
		IOLockUnlock(fb->fLock);
	}
	sweet_fb_rearm(fb, sender);
}

void
AppleCLCD::addClient(IOMobileFramebufferUserClient *client)
{
	IOLockLock(fLock);
	fClients->setObject(client);
	IOLockUnlock(fLock);
}

void
AppleCLCD::removeClient(IOMobileFramebufferUserClient *client)
{
	IOLockLock(fLock);
	unsigned int i = fClients->getNextIndexOfObject(client, 0);
	if (i != (unsigned int)-1) {
		fClients->removeObject(i);
	}
	IOLockUnlock(fLock);
}

static uint32_t
get32(const uint8_t *p, uint32_t off)
{
	uint32_t v;
	memcpy(&v, p + off, 4);
	return v;
}

static uint32_t sweet_fb_swaps_logged;
static uint64_t sweet_fb_swap_count;

/* Copy each enabled layer's surface into the panel memory (no scaling). */
/*
 * A swap only records the frame: copying 10 MB into the panel on every
 * swap (backboardd presents at 60 Hz) took the whole CPU. The vsync timer
 * shows the latest frame every fFlushTicks ticks (boot-arg sweetfb_hz,
 * default 10 Hz).
 */
void
AppleCLCD::swap(const uint8_t *rec)
{
#if defined(FUXI)
	fuxi_perf_swap();
#endif
	IOLockLock(fLock);
#if defined(PANDORA)
	/* 0218: the queue holds one swap while the one before it goes on screen (presentLoop); the next one waits here
	 * for room, at most 100 ms, then takes its place (that one is not shown). */
	if (fDirty) {
		uint64_t deadline;
		nanoseconds_to_absolutetime(100 * NSEC_PER_MSEC, &deadline);
		deadline += mach_absolute_time();
		while (fDirty && !fPowerOff && mach_absolute_time() < deadline) {
			IOLockSleepDeadline(fLock, &fDirty, *(AbsoluteTime *)&deadline, THREAD_UNINT);
		}
	}
#endif
	memcpy(fPending, rec, SWEET_FB_SWAP_REC_SIZE);
#if defined(PANDORA)
	fPendingSeq = sweetfb_gpu_frames() ? pandora_gpu_issued() : 0;
	fSwapQueued = get32(rec, 0x50);
#endif
	fStatSwaps++;
	if (fDirty) {
		fStatDropped++;         /* the previous frame was never shown */
	}
	fDirty = true;
#if defined(PANDORA)
	IOLockWakeup(fLock, &fDirty, false);         /* presentLoop */
	IOLockWakeup(fLock, &fSwapShown, false);     /* swapWait(): a swap taken over is done */
#endif
	IOLockUnlock(fLock);
	if ((++sweet_fb_swap_count % 1000) == 0) {
		IOLog("SWEET FB: %llu swaps\n", sweet_fb_swap_count);
	}
}

#if defined(PANDORA)
static void
sweet_scan_hold(SweetSurface *s)
{
	s->retain();
	sweet_surface_use(s, 1);
}

static void
sweet_scan_drop(SweetSurface **s)
{
	if (*s != NULL) {
		sweet_surface_use(*s, -1);
		(*s)->release();
		*s = NULL;
	}
}

/* 0217: the IOVA of S's memory for the display (mapped on first use, kept while it keeps being swapped), or 0. */
uint32_t
AppleCLCD::scanIova(SweetSurface *s)
{
	unsigned slot = SWEET_SCAN_MAPS;
	fScanClock++;
	for (unsigned i = 0; i < SWEET_SCAN_MAPS; i++) {
		if (fScanMaps[i].s == s) {
			fScanMaps[i].used = fScanClock;
			return fScanMaps[i].iova;
		}
	}
	/* A slot: a free one, else the one longest unused that the pipes do not hold. */
	for (unsigned i = 0; i < SWEET_SCAN_MAPS; i++) {
		SweetSurface *t = fScanMaps[i].s;
		if (t == NULL) {
			slot = i;
			break;
		}
		if (t != fScanShown && t != fScanQueued && (slot == SWEET_SCAN_MAPS || fScanMaps[i].used < fScanMaps[slot].used)) {
			slot = i;
		}
	}
	if (slot == SWEET_SCAN_MAPS) {
		return 0;
	}
	if (fScanMaps[slot].s != NULL) {
		pandora_scanout_unmap(fScanMaps[slot].iova, fScanMaps[slot].pages);
		OSSafeReleaseNULL(fScanMaps[slot].s);
	}
	const uint64_t len = s->memory->getLength();
	const uint32_t n = (uint32_t)((len + 0xfff) >> 12);
	uint64_t *pages = (uint64_t *)IOMallocData((vm_size_t)n * sizeof(uint64_t));
	if (pages == NULL) {
		return 0;
	}
	bool ok = true;
	for (uint32_t i = 0; i < n && ok; i++) {
		IOByteCount seg = 0;
		const addr64_t pa = s->memory->getPhysicalSegment((IOByteCount)i << 12, &seg, kIOMemoryMapperNone);
		ok = pa != 0 && (pa & 0xfff) == 0;
		pages[i] = pa;
	}
	const uint32_t iova = ok ? pandora_scanout_map(pages, n) : 0;
	IOFreeData(pages, (vm_size_t)n * sizeof(uint64_t));
	if (iova == 0) {
		return 0;
	}
	s->retain();
	fScanMaps[slot].s = s;
	fScanMaps[slot].iova = iova;
	fScanMaps[slot].pages = n;
	fScanMaps[slot].used = fScanClock;
	IOLog("SWEET FB: scanout maps surface %u (%ux%u, %u pages) at iova %#x\n", s->id, s->width, s->height, n, iova);
	return iova;
}

/*
 * 0217: a frame the pipes take as it is: one layer, the whole presented display, a BGRA surface of exactly that
 * size, the console paused, display on, brightness on the panel (DCS, sweet_fb_dim untouched). True when the
 * pipes were given it.
 */
bool
AppleCLCD::scanPresent(const uint8_t *rec, uint32_t mask)
{
	if (!fScanWanted || !fConsolePaused || fPowerOff || sweet_fb_dim < 256) {
		return false;
	}
	if (!fScanOn) {
		if (fScanTried) {
			return false;
		}
		fScanTried = true;
		fScanOn = pandora_scanout_init() == 0;
		if (!fScanOn) {
			return false;
		}
	}
	uint32_t layer = SWEET_FB_LAYERS, layers = 0;
	for (uint32_t l = 0; l < SWEET_FB_LAYERS; l++) {
		if ((mask & (1u << l)) && get32(rec, 0x54 + 4 * l) != 0) {
			layer = l;
			layers++;
		}
	}
	if (layers != 1) {
		return false;
	}
	const uint8_t *a = rec + 0x64 + 16 * layer, *b = rec + 0xc4 + 16 * layer;
	SweetSurface *s = sweet_surface_copy(get32(rec, 0x54 + 4 * layer));
	const bool ok = s != NULL && s != fSurface && s->memory != NULL && s->pixelFormat == SWEET_FB_FORMAT_BGRA &&
	    s->bytesPerElement == 4 && s->elementWidth == 1 && s->elementHeight == 1 && s->width == fLogicalW &&
	    s->height == fLogicalH && s->bytesPerRow < 0x10000 && (s->offset & 0xf) == 0 &&
	    (uint64_t)s->offset + (uint64_t)s->bytesPerRow * s->height <= s->memory->getLength() &&
	    get32(a, 0) == 0 && get32(a, 4) == 0 && get32(a, 8) == s->width && get32(a, 12) == s->height &&
	    get32(b, 0) == 0 && get32(b, 4) == 0 && get32(b, 8) == fLogicalW && get32(b, 12) == fLogicalH;
	const uint32_t iova = ok ? scanIova(s) : 0;
	if (iova == 0 || !pandora_scanout_show(iova + s->offset, s->bytesPerRow, s->width, s->height, fFill)) {
		if (ok && fScanRefused++ < 8) {
			IOLog("SWEET FB: scanout refused surface %u (map %#x)\n", s->id, iova);
		}
		OSSafeReleaseNULL(s);
		return false;
	}
	/* show() waited for the earlier queued frame's flush: the pipes read that one now, the one before it no more. */
	if (fScanQueued != NULL) {
		sweet_scan_drop(&fScanShown);
		fScanShown = fScanQueued;
		fScanQueued = NULL;
	}
	sweet_scan_hold(s);
	fScanQueued = s;
	fScanQueuedAddr = iova + s->offset;
	fScanToPanel = false;
	s->release();
	return true;
}

/* 0217: the pipes back on the panel memory; the surface holds go once that is on screen (scanTick). */
void
AppleCLCD::scanPanel(void)
{
	if (fScanOn && !fScanToPanel && (fScanShown != NULL || fScanQueued != NULL) && pandora_scanout_panel()) {
		fScanToPanel = true;
	}
}

/* 0217: drop what the pipes no longer read (0218: scanSettle, once they latched a frame). */
void
AppleCLCD::scanTick(void)
{
	if (!fScanOn) {
		return;
	}
	if (fScanToPanel) {
		if (pandora_scanout_current(0)) {
			sweet_scan_drop(&fScanShown);
			sweet_scan_drop(&fScanQueued);
			fScanToPanel = false;
		}
		return;
	}
	if (fScanQueued != NULL && pandora_scanout_current(fScanQueuedAddr)) {
		sweet_scan_drop(&fScanShown);
		fScanShown = fScanQueued;
		fScanQueued = NULL;
	}
}

/*
 * 0218: after a frame went to the pipes (or they went back to the panel memory): until CTL0 has latched it (its flush
 * bits clear at the frame that takes them, one panel refresh at most: 8.3 ms at 120 Hz) and both pipes read it; then
 * the pipes no longer read the frame before it (scanTick lets that go). False when that took over 50 ms.
 */
bool
AppleCLCD::scanSettle(void)
{
	if (!fScanOn || (fScanQueued == NULL && !fScanToPanel)) {
		return true;
	}
	const uint32_t want = fScanToPanel ? 0 : fScanQueuedAddr;
	const uint64_t t0 = mach_absolute_time();
	uint64_t limit;
	nanoseconds_to_absolutetime(50 * NSEC_PER_MSEC, &limit);
	bool on;
	while (!(on = pandora_scanout_flushed() && pandora_scanout_current(want)) && mach_absolute_time() - t0 < limit) {
		delay(250);     /* a sleep, not a spin */
	}
	const uint64_t took = mach_absolute_time() - t0;
	fLatchTime += took;
	fLatchMax = MAX(fLatchMax, took);
	fLatchCount++;
	if (!on) {
		fLatchLate++;
	}
	scanTick();
	return on;
}

/*
 * 0218: frames go to the display here, as through IOMFB's swap queue in iOS: a swap waits in fPending (queued) until
 * the one before it is on screen, is taken (active), waits for the GPU work issued before it (backboardd swaps once
 * its command buffer is scheduled, so that work includes the frame), goes to the pipes or is copied into the panel
 * memory, and is on screen (shown) once the pipes latched it. CoreAnimation draws into its two frame buffers in turn
 * and into one only when SwapWait says the swap that last showed it is off the screen (swapWait), so each swap has
 * to reach the screen soon after it came, not at a later tick. Display off is done here as well.
 */
void
AppleCLCD::presentLoop(void)
{
	uint8_t *rec = (uint8_t *)IOMallocData(SWEET_FB_SWAP_REC_SIZE);
	uint64_t gap = 0, last = 0;
	if (rec == NULL) {
		IOLog("SWEET FB: present thread: no memory\n");
		return;
	}
	if (fFlushTicks > 1) {      /* sweetfb_hz under 60: at most that many frames a second */
		nanoseconds_to_absolutetime((uint64_t)fFlushTicks * SWEET_FB_VSYNC_US * NSEC_PER_USEC, &gap);
	}
	IOLockLock(fLock);
	for (;;) {
		if (fPowerOff && !fOffDone) {
			fOffDone = true;
			IOLockUnlock(fLock);
			displayOff();
			IOLockLock(fLock);
			continue;
		}
		if (fPowerOff || !fDirty) {
			IOLockSleep(fLock, &fDirty, THREAD_UNINT);
			continue;
		}
		memcpy(rec, fPending, SWEET_FB_SWAP_REC_SIZE);
		const uint32_t seq = fPendingSeq, id = fSwapQueued;
		fDirty = false;
		fCopying = true;
		fSwapActive = id;
		IOLockWakeup(fLock, &fDirty, false);        /* swap(): the queue has room */
		IOLockUnlock(fLock);
		if (gap != 0 && mach_absolute_time() < last + gap) {
			clock_delay_until(last + gap);
		}
		if (seq != 0 && pandora_gpu_wait(seq, 0) == ETIMEDOUT) {
			fStatGpuWait++;
			(void)pandora_gpu_wait(seq, 500 * NSEC_PER_MSEC);   /* at most 0.5 s */
		}
		present(rec);
		last = mach_absolute_time();
		(void)scanSettle();
		IOLockLock(fLock);
		fCopying = false;
		fSwapActive = 0;
		fSwapShown = id;
		IOLockWakeup(fLock, &fSwapShown, false);    /* swapWait() */
	}
}

/* 0218: display off (RequestPowerChange 0): the panel memory black (written only, it is write-combined), then the
 * pipes back on it if they read a surface: black on screen, and the surfaces are let go. */
void
AppleCLCD::displayOff(void)
{
	for (uint32_t y = 0; y < fHeight; y++) {
		volatile uint64_t *row = (volatile uint64_t *)(fPixels + (uint64_t)y * fRowBytes);
		for (uint32_t x = 0; x < fWidth / 2; x++) {
			row[x] = 0;
		}
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	scanPanel();
	(void)scanSettle();
}

/*
 * 0218: swap_wait_gated's tests, with the lock held. 0: the swap is off the screen (not queued, not active, not the
 * one shown); 1: it is on screen or was taken over (not queued, not active); 2: nothing queued or active. Display off:
 * all done (swap_wait_gated returns at once then).
 */
bool
AppleCLCD::swapDone(uint32_t id, uint32_t type)
{
	const bool queued = fDirty && id == fSwapQueued, active = fCopying && id == fSwapActive;
	if (fPowerOff) {
		return true;
	}
	switch (type) {
	case 0:
		return id == 0 || (!queued && !active && id != fSwapShown);
	case 1:
		return id == 0 || (!queued && !active);
	default:
		return !fDirty && !fCopying;
	}
}

/*
 * 0218: SwapWait (sel 6) [swap id, options, timeout ms] as swap_wait_gated: options bit 31 asks without waiting
 * (kIOReturnBusy while not done), the low bits are the test (swapDone; 3 and 4 ask whether the display is up: it is).
 * A wait ends at its timeout (kIOReturnTimeout) or, given none, after 5 s (kIOReturnAborted, where iOS clears its
 * swaps).
 */
IOReturn
AppleCLCD::swapWait(uint32_t id, uint32_t options, uint32_t timeoutMs)
{
	const uint32_t type = options & 0x7fffffffu;
	IOReturn ret = type == 4 || type > 4 ? kIOReturnBadArgument : kIOReturnSuccess;
	IOLockLock(fLock);
	fSwapWaits++;
	if (type <= 2 && !swapDone(id, type)) {
		if (options & 0x80000000u) {
			ret = kIOReturnBusy;
			fSwapBusy++;
		} else {
			uint64_t deadline;
			nanoseconds_to_absolutetime((uint64_t)(timeoutMs ? timeoutMs : 5000) * NSEC_PER_MSEC, &deadline);
			deadline += mach_absolute_time();
			fSwapSlept++;
			while (!swapDone(id, type)) {
				if (IOLockSleepDeadline(fLock, &fSwapShown, *(AbsoluteTime *)&deadline, THREAD_UNINT) == THREAD_TIMED_OUT &&
				    !swapDone(id, type)) {
					ret = timeoutMs ? kIOReturnTimeout : kIOReturnAborted;
					fSwapTimeouts++;
					break;
				}
			}
		}
	}
	const bool log = fSwapLogged < 24;
	const uint32_t queued = fDirty ? fSwapQueued : 0, active = fCopying ? fSwapActive : 0, shown = fSwapShown;
	fSwapLogged += log;
	IOLockUnlock(fLock);
	if (log) {
		IOLog("SWEET FB: swap wait %u options %#x timeout %u -> %#x (queued %u active %u shown %u)\n", id, options,
		    timeoutMs, ret, queued, active, shown);
	}
	return ret;
}
#endif

void
AppleCLCD::present(const uint8_t *rec)
{
	/*
	 * NX741J: sweetfb_doublebuffer=1 draws this frame into a back buffer and copies it to the
	 * panel in one go afterwards, so the panel never holds half of one frame and half of the
	 * next while a layer copy is in flight. Off by default.
	 */
	{
		static int db = -1;
		if (db < 0) {
			db = 0;
			PE_parse_boot_argn("sweetfb_doublebuffer", &db, sizeof(db));
		}
		if (db && fBack == 0 && fRowBytes && fHeight) {
			fBackBytes = (size_t)fRowBytes * fHeight;
			fBack = (uintptr_t)IOMallocContiguous(fBackBytes, 0x4000, NULL);
			if (fBack) {
				IOLog("SWEET FB: double buffer %zu bytes at 0x%llx\n", fBackBytes,
				    (unsigned long long)fBack);
			}
		}
		fDraw = fBack ? fBack : fPixels;
	}
	uint32_t mask = get32(rec, 0x104);
	uint64_t t0 = mach_absolute_time();
	bool gpuFrame = false, cpuFrame = false;
	bool shows = false;
#if defined(PANDORA)
	static char layersLast[320];
	static uint32_t layersLogged;
	static int dumpSecs = -1;
	static uint64_t dumpEvery, dumpNext, dumpSeq;
	char layers[320];
	size_t layersLen = 0;
	uint32_t drawnMask = 0;
	uint8_t *dump = NULL;
	uint32_t dumpCap = 0, dumpLen = 4096;
	struct pandora_fbdump_head *dh = NULL;
	layers[0] = 0;
	if (dumpSecs < 0) {
		dumpSecs = 0;
		PE_parse_boot_argn("pandora_fbdump", &dumpSecs, sizeof(dumpSecs));
		nanoseconds_to_absolutetime((uint64_t)dumpSecs * NSEC_PER_SEC, &dumpEvery);
	}
	static int dumpSwap = -1;   /* 0212: pandora_fbdump_swap=N, one dump at backboardd's swap N */
	if (dumpSwap < 0) {
		dumpSwap = 0;
		PE_parse_boot_argn("pandora_fbdump_swap", &dumpSwap, sizeof(dumpSwap));
	}
	if (dumpSwap > 0 ? (fConsolePaused && get32(rec, 0x50) == (uint32_t)dumpSwap)
	    : (dumpSecs > 0 && fConsolePaused && t0 >= dumpNext)) {
		dump = pandora_fbdump_begin(&dumpCap);
		if (dump != NULL && dumpCap > 4096) {
			bzero(dump, 4096);
			dh = (struct pandora_fbdump_head *)dump;
			memcpy(dh->magic, "PANDFBDM", 8);
			dh->version = 1;
			dh->seq = (uint32_t)++dumpSeq;
			dh->scale = 3;
			uint64_t ns;
			absolutetime_to_nanoseconds(t0, &ns);
			dh->uptime_ms = ns / NSEC_PER_MSEC;
			dumpNext = t0 + dumpEvery;
		} else if (dump != NULL) {
			pandora_fbdump_commit(0);
			dump = NULL;
		}
	}
#endif

	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		if ((mask & (1u << layer)) && get32(rec, 0x54 + 4 * layer) != 0) {
			shows = true;
		}
	}
#if !defined(PANDORA)
	shows = true;
#endif
	if (!fConsolePaused && shows) {
#if defined(PANDORA)
		int quiet = 1;
#else
		int quiet = 0;
#endif
		fConsolePaused = true;
		PE_parse_boot_argn("sweetfb_noconsole", &quiet, sizeof(quiet));
		if (quiet != 0) {
			/* Not PE_initialize_console(kPEDisableScreen): its console switch
			 * writes the read-only-late cons_ops_index and panics. */
			sweet_console_muted = 1;
#if defined(PANDORA)
			/* The border around the centered iPhone X area keeps the last console
			 * text otherwise: clear the whole panel once. */
			for (uint32_t y = 0; y < fHeight; y++) {
				bzero((void *)(fDraw + (uint64_t)y * fRowBytes), (size_t)fWidth * 4);
			}
#endif
		}
	}

#if defined(PANDORA)
	/* 0217: the pipes take the frame themselves when they can; else the panel memory and the copy below. */
	const bool scanned = shows && scanPresent(rec, mask);
	if (shows && !scanned) {
		scanPanel();
	}
	if (shows) {
		if (scanned) {
			fScanFrames++;
		} else {
			fScanCopies++;
		}
	}
#endif
	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		uint32_t id = get32(rec, 0x54 + 4 * layer);
		const uint8_t *a = rec + 0x64 + 16 * layer;
		const uint8_t *b = rec + 0xc4 + 16 * layer;
		int32_t sx = (int32_t)get32(a, 0), sy = (int32_t)get32(a, 4);
		int32_t dx = (int32_t)get32(b, 0), dy = (int32_t)get32(b, 4);
		int32_t w = (int32_t)get32(b, 8), h = (int32_t)get32(b, 12);

		if (!(mask & (1u << layer)) || id == 0) {
			continue;
		}
		SweetSurface *s = sweet_surface_copy(id);
#if defined(PANDORA)
		/*
		 * NX741J: CoreAnimation rotates its surfaces. The dirty rectangles in this record
		 * describe the frame just rendered into the *current* surface; when the surface
		 * changes, the rest of the new surface still holds an older frame (or nothing),
		 * and copying only the rectangles leaves those areas stale: the mosaic. Copy the
		 * whole surface whenever the rotation is detected.
		 */
		if (s != NULL && fLastSurf[layer] != id) {
			if (fLastSurf[layer] != 0 && sweet_fb_fullcopy_logged < 8) {
				sweet_fb_fullcopy_logged++;
				IOLog("SWEET FB: layer %u surface %u -> %u, full frame %dx%d\n", layer,
				    fLastSurf[layer], id, s->width, s->height);
			}
			fLastSurf[layer] = id;
			sx = sy = 0;
			dx = dy = 0;
			w = (int32_t)s->width;
			h = (int32_t)s->height;
		}
		if (layersLen < sizeof(layers)) {
			uint32_t f = s ? s->pixelFormat : 0;
			int n = snprintf(layers + layersLen, sizeof(layers) - layersLen,
			    " [%u %c%c%c%c %ux%u a %d,%d %ux%u b %d,%d %dx%d]", layer,
			    (char)(f >> 24), (char)(f >> 16), (char)(f >> 8), (char)f,
			    s ? s->width : 0, s ? s->height : 0, sx, sy, get32(a, 8), get32(a, 12),
			    dx, dy, w, h);
			if (n > 0) {
				layersLen = MIN(layersLen + (size_t)n, sizeof(layers));
			}
		}
		if (dh != NULL && dh->frames == 0 && s != NULL && s != fSurface && s->kernelAddress() &&
		    (s->bytesPerElement == 4 || s->bytesPerElement == 0) && s->memory != NULL &&
		    (uint64_t)s->offset + (uint64_t)s->height * s->bytesPerRow <= s->memory->getLength()) {
			uint32_t ow, oh, n = pandora_fbdump_scale(dump + dumpLen, dumpCap - dumpLen,
			    s->kernelAddress() + s->offset, s->bytesPerRow, s->width, s->height, &ow, &oh);
			if (n) {
				strlcpy(dh->f[0].name, "surface", sizeof(dh->f[0].name));
				dh->f[0].width = ow;
				dh->f[0].height = oh;
				dh->f[0].offset = dumpLen;
				dh->f[0].format = s->pixelFormat;
				dh->frames = 1;
				dumpLen += n;
			}
		}
#endif
		if (sweet_fb_swaps_logged < 16) {
			sweet_fb_swaps_logged++;
			IOLog("SWEET FB: swap %u layer %u surface %u %ux%u fmt 0x%x bpr %u a %d,%d %ux%u b %d,%d %dx%d\n",
			    get32(rec, 0x50), layer, id, s ? s->width : 0, s ? s->height : 0,
			    s ? s->pixelFormat : 0, s ? s->bytesPerRow : 0, sx, sy, get32(a, 8),
			    get32(a, 12), dx, dy, w, h);
		}
#if defined(PANDORA)
		if (scanned) {   /* 0217: on the pipes already, nothing to copy */
			OSSafeReleaseNULL(s);
			drawnMask |= 1u << layer;
			continue;
		}
#endif
		if (s == NULL || s == fSurface) {
			OSSafeReleaseNULL(s);
			continue;
		}
		uintptr_t src = s->kernelAddress();
		uint32_t bpe = s->bytesPerElement ? s->bytesPerElement : 4;
		if (w <= 0 || h <= 0) {
			w = (int32_t)s->width;
			h = (int32_t)s->height;
		}
		if (sx < 0 || sy < 0) {
			sx = sy = 0;
		}
		if (dx < 0 || dy < 0) {
			dx = dy = 0;
		}
#if defined(PANDORA)
		if (fFill) {
			/* Clip in logical space, check the source, then for every panel pixel
			 * whose logical position falls in the layer's rectangle copy that pixel. */
			if (w > (int32_t)s->width - sx) {
				w = (int32_t)s->width - sx;
			}
			if (h > (int32_t)s->height - sy) {
				h = (int32_t)s->height - sy;
			}
			if (w > (int32_t)fLogicalW - dx) {
				w = (int32_t)fLogicalW - dx;
			}
			if (h > (int32_t)fLogicalH - dy) {
				h = (int32_t)fLogicalH - dy;
			}
			uint64_t lastf = (uint64_t)s->offset + (uint64_t)(sy + h - 1) * s->bytesPerRow +
			    (uint64_t)(sx + w) * 4;
			if (src != 0 && bpe == 4 && w > 0 && h > 0 && s->elementWidth == 1 &&
			    s->elementHeight == 1 && s->memory != NULL && lastf <= s->memory->getLength()) {
				uint32_t px0 = (uint32_t)(((uint64_t)dx * fWidth + fLogicalW - 1) / fLogicalW);
				uint32_t px1 = (uint32_t)MIN(((uint64_t)(dx + w) * fWidth + fLogicalW - 1) / fLogicalW,
				    (uint64_t)fWidth);
				uint32_t py0 = (uint32_t)(((uint64_t)dy * fHeight + fLogicalH - 1) / fLogicalH);
				uint32_t py1 = (uint32_t)MIN(((uint64_t)(dy + h) * fHeight + fLogicalH - 1) / fLogicalH,
				    (uint64_t)fHeight);
				uint32_t k = sweet_fb_dim;
				for (uint32_t Y = py0; Y < py1; Y++) {
					uint32_t ly = (uint32_t)((uint64_t)Y * fLogicalH / fHeight);
					const uint32_t *srow = (const uint32_t *)(src + s->offset +
					    (uint64_t)(sy + (int32_t)ly - dy) * s->bytesPerRow);
					uint32_t *drow = (uint32_t *)(fDraw + (uint64_t)Y * fRowBytes);
					if (k >= 256) {
						for (uint32_t X = px0; X < px1; X++) {
							drow[X] = srow[sx + (int32_t)fFillX[X] - dx];
						}
					} else {
						for (uint32_t X = px0; X < px1; X++) {
							drow[X] = sweet_fb_scale(srow[sx + (int32_t)fFillX[X] - dx], k);
						}
					}
				}
				cpuFrame = true;
#if defined(PANDORA)
				drawnMask |= 1u << layer;
#endif
			}
			s->release();
			continue;
		}
#endif
		/* Logical (presented) display coordinates -> panel coordinates. */
		dx -= fOffX;
		dy -= fOffY;
		if (dx < 0) {
			sx -= dx;
			w += dx;
			dx = 0;
		}
		if (dy < 0) {
			sy -= dy;
			h += dy;
			dy = 0;
		}
		if (w > (int32_t)s->width - sx) {
			w = (int32_t)s->width - sx;
		}
		if (h > (int32_t)s->height - sy) {
			h = (int32_t)s->height - sy;
		}
		if (w > (int32_t)fWidth - dx) {
			w = (int32_t)fWidth - dx;
		}
		if (h > (int32_t)fHeight - dy) {
			h = (int32_t)fHeight - dy;
		}
		uint64_t last = (uint64_t)s->offset + (uint64_t)(sy + h - 1) * s->bytesPerRow +
		    (uint64_t)(sx + w) * 4;
		if (w <= 0 || h <= 0 || s->elementWidth != 1 || s->elementHeight != 1 ||
		    s->memory == NULL || last > s->memory->getLength()) {
			src = 0;
		}
		if (src != 0 && bpe == 4) {
			int copied = 0;
#if defined(PANDORA)
			drawnMask |= 1u << layer;
#endif
#if defined(FUXI)
			uint64_t copyStart = mach_absolute_time();
			/* CA's composed frame has one layer. Preserve the CPU path for
			 * multi-layer records rather than submit multiple fences per frame. */
			if ((mask & (mask-1)) == 0)
				copied = sweet_surface_gpu_copy(s, src, sx, sy, dx, dy, w, h);
			if (copied == 2) {
				gpuFrame = true;
				fGpuPending = true;
			}
			copyStart = mach_absolute_time();
#endif
			if (copied != 2) for (int32_t y = 0; y < h; y++) {
#if defined(PANDORA)
				if (sweetfb_gpu_frames()) {
					flush_dcache((vm_offset_t)(src + s->offset + (uint64_t)(sy + y) * s->bytesPerRow + (uint64_t)sx * 4),
					    (unsigned)w * 4, FALSE);
				}
#endif
				memcpy((void *)(fDraw + (uint64_t)(dy + y) * fRowBytes + (uint64_t)dx * 4),
				    (const void *)(src + s->offset + (uint64_t)(sy + y) * s->bytesPerRow + (uint64_t)sx * 4),
				    (size_t)w * 4);
			}
#if defined(FUXI)
			if (copied != 2) {
				cpuFrame = true;
				fCpuTime += mach_absolute_time() - copyStart;
			}
#endif
		}
		s->release();
	}
#if defined(PANDORA)
	/* NX741J: hand the finished back buffer to the panel in one copy. */
	if (fBack && fDraw == fBack) {
		for (uint32_t y = 0; y < fHeight; y++) {
			memcpy((void *)(fPixels + (uint64_t)y * fRowBytes),
			    (const void *)(fBack + (uint64_t)y * fRowBytes), (size_t)fWidth * 4);
		}
	}
#endif
#if defined(PANDORA)
	if (layersLen && strncmp(layers, layersLast, sizeof(layers)) != 0 && layersLogged < 300) {
		layersLogged++;
		strlcpy(layersLast, layers, sizeof(layersLast));
		IOLog("SWEET FB: layers mask 0x%x drawn 0x%x gpu %d:%s\n", mask, drawnMask, gpuFrame,
		    layers);
	}
	if (dh != NULL) {
		/* 0217: a scanned-out frame is not in the panel memory */
		uint32_t ow = 0, oh = 0, n = scanned ? 0 : pandora_fbdump_scale(dump + dumpLen, dumpCap - dumpLen,
		    (uintptr_t)fPixels, fRowBytes, fWidth, fHeight, &ow, &oh);
		if (n) {
			uint32_t i = dh->frames;
			strlcpy(dh->f[i].name, "panel", sizeof(dh->f[i].name));
			dh->f[i].width = ow;
			dh->f[i].height = oh;
			dh->f[i].offset = dumpLen;
			dh->f[i].format = 0;
			dh->frames = i + 1;
			dumpLen += n;
		}
		pandora_fbdump_commit(dumpLen);
	}
#endif
#if defined(FUXI)
	if (!gpuFrame) fuxi_display_kick();
	fCpuFrames += cpuFrame;
	uint64_t t1 = mach_absolute_time(), took = t1 - t0;
	fStatCopy += took;
	fStatCopyMax = took > fStatCopyMax ? took : fStatCopyMax;
	if (++fStatFrames >= 600) {
		uint64_t avg_ns, max_ns, span_ns;
		absolutetime_to_nanoseconds(fStatCopy / fStatFrames, &avg_ns);
		absolutetime_to_nanoseconds(fStatCopyMax, &max_ns);
		absolutetime_to_nanoseconds(t1 - fStatStart, &span_ns);
		IOLockLock(fLock);
		uint32_t swaps = fStatSwaps, dropped = fStatDropped, gpuWait = fStatGpuWait;
		fStatSwaps = fStatDropped = fStatGpuWait = 0;
		IOLockUnlock(fLock);
		IOLog("SWEET FB: stats %u frames in %llu ms, copy avg %llu us max %llu us, "
		    "%u swaps, %u not shown, %u waited for the GPU\n", fStatFrames, span_ns / 1000000,
		    avg_ns / 1000, max_ns / 1000, swaps, dropped, gpuWait);
#if defined(PANDORA)
		uint64_t scanShows = 0, scanFaults = 0;
		pandora_scanout_counts(&scanShows, &scanFaults);
		IOLog("SWEET FB: scanout %s: %u frames scanned out, %u copied; %llu pipe updates, %llu SMMU faults, %u refused\n",
		    fScanOn ? "on" : "off", fScanFrames, fScanCopies, scanShows, scanFaults, fScanRefused);
		fScanFrames = fScanCopies = 0;
		IOLockLock(fLock);
		const uint32_t waits = fSwapWaits, busy = fSwapBusy, slept = fSwapSlept, late = fSwapTimeouts;
		fSwapWaits = fSwapBusy = fSwapSlept = fSwapTimeouts = 0;
		IOLockUnlock(fLock);
		uint64_t latchAvg = 0, latchMax = 0;
		absolutetime_to_nanoseconds(fLatchCount ? fLatchTime / fLatchCount : 0, &latchAvg);
		absolutetime_to_nanoseconds(fLatchMax, &latchMax);
		IOLog("SWEET FB: swap waits %u (%u busy, %u slept, %u timed out); pipes latched in avg %llu us max %llu us "
		    "(%u frames, %u over 50 ms)\n", waits, busy, slept, late, latchAvg / 1000, latchMax / 1000, fLatchCount,
		    fLatchLate);
		fLatchTime = fLatchMax = 0;
		fLatchCount = fLatchLate = 0;
#endif
		uint64_t gpu_ns, cpu_ns;
		absolutetime_to_nanoseconds(fGpuTime, &gpu_ns);
		absolutetime_to_nanoseconds(fCpuTime, &cpu_ns);
		struct fg_times split;
		sweet_surface_gpu_stats(&split);
		uint64_t ticks[] = {split.pin, split.lookup, split.map, split.clean,
		    split.submit, split.wait, split.unmap, split.cpu};
		uint64_t us[8];
		unsigned attempts = split.attempts;
		for (unsigned i = 0; i < 8; i++) {
			uint64_t ns;
			absolutetime_to_nanoseconds(ticks[i], &ns);
			us[i] = attempts ? ns / 1000 / attempts : 0;
		}
		IOLog("FUXI GPU: presents GPU %u CPU %u, GPU retire avg %llu us CPU avg %llu us, "
		    "split pin %llu lookup %llu map+TLB %llu clean %llu submit %llu wait %llu unmap %llu us, "
		    "GPU CPU-time %llu us, cache hit %u miss %u, sleeps %u, 2M %u 64K %u, busy ticks %u failed %u\n",
		    fGpuFrames, fCpuFrames, fGpuFrames ? gpu_ns / 1000 / fGpuFrames : 0ULL,
		    fCpuFrames ? cpu_ns / 1000 / fCpuFrames : 0ULL,
		    us[0], us[1], us[2], us[3], us[4], us[5], us[6], us[7],
		    split.hits, split.misses, split.sleeps, split.blocks, split.groups, fGpuBusyTicks, fGpuFailed);
		fGpuBusyTicks = fGpuFailed = 0;
		fGpuFrames = fCpuFrames = 0;
		fGpuTime = fCpuTime = 0;
		fStatFrames = 0;
		fStatCopy = fStatCopyMax = 0;
		fStatStart = t1;
	}
#endif
}

bool
IOMobileFramebufferUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	return true;
}

bool
IOMobileFramebufferUserClient::start(IOService *provider)
{
	fFB = OSDynamicCast(AppleCLCD, provider);
	if (fFB == NULL || !IOUserClient::start(provider)) {
		return false;
	}
	fFB->addClient(this);
	return true;
}

IOReturn
IOMobileFramebufferUserClient::clientClose(void)
{
	if (fFB != NULL) {
		fFB->removeClient(this);
		IOLockLock(fFB->fLock);
		mach_port_t old = fPort;
		fPort = MACH_PORT_NULL;
		IOLockUnlock(fFB->fLock);
		if (old != MACH_PORT_NULL) {
			releaseNotificationPort(old);
		}
	}
	terminate();
	return kIOReturnSuccess;
}

IOReturn
IOMobileFramebufferUserClient::registerNotificationPort(mach_port_t port, __unused UInt32 type,
    __unused io_user_reference_t refCon)
{
	mach_port_t old;
	IOLockLock(fFB->fLock);
	old = fPort;
	fPort = port;
	IOLockUnlock(fFB->fLock);
	if (old != MACH_PORT_NULL) {
		releaseNotificationPort(old);
	}
	return kIOReturnSuccess;
}

/* Called with the framebuffer lock held. */
void
IOMobileFramebufferUserClient::notify(uint32_t type, const uint64_t *values, uint32_t count)
{
	OSAsyncReference64 ref;

	if (type >= SWEET_FB_NOTIFY_TYPES || fFunc[type] == 0 || fPort == MACH_PORT_NULL) {
		return;
	}
	bzero(ref, sizeof(ref));
	setAsyncReference64(ref, fPort, (mach_vm_address_t)fFunc[type], fRefcon[type], fTask);
	sendAsyncResult64WithOptions(ref, kIOReturnSuccess, (io_user_reference_t *)values, count,
	    kIOUserNotifyOptionCanDrop);
}

static uint64_t sweet_fb_logged;        /* selectors already logged (< 64) */

IOReturn
IOMobileFramebufferUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
	IOReturn ret = kIOReturnSuccess;
	uint64_t out[2] = { 0, 0 };
	uint32_t outCount = 0;

	switch (selector) {
	case kSweetFBGetLayerDefaultSurface:
		out[0] = sweet_surface_port_in_task(fFB->fSurface, fTask);
		outCount = 1;
		break;
	case kSweetFBSwapBegin:
		IOLockLock(fFB->fLock);
		out[0] = ++fFB->fSwapID;
		IOLockUnlock(fFB->fLock);
		outCount = 1;
		break;
	case kSweetFBSwapEnd: {
		uint8_t rec[SWEET_FB_SWAP_REC_SIZE];
		if (args->structureInputDescriptor != NULL ||
		    args->structureInputSize < SWEET_FB_SWAP_REC_SIZE) {
			ret = kIOReturnBadArgument;
			break;
		}
		memcpy(rec, args->structureInput, sizeof(rec));
#if defined(PANDORA)
		sweet_fb_brightness(rec);
#endif
		fFB->swap(rec);
		break;
	}
#if defined(PANDORA)
	case 50:        /* IOMobileFramebufferSetBrightnessCorrection(fb, value): logged only */
		if (args->scalarInputCount >= 2 && sweet_fb_bright_logs++ < 40) {
			IOLog("SWEET FB: brightness correction %#llx %#llx\n", args->scalarInput[0],
			    args->scalarInput[1]);
		}
		break;
#endif
	case kSweetFBGetID:
		out[0] = 0;
		outCount = 1;
		break;
	case kSweetFBGetDisplaySize:
		out[0] = fFB->fLogicalW;
		out[1] = fFB->fLogicalH;
		outCount = 2;
		break;
	case kSweetFBIsMainDisplay:
		out[0] = 1;
		outCount = 1;
		break;
	case kSweetFBGetDotPitch:
		out[0] = SWEET_FB_DOT_PITCH;
		outCount = 1;
		break;
	case kSweetFBNotifications:
		if (args->scalarInputCount >= 3 && args->scalarInput[2] < SWEET_FB_NOTIFY_TYPES) {
			uint32_t type = (uint32_t)args->scalarInput[2];
			IOLockLock(fFB->fLock);
			fFunc[type] = args->scalarInput[0];
			fRefcon[type] = args->scalarInput[1];
			IOLockUnlock(fFB->fLock);
			char name[32];
			proc_selfname(name, sizeof(name));
			IOLog("SWEET FB: %s notifications type %u %s\n", name, type,
			    args->scalarInput[0] ? "on" : "off");
		}
		break;
	case kSweetFBRequestPowerChange:
		if (args->scalarInputCount >= 1) {
			bool off = args->scalarInput[0] == 0;
			IOLockLock(fFB->fLock);
			if (off != fFB->fPowerOff) {
				fFB->fPowerOff = off;
				fFB->fFadeStep = 0;
				fFB->fDirty = true;     /* on: redraw the latest frame */
#if defined(PANDORA)
				fFB->fOffDone = false;
				fFB->fSwapQueued = get32(fFB->fPending, 0x50);
				IOLockWakeup(fFB->fLock, &fFB->fDirty, false);
				IOLockWakeup(fFB->fLock, &fFB->fSwapShown, false);
#endif
			}
			sweet_fb_display_off = off ? 1 : 0;
			IOLockUnlock(fFB->fLock);
			IOLog("SWEET FB: display %s\n", off ? "off" : "on");
		}
		break;
#if defined(PANDORA)
	case kSweetFBSwapWait:  /* kern_SwapWait / kern_SwapWaitWithTimeout: [swap id, options, timeout ms] */
		ret = fFB->swapWait(args->scalarInputCount > 0 ? (uint32_t)args->scalarInput[0] : 0,
		    args->scalarInputCount > 1 ? (uint32_t)args->scalarInput[1] : 0,
		    args->scalarInputCount > 2 ? (uint32_t)args->scalarInput[2] : 0);
		break;
#else
	case kSweetFBSwapWait:
#endif
	default:
		break;
	}

	for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
		args->scalarOutput[i] = i < outCount ? out[i] : 0;
	}
	if (args->structureOutputDescriptor == NULL && args->structureOutput != NULL) {
		bzero(args->structureOutput, args->structureOutputSize);
	}
	if (selector >= 64 || !(sweet_fb_logged & (1ULL << selector))) {
		if (selector < 64) {
			sweet_fb_logged |= 1ULL << selector;
		}
		char name[32];
		proc_selfname(name, sizeof(name));
		IOLog("SWEET FB: %s sel %u in %u/%u out %u/%u -> 0x%x\n", name, selector,
		    args->scalarInputCount, args->structureInputSize,
		    args->scalarOutputCount, args->structureOutputSize, ret);
	}
	return ret;
}
#endif /* SWEET */

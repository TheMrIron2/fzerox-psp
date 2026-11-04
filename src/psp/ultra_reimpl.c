/* libultra OS layer for PSP, modelled on sf64-psp's ultra_reimpl.c */

#include "PR/os.h"
#include "PR/sptask.h"
#include "PR/mbi.h"
#include "PR/piint.h"
#include "src/psp/platform.h"
#include "src/psp/n64ram.h"
#include "src/psp/psp.h"

#include <pspkernel.h>
#include <pspintrman.h>
#include <pspdebug.h>

#define PSP_N64_TICKS_PER_SECOND 46875000ULL
#define PSP_N64_THREAD_STACK_SIZE 0x10000
#define PSP_MAX_TIMERS 64
#define PSP_EEPROM_SIZE 512

/* Globals expected by the libultra headers */
u64 osClockRate = PSP_N64_TICKS_PER_SECOND;
u32 osMemSize = 4 * 1024 * 1024;
s32 osTvType = OS_TV_NTSC;
s32 osResetType = 0;
s32 osRomType = 0;
void* osRomBase;
s32 osCicId = 0;
s32 osVersion = 0;
s32 osAppNMIBuffer[16];
OSIntMask __OSGlobalIntMask = OS_IM_ALL;

static void psp_memcpy(void* dst, const void* src, u32 size) {
    u8* d = (u8*) dst;
    const u8* s = (const u8*) src;

    while (size-- != 0) {
        *d++ = *s++;
    }
}

static void psp_memzero(void* dst, u32 size) {
    u8* d = (u8*) dst;

    while (size-- != 0) {
        *d++ = 0;
    }
}

/* Threads */
typedef struct PspThread {
    OSThread* owner;
    SceUID threadId;
    void (*entry)(void*);
    void* arg;
    volatile int active;
} PspThread;

static PspThread sThreads[16];

static s32 psp_thread_priority_from_os(OSPri pri) {
    const s32 lowest = 0x70;
    const s32 highest = 0x10;

    if (pri <= OS_PRIORITY_IDLE) {
        return lowest;
    }
    if (pri >= OS_PRIORITY_MAX) {
        return highest;
    }

    return lowest - ((pri * (lowest - highest)) / OS_PRIORITY_MAX);
}

static int psp_thread_entry(SceSize args, void* argp) {
    PspThread* thread;

    if (args != sizeof(thread) || argp == NULL) {
        return -1;
    }

    thread = *(PspThread**) argp;
    if (thread != NULL && thread->entry != NULL) {
        thread->entry(thread->arg);
    }
    if (thread != NULL) {
        thread->active = 0;
    }

    return 0;
}

void osCreateThread(OSThread* thread, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri) {
    s32 i;

    (void) sp;

    thread->id = id;
    thread->priority = pri;
    thread->state = OS_STATE_STOPPED;

    for (i = 0; i < (s32) (sizeof(sThreads) / sizeof(sThreads[0])); i++) {
        if (!sThreads[i].active && sThreads[i].owner == NULL) {
            sThreads[i].owner = thread;
            sThreads[i].entry = entry;
            sThreads[i].arg = arg;
            sThreads[i].threadId = -1;
            return;
        }
    }
}

void osStartThread(OSThread* thread) {
    s32 i;

    for (i = 0; i < (s32) (sizeof(sThreads) / sizeof(sThreads[0])); i++) {
        if (sThreads[i].owner == thread) {
            PspThread* threadArg = &sThreads[i];

            sThreads[i].active = 1;
            sThreads[i].threadId = sceKernelCreateThread("n64_thread", psp_thread_entry,
                                                         psp_thread_priority_from_os(thread->priority),
                                                         PSP_N64_THREAD_STACK_SIZE,
                                                         PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
            if (sThreads[i].threadId >= 0) {
                thread->state = OS_STATE_RUNNING;
                sceKernelStartThread(sThreads[i].threadId, sizeof(threadArg), &threadArg);
                PspLog("[os] thread %d start\n", (int) thread->id);
            } else {
                sThreads[i].active = 0;
            }
            return;
        }
    }
}

void osStopThread(OSThread* thread) {
    thread->state = OS_STATE_STOPPED;
}

void osDestroyThread(OSThread* thread) {
    s32 i;

    for (i = 0; i < (s32) (sizeof(sThreads) / sizeof(sThreads[0])); i++) {
        if (sThreads[i].owner == thread) {
            if (sThreads[i].threadId >= 0) {
                sceKernelTerminateDeleteThread(sThreads[i].threadId);
            }
            psp_memzero(&sThreads[i], sizeof(sThreads[i]));
            return;
        }
    }
}

void osYieldThread(void) {
    sceKernelDelayThread(0);
}

OSId osGetThreadId(OSThread* thread) {
    return thread != NULL ? thread->id : 0;
}

void osSetThreadPri(OSThread* thread, OSPri pri) {
    s32 i;

    if (thread == NULL) {
        sceKernelChangeThreadPriority(sceKernelGetThreadId(), psp_thread_priority_from_os(pri));
        return;
    }

    thread->priority = pri;
    for (i = 0; i < (s32) (sizeof(sThreads) / sizeof(sThreads[0])); i++) {
        if (sThreads[i].owner == thread && sThreads[i].threadId >= 0) {
            sceKernelChangeThreadPriority(sThreads[i].threadId, psp_thread_priority_from_os(pri));
            break;
        }
    }
}

OSPri osGetThreadPri(OSThread* thread) {
    return thread != NULL ? thread->priority : 0;
}

/* Timers */
typedef struct PspTimerSlot {
    OSTimer* timer;
    u64 deadlineUs;
    u64 intervalUs;
    OSMesgQueue* mq;
    OSMesg msg;
    int active;
} PspTimerSlot;

static PspTimerSlot sTimerSlots[PSP_MAX_TIMERS];
static SceUID sTimerLock = -1;
static SceUID sTimerWake = -1;
static SceUID sTimerThreadId = -1;
static int sTimerReady;

static u64 psp_ticks_to_us(OSTime ticks) {
    return (ticks / PSP_N64_TICKS_PER_SECOND) * 1000000ULL +
           ((ticks % PSP_N64_TICKS_PER_SECOND) * 1000000ULL) / PSP_N64_TICKS_PER_SECOND;
}

static OSTime psp_us_to_ticks(u64 us) {
    return (OSTime) ((us / 1000000ULL) * PSP_N64_TICKS_PER_SECOND +
                     ((us % 1000000ULL) * PSP_N64_TICKS_PER_SECOND) / 1000000ULL);
}

static int psp_timer_thread(SceSize args, void* argp) {
    (void) args;
    (void) argp;

    for (;;) {
        u64 now;
        u64 nearest = 0;
        int hasNearest = 0;
        int i;

        sceKernelWaitSema(sTimerLock, 1, NULL);
        now = (u64) sceKernelGetSystemTimeWide();

        for (i = 0; i < PSP_MAX_TIMERS; i++) {
            PspTimerSlot* slot = &sTimerSlots[i];

            if (!slot->active) {
                continue;
            }
            if (now >= slot->deadlineUs) {
                osSendMesg(slot->mq, slot->msg, OS_MESG_NOBLOCK);
                if (slot->intervalUs != 0) {
                    do {
                        slot->deadlineUs += slot->intervalUs;
                    } while (slot->deadlineUs <= now);
                } else {
                    slot->active = 0;
                    if (slot->timer != NULL) {
                        slot->timer->next = NULL;
                        slot->timer->prev = NULL;
                        slot->timer->value = 0;
                    }
                    continue;
                }
            }
            if (slot->timer != NULL) {
                slot->timer->value = psp_us_to_ticks(slot->deadlineUs - now);
            }
            if (!hasNearest || slot->deadlineUs < nearest) {
                nearest = slot->deadlineUs;
                hasNearest = 1;
            }
        }
        sceKernelSignalSema(sTimerLock, 1);

        if (hasNearest) {
            u64 delta = nearest > now ? nearest - now : 0;
            SceUInt timeout = (SceUInt) (delta != 0 ? delta : 1);
            sceKernelWaitSema(sTimerWake, 1, &timeout);
        } else {
            sceKernelWaitSema(sTimerWake, 1, NULL);
        }
    }

    return 0;
}

static void psp_timer_init(void) {
    if (sTimerReady) {
        return;
    }

    sTimerLock = sceKernelCreateSema("n64_timer_lock", 0, 1, 1, NULL);
    sTimerWake = sceKernelCreateSema("n64_timer_wake", 0, 0, 1, NULL);
    if (sTimerLock < 0 || sTimerWake < 0) {
        return;
    }

    sTimerThreadId = sceKernelCreateThread("n64_timer", psp_timer_thread, 0x18, 0x2000, PSP_THREAD_ATTR_USER, NULL);
    if (sTimerThreadId < 0) {
        return;
    }
    if (sceKernelStartThread(sTimerThreadId, 0, NULL) < 0) {
        return;
    }

    sTimerReady = 1;
}

OSTime osGetTime(void) {
    u64 us = (u64) sceKernelGetSystemTimeWide();

    return psp_us_to_ticks(us);
}

void osSetTime(OSTime time) {
    (void) time;
}

u32 osGetCount(void) {
    return (u32) osGetTime();
}

int osSetTimer(OSTimer* timer, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    PspTimerSlot* slot = NULL;
    u64 now;
    s32 i;

    if (timer == NULL) {
        return -1;
    }
    psp_timer_init();
    if (!sTimerReady) {
        return -1;
    }

    sceKernelWaitSema(sTimerLock, 1, NULL);

    for (i = 0; i < PSP_MAX_TIMERS; i++) {
        if (sTimerSlots[i].active && sTimerSlots[i].timer == timer) {
            slot = &sTimerSlots[i];
            break;
        }
    }
    if (slot == NULL) {
        for (i = 0; i < PSP_MAX_TIMERS; i++) {
            if (!sTimerSlots[i].active) {
                slot = &sTimerSlots[i];
                break;
            }
        }
    }

    if (slot == NULL) {
        sceKernelSignalSema(sTimerLock, 1);
        return -1;
    }

    now = (u64) sceKernelGetSystemTimeWide();
    slot->timer = timer;
    slot->deadlineUs = now + psp_ticks_to_us(countdown);
    slot->intervalUs = psp_ticks_to_us(interval);
    slot->mq = mq;
    slot->msg = msg;
    slot->active = 1;

    timer->next = NULL;
    timer->prev = NULL;
    timer->interval = interval;
    timer->value = countdown;
    timer->mq = mq;
    timer->msg = msg;

    sceKernelSignalSema(sTimerLock, 1);
    sceKernelSignalSema(sTimerWake, 1);
    return 0;
}

int osStopTimer(OSTimer* timer) {
    s32 i;
    int found = 0;

    if (timer == NULL || !sTimerReady) {
        return -1;
    }

    sceKernelWaitSema(sTimerLock, 1, NULL);
    for (i = 0; i < PSP_MAX_TIMERS; i++) {
        if (sTimerSlots[i].active && sTimerSlots[i].timer == timer) {
            sTimerSlots[i].active = 0;
            found = 1;
            break;
        }
    }
    sceKernelSignalSema(sTimerLock, 1);
    return found ? 0 : -1;
}

/* Cache */
void osWritebackDCacheAll(void) {
    sceKernelDcacheWritebackAll();
}

void osWritebackDCache(void* addr, s32 size) {
    sceKernelDcacheWritebackRange(addr, size);
}

void osInvalDCache(void* addr, s32 size) {
    sceKernelDcacheInvalidateRange(addr, size);
}

void osInvalICache(void* addr, s32 size) {
    (void) addr;
    (void) size;
    sceKernelIcacheInvalidateAll();
}

/* Interrupt masks */
OSIntMask osGetIntMask(void) {
    return __OSGlobalIntMask;
}

OSIntMask osSetIntMask(OSIntMask mask) {
    OSIntMask old = __OSGlobalIntMask;

    __OSGlobalIntMask = mask;
    return old;
}

/* Address translation: N64 KSEG0 range maps into the PSP shadow RAM */
u32 osVirtualToPhysical(void* addr) {
    u32 value = (u32) addr;

    if (value >= 0x80000000u && value < 0x80800000u) {
        return (u32) (u32) (gPspN64Ram + (value - 0x80000000u));
    }
    return (u32) value;
}

void* osPhysicalToVirtual(u32 addr) {
    u32 value = addr;

    if (value >= 0x80000000u && value < 0x80800000u) {
        return gPspN64Ram + (value - 0x80000000u);
    }
    if (value < 0x800000u) {
        return gPspN64Ram + value;
    }
    return (void*) value;
}

/* System */
void osInitialize(void) {
    osTvType = OS_TV_NTSC;
    osResetType = 0;
    psp_timer_init();
}

u32 osGetMemSize(void) {
    return osMemSize;
}

void osExit(void) {
    sceKernelExitGame();
}

void osSetEventMesg(OSEvent event, OSMesgQueue* mq, OSMesg msg) {
    PspPlatform_SetEventMesg(event, mq, msg);
}

/* Video interface */
static OSViMode* sViMode;static void* sViCurrentBuffer;
static void* sViNextBuffer;
static u8 sViBlack;

void osCreateViManager(OSPri pri) {
    (void) pri;
}

void osViSetMode(OSViMode* mode) {
    sViMode = mode;
}

void osViSetSpecialFeatures(u32 func) {
    (void) func;
}

void osViSetXScale(f32 scale) {
    (void) scale;
}

void osViSetYScale(f32 scale) {
    (void) scale;
}

void osViExtendVStart(u32 value) {
    (void) value;
}

void osViFade(u8 active, u16 value) {
    (void) active;
    (void) value;
}

void osViRepeatLine(u8 active) {
    (void) active;
}

void osViBlack(u8 active) {
    sViBlack = active;
}

void osViSwapBuffer(void* frameBufPtr) {
    sViCurrentBuffer = frameBufPtr;
    sViNextBuffer = frameBufPtr;
}

void* osViGetCurrentFramebuffer(void) {
    return sViCurrentBuffer;
}

void* osViGetNextFramebuffer(void) {
    return sViNextBuffer;
}

u32 osViGetCurrentMode(void) {
    return sViMode != NULL ? sViMode->type : 0;
}

u32 osViGetCurrentLine(void) {
    return 0;
}

u32 osViGetCurrentField(void) {
    return 0;
}

u32 osViGetStatus(void) {
    return 0;
}

void osViSetEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
    PspPlatform_SetViEvent(mq, msg, retraceCount);
}

/* Peripheral interface */
static OSPiHandle sCartRomHandle;
static OSPiHandle sDriveRomHandle;
static OSMesgQueue* sPiCmdQueue;

void osCreatePiManager(OSPri pri, OSMesgQueue* cmdQ, OSMesg* cmdBuf, s32 cmdMsgCnt) {
    (void) pri;
    if (cmdQ != NULL && cmdBuf != NULL) {
        osCreateMesgQueue(cmdQ, cmdBuf, cmdMsgCnt);
        sPiCmdQueue = cmdQ;
    }
}

OSPiHandle* osCartRomInit(void) {
    return &sCartRomHandle;
}

OSPiHandle* osDriveRomInit(void) {
    return &sDriveRomHandle;
}

s32 osEPiLinkHandle(OSPiHandle* handle) {
    (void) handle;
    return 0;
}

s32 osEPiDeviceType(OSPiHandle* handle, OSPiInfo* info) {
    (void) handle;
    (void) info;
    return 0;
}

s32 osEPiWriteIo(OSPiHandle* handle, u32 addr, u32 data) {
    (void) handle;
    (void) addr;
    (void) data;
    return 0;
}

s32 osEPiReadIo(OSPiHandle* handle, u32 addr, u32* data) {
    (void) handle;
    (void) addr;
    if (data != NULL) {
        *data = 0;
    }
    return 0;
}

s32 osPiWriteIo(u32 addr, u32 data) {
    (void) addr;
    (void) data;
    return 0;
}

s32 osPiReadIo(u32 addr, u32* data) {
    (void) addr;
    if (data != NULL) {
        *data = 0;
    }
    return 0;
}

u32 osPiGetStatus(void) {
    return 0;
}

s32 osPiGetDeviceType(void) {
    return 0;
}

OSMesgQueue* osPiGetCmdQueue(void) {
    return sPiCmdQueue;
}

s32 osPiStartDma(OSIoMesg* mb, s32 pri, s32 direction, u32 devAddr, void* dramAddr, u32 size, OSMesgQueue* mq) {
    (void) pri;
    (void) direction;

    if (mb == NULL) {
        return -1;
    }

    mb->hdr.retQueue = mq;
    mb->dramAddr = dramAddr;
    mb->devAddr = devAddr;
    mb->size = size;

    /* devAddr is a ROM offset, not a host pointer; no ROM blob is embedded yet.
     * Leave the destination untouched and just complete the transfer. */
    if (mq != NULL) {
        osSendMesg(mq, mb, OS_MESG_NOBLOCK);
    }
    return 0;
}

s32 osEPiStartDma(OSPiHandle* handle, OSIoMesg* mb, s32 direction) {
    (void) handle;
    return osPiStartDma(mb, 0, direction, mb->devAddr, mb->dramAddr, mb->size, mb->hdr.retQueue);
}

s32 osEPiRawStartDma(OSPiHandle* handle, s32 direction, u32 devAddr, void* dramAddr, u32 size) {
    (void) handle;
    (void) direction;
    (void) devAddr;
    (void) dramAddr;
    (void) size;
    return 0;
}

s32 osPiRawStartDma(s32 direction, u32 devAddr, void* dramAddr, u32 size) {
    (void) direction;
    (void) devAddr;
    (void) dramAddr;
    (void) size;
    return 0;
}

s32 osEPiRawWriteIo(OSPiHandle* handle, u32 addr, u32 data) {
    (void) handle;
    (void) addr;
    (void) data;
    return 0;
}

s32 osEPiRawReadIo(OSPiHandle* handle, u32 addr, u32* data) {
    (void) handle;
    (void) addr;
    if (data != NULL) {
        *data = 0;
    }
    return 0;
}

s32 osPiRawWriteIo(u32 addr, u32 data) {
    (void) addr;
    (void) data;
    return 0;
}

s32 osPiRawReadIo(u32 addr, u32* data) {
    (void) addr;
    if (data != NULL) {
        *data = 0;
    }
    return 0;
}

/* Signal processor tasks */
static OSTask* sLoadedTask;

void osSpTaskLoad(OSTask* task) {
    sLoadedTask = task;
}

void osSpTaskStartGo(OSTask* task) {
    OSTask* current = task != NULL ? task : sLoadedTask;

    if (current == NULL) {
        return;
    }
    if (current->t.type == M_AUDTASK) {
        PspPlatform_RunAudioTask(current);
    } else {
        PspPlatform_RunGfxTask(current);
    }
}

void osSpTaskYield(void) {
}

OSYieldResult osSpTaskYielded(OSTask* task) {
    (void) task;
    return 0;
}

/* Display processor */
void osDpSetStatus(u32 data) {
    (void) data;
}

u32 osDpGetStatus(void) {
    return 0;
}

void osDpGetCounters(u32* counters) {
    if (counters != NULL) {
        counters[0] = 0;
        counters[1] = 0;
        counters[2] = 0;
        counters[3] = 0;
    }
}

s32 osDpSetNextBuffer(void* buffer, u64 size) {
    (void) buffer;
    (void) size;
    return 0;
}

/* Audio interface */
u32 osAiGetStatus(void) {
    return 0;
}

u32 osAiGetLength(void) {
    return 0;
}

s32 osAiSetFrequency(u32 freq) {
    return (s32) freq;
}

s32 osAiSetNextBuffer(void* buffer, u32 size) {
    (void) buffer;
    (void) size;
    return 0;
}

/* Controllers */
s32 osContInit(OSMesgQueue* mq, u8* bitpattern, OSContStatus* data) {
    s32 i;

    (void) mq;

    if (bitpattern != NULL) {
        *bitpattern = 1;
    }
    for (i = 0; i < MAXCONTROLLERS; i++) {
        data[i].type = CONT_TYPE_NORMAL;
        data[i].status = 0;
        data[i].errno = (i == 0) ? 0 : CONT_NO_RESPONSE_ERROR;
    }
    return 0;
}

s32 osContReset(OSMesgQueue* mq, OSContStatus* data) {
    u8 bitpattern;

    return osContInit(mq, &bitpattern, data);
}

s32 osContStartReadData(OSMesgQueue* mq) {
    if (mq != NULL) {
        osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    }
    return 0;
}

void osContGetReadData(OSContPad* pad) {
    PspPlatform_PollInput(pad);
}

s32 osContStartQuery(OSMesgQueue* mq) {
    if (mq != NULL) {
        osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    }
    return 0;
}

void osContGetQuery(OSContStatus* data) {
    s32 i;

    for (i = 0; i < MAXCONTROLLERS; i++) {
        data[i].type = CONT_TYPE_NORMAL;
        data[i].status = 0;
        data[i].errno = (i == 0) ? 0 : CONT_NO_RESPONSE_ERROR;
    }
}

s32 osContSetCh(u8 ch) {
    (void) ch;
    return 0;
}

/* EEPROM: session-only store until a persistent save path lands */
static u8 sEeprom[PSP_EEPROM_SIZE];

s32 osEepromProbe(OSMesgQueue* mq) {
    (void) mq;
    return EEPROM_TYPE_4K;
}

s32 osEepromLongRead(OSMesgQueue* mq, u8 address, u8* buffer, int nbytes) {
    s32 offset = (s32) address * EEPROM_BLOCK_SIZE;

    (void) mq;
    if (buffer == NULL || offset < 0 || offset + nbytes > PSP_EEPROM_SIZE) {
        return -1;
    }
    psp_memcpy(buffer, sEeprom + offset, nbytes);
    return 0;
}

s32 osEepromLongWrite(OSMesgQueue* mq, u8 address, u8* buffer, int nbytes) {
    s32 offset = (s32) address * EEPROM_BLOCK_SIZE;

    (void) mq;
    if (buffer == NULL || offset < 0 || offset + nbytes > PSP_EEPROM_SIZE) {
        return -1;
    }
    psp_memcpy(sEeprom + offset, buffer, nbytes);
    return 0;
}

s32 osEepromRead(OSMesgQueue* mq, u8 address, u8* buffer) {
    return osEepromLongRead(mq, address, buffer, EEPROM_BLOCK_SIZE);
}

s32 osEepromWrite(OSMesgQueue* mq, u8 address, u8* buffer) {
    return osEepromLongWrite(mq, address, buffer, EEPROM_BLOCK_SIZE);
}

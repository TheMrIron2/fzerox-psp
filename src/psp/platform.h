#ifndef PSP_PLATFORM_H
#define PSP_PLATFORM_H

#include "src/psp/psp.h"
#include "PR/ultratypes.h"
#include "PR/os_message.h"
#include "PR/os_cont.h"
#include "PR/sptask.h"

void PspPlatform_SetEventMesg(OSEvent event, OSMesgQueue* mq, OSMesg msg);
void PspPlatform_SetViEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount);
void PspPlatform_PollInput(OSContPad* pads);
void PspPlatform_RunGfxTask(OSTask* task);
void PspPlatform_RunAudioTask(OSTask* task);
void PspPlatform_Log(const char* message);

u32 PspPlatform_GetViCount(void);
u32 PspPlatform_GetGfxTaskCount(void);
u32 PspPlatform_GetAudioTaskCount(void);

#endif

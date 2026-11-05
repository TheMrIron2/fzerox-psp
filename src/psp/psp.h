#ifndef PSP_H
#define PSP_H

/* Bootstrap-facing entry points with no PR/PSPSDK type dependencies */

void PspPlatform_Init(void);
void PspSelfTest_Run(void);
void PspLog(const char* fmt, ...);
void PspLog_SetScreen(int enable);

#endif

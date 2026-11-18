#ifndef FZERO_PSP_OVERLAY_H
#define FZERO_PSP_OVERLAY_H
int PspOverlay_Input(unsigned int buttons);
void PspOverlay_Draw(void);
void PspOverlay_RecordFrame(unsigned int workUs, unsigned int syncUs, unsigned int waitUs,
                            unsigned long long presentedUs);
#endif

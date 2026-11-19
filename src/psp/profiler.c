#include "src/psp/profiler.h"
#ifdef PSP_PROFILE
#include <pspiofilemgr.h>
#include <psppower.h>
#include <stdio.h>
#include <string.h>

#define CAPACITY 300
#ifndef PSP_PROFILE_REVISION
#define PSP_PROFILE_REVISION "unknown"
#endif

enum { IDLE, ARMED, RECORDING, STOPPING, SAVING, SAVED, ERROR };
static volatile int sState;
static volatile int sReady;
static PspProfileFrame sFrames[CAPACITY];
static unsigned int sCount;
static unsigned long long sLastStart;
static PspProfileGame sGame;
static struct { void *task; PspProfileGame game; } sTasks[2];
static unsigned int sNextTask;
PspProfileFrame *gPspProfileFrame;
int gPspProfileSample;

int PspProfile_Active(void) { return sState == ARMED || sState == RECORDING; }
const char *PspProfile_Status(void) {
    static const char *names[] = {"IDLE", "ARMED CLOSE SELECT", "RECORDING", "STOPPING", "SAVING", "SAVED", "ERROR"};
    return names[sState];
}
void PspProfile_Control(int close) {
    if (close) { if (sState == ARMED) sReady = 1; return; }
    if (sState == RECORDING) sState = STOPPING;
    else if (sState == ARMED) sState = IDLE;
    else if (sState == IDLE || sState == SAVED || sState == ERROR) { sReady = 0; sState = ARMED; }
}
void PspProfile_GameBegin(void) { memset(&sGame, 0, sizeof(sGame)); sGame.valid = PspProfile_Active(); }
void PspProfile_GameUpdate(unsigned int us) { if (sGame.valid) sGame.update_us += us; }
void PspProfile_GameDraw(unsigned int us) { if (sGame.valid) sGame.draw_us += us; }
void PspProfile_GameWait(unsigned int us) { if (sGame.valid) sGame.backpressure_us += us; }
void PspProfile_GameSubmit(void *task, unsigned int frame, int mode, int course) {
    unsigned int i = sNextTask++ & 1;
    sGame.game_frame = frame; sGame.mode = mode; sGame.course = course;
    sTasks[i].game = sGame;
    sTasks[i].task = task;
}
void PspProfile_FrameBegin(void *task, unsigned int id) {
    unsigned int i;
    unsigned long long now;
    if (sState == ARMED && sReady) { sCount = 0; sLastStart = 0; sState = RECORDING; }
    if (sState != RECORDING && sState != STOPPING) return;
    if (sCount >= CAPACITY) return;
    now = PROFILE_NOW();
    gPspProfileFrame = &sFrames[sCount];
    memset(gPspProfileFrame, 0, sizeof(*gPspProfileFrame));
    gPspProfileFrame->task = id;
    if (sLastStart) gPspProfileFrame->interval_us = (unsigned int)(now - sLastStart);
    sLastStart = now;
    for (i = 0; i < 2; i++) if (sTasks[i].task == task) { gPspProfileFrame->game = sTasks[i].game; sTasks[i].task = 0; break; }
}
static int write_text(int fd, const char *s) { int n = strlen(s); return sceIoWrite(fd, s, n) == n; }
static void save_capture(void) {
    char path[100], line[512];
    const char *dir = "ms0:/PSP/GAME/FZXPROFILE";
    int fd = -1, slot, ok = 1;
    unsigned int i, missing = 0;
    sceIoMkdir(dir, 0777);
    for (slot = 0; slot < 10000; slot++) {
        snprintf(path, sizeof(path), "%s/profile-%04d.csv", dir, slot);
        fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_EXCL, 0666);
        if (fd >= 0) break;
        { SceIoStat st; if (sceIoGetstat(path, &st) < 0) break; }
    }
    if (fd < 0) {
        dir = "ef0:/PSP/GAME/FZXPROFILE"; sceIoMkdir(dir, 0777);
        for (slot = 0; slot < 10000; slot++) {
            SceIoStat st;
            snprintf(path, sizeof(path), "%s/profile-%04d.csv", dir, slot);
            fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_EXCL, 0666);
            if (fd >= 0) break;
            if (sceIoGetstat(path, &st) < 0) break;
        }
    }
    if (fd < 0) { sState = ERROR; return; }
    for (i = 0; i < sCount; i++) if (!sFrames[i].game.valid) missing++;
    snprintf(line, sizeof(line), "# revision=%s,mode=sampled_1_in_16,renderer=gu_ordered_batches,vertex_copy=word,lighting=vertex_cache,texture_writeback=producer,capacity=%d,frames=%u,missing_game=%u,cpu_mhz=%d,bus_mhz=%d,hardware_counters=unavailable_not_sampled,revision_dirty=unknown,stop=%s\n", PSP_PROFILE_REVISION, CAPACITY, sCount, missing, scePowerGetCpuClockFrequencyInt(), scePowerGetBusClockFrequencyInt(), sCount == CAPACITY ? "capacity" : "manual");
    ok &= write_text(fd, line);
    ok &= write_text(fd, "task,interval_us,dl_us,sync_us,present_us,game_valid,game_frame,game_mode,course,update_us,draw_us,backpressure_us,commands,vertices,matrices,triangles,texture_hits,texture_misses,decodes,draws,submitted_vertices,clip_calls,clip_rejects,clipped,reflections,samples,triangle_us,light_us,clip_us,submit_us,texture_binds,texture_bytes,batch_draws,batch_vertices,batch_submit_us,batch_copy_us,lit_vertices,lit_vertices_computed,texture_writebacks\n");
    for (i = 0; i < sCount && ok; i++) {
        PspProfileFrame *r = &sFrames[i]; PspProfileGame *g = &r->game;
        snprintf(line, sizeof(line), "%u,%u,%u,%u,%u,%u,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", r->task,r->interval_us,r->dl_us,r->sync_us,r->present_us,g->valid,g->game_frame,g->mode,g->course,g->update_us,g->draw_us,g->backpressure_us,r->commands,r->vertices,r->matrices,r->triangles,r->texture_hits,r->texture_misses,r->decodes,r->draws,r->submitted_vertices,r->clip_calls,r->clip_rejects,r->clipped,r->reflections,r->samples,r->triangle_us,r->light_us,r->clip_us,r->submit_us,r->texture_binds,r->texture_bytes,r->batch_draws,r->batch_vertices,r->batch_submit_us,r->batch_copy_us,r->lit_vertices,r->lit_vertices_computed,r->texture_writebacks);
        ok &= write_text(fd, line);
    }
    if (sceIoClose(fd) < 0) ok = 0;
    sState = ok ? SAVED : ERROR;
}
void PspProfile_FrameEnd(unsigned int dl, unsigned int sync, unsigned int present) {
    if (!gPspProfileFrame) return;
    gPspProfileFrame->dl_us = dl; gPspProfileFrame->sync_us = sync; gPspProfileFrame->present_us = present;
    gPspProfileFrame = 0;
    sCount++;
    if (sCount == CAPACITY || sState == STOPPING) { sState = SAVING; save_capture(); }
}
#endif

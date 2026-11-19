#ifndef FZX_PSP_PROFILER_H
#define FZX_PSP_PROFILER_H

#ifdef PSP_PROFILE
extern long long sceKernelGetSystemTimeWide(void);
typedef struct PspProfileGame {
    unsigned int valid, update_us, draw_us, backpressure_us, game_frame;
    int mode, course;
} PspProfileGame;
typedef struct PspProfileFrame {
    unsigned int task, interval_us, dl_us, sync_us, present_us;
    PspProfileGame game;
    unsigned int commands, vertices, matrices, triangles, texture_hits, texture_misses;
    unsigned int decodes, draws, submitted_vertices, clip_calls, clip_rejects, clipped, reflections;
    unsigned int samples, triangle_us, light_us, clip_us, submit_us;
    unsigned int texture_binds, texture_bytes;
    unsigned int batch_draws, batch_vertices, batch_submit_us, batch_copy_us;
    unsigned int lit_vertices, lit_vertices_computed;
    unsigned int texture_writebacks;
} PspProfileFrame;
extern PspProfileFrame *gPspProfileFrame;
extern int gPspProfileSample;
void PspProfile_Control(int close);
const char *PspProfile_Status(void);
int PspProfile_Active(void);
void PspProfile_GameBegin(void);
void PspProfile_GameUpdate(unsigned int us);
void PspProfile_GameDraw(unsigned int us);
void PspProfile_GameWait(unsigned int us);
void PspProfile_GameSubmit(void *task, unsigned int frame, int mode, int course);
void PspProfile_FrameBegin(void *task, unsigned int id);
void PspProfile_FrameEnd(unsigned int dl, unsigned int sync, unsigned int present);
#define PROFILE_COUNT(field, amount) do { if (gPspProfileFrame) gPspProfileFrame->field += (amount); } while (0)
#define PROFILE_NOW() ((unsigned long long)sceKernelGetSystemTimeWide())
#else
#define PspProfile_Control(x) ((void)0)
#define PspProfile_Active() 0
#define PspProfile_GameBegin() ((void)0)
#define PspProfile_GameUpdate(x) ((void)0)
#define PspProfile_GameDraw(x) ((void)0)
#define PspProfile_GameWait(x) ((void)0)
#define PspProfile_GameSubmit(a,b,c,d) ((void)0)
#define PspProfile_FrameBegin(a,b) ((void)0)
#define PspProfile_FrameEnd(a,b,c) ((void)0)
#define PROFILE_COUNT(field, amount) ((void)0)
#define PROFILE_NOW() 0ULL
#endif
#endif

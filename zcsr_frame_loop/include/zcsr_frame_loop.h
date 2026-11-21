#ifndef ZCSR_FRAME_LOOP_H
#define ZCSR_FRAME_LOOP_H
/* Runtime frame/lifecycle owner.
 *
 * zcsr_frame_loop owns frame iteration, optional fixed-step accumulation,
 * service ordering and module init/tick/shutdown. It does not create a window,
 * renderer, input backend or UI document. The host (runtime_shell) supplies a
 * generic host pointer to each callback; product-specific context stays there.
 */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zcsr_frame_loop zcsr_frame_loop;

typedef bool (*zcsr_frame_init_fn)(void* host, void* user);
typedef bool (*zcsr_frame_tick_fn)(void* host, double dt, void* user);
typedef void (*zcsr_frame_shutdown_fn)(void* host, void* user);

typedef struct zcsr_frame_module_desc {
    const char* title;
    /* Services tick whenever the loop runs. A non-service module participates
     * only while it is the active module (one render/gameplay module at a time). */
    bool service;
    void* user;
    zcsr_frame_init_fn init;
    zcsr_frame_tick_fn tick;
    zcsr_frame_shutdown_fn shutdown;
} zcsr_frame_module_desc;

zcsr_frame_loop* zcsr_frame_loop_create(size_t capacity);
/* Calls shutdown for initialized modules, then frees the loop. */
void zcsr_frame_loop_destroy(zcsr_frame_loop* loop);

bool zcsr_frame_loop_add_module(zcsr_frame_loop* loop,
                                const zcsr_frame_module_desc* desc);
size_t zcsr_frame_loop_module_count(const zcsr_frame_loop* loop);

/* -1 = no active non-service module (shell home screen). */
bool zcsr_frame_loop_set_active(zcsr_frame_loop* loop, int index);
int zcsr_frame_loop_active(const zcsr_frame_loop* loop);

/* 0 disables fixed-step (dt is passed through). A positive value accumulates
 * elapsed seconds and runs at most 8 substeps per call to avoid spiral-of-death. */
void zcsr_frame_loop_set_fixed_step(zcsr_frame_loop* loop, double seconds);
double zcsr_frame_loop_fixed_step(const zcsr_frame_loop* loop);

/* Runs service modules, then the active module. Lazy-initializes them once.
 * Returns false if any init/tick callback fails; the caller should stop. */
bool zcsr_frame_loop_tick(zcsr_frame_loop* loop, void* host, double elapsed_seconds);

/* Shutdown all initialized modules (idempotent). */
void zcsr_frame_loop_shutdown(zcsr_frame_loop* loop, void* host);

unsigned long long zcsr_frame_loop_frame_index(const zcsr_frame_loop* loop);

#ifdef __cplusplus
}
#endif
#endif

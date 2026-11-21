#include "zcsr_frame_loop.h"

#include <stdlib.h>
#include <string.h>

typedef struct FrameModule {
    zcsr_frame_module_desc desc;
    bool initialized;
} FrameModule;

struct zcsr_frame_loop {
    FrameModule* modules;
    size_t capacity;
    size_t count;
    int active;
    double fixed_step;
    double accumulator;
    unsigned long long frame_index;
};

zcsr_frame_loop* zcsr_frame_loop_create(size_t capacity) {
    zcsr_frame_loop* loop;
    if (capacity == 0u) capacity = 1u;
    if (capacity > 256u) capacity = 256u;
    loop = (zcsr_frame_loop*)calloc(1u, sizeof(*loop));
    if (loop == NULL) return NULL;
    loop->modules = (FrameModule*)calloc(capacity, sizeof(*loop->modules));
    if (loop->modules == NULL) {
        free(loop);
        return NULL;
    }
    loop->capacity = capacity;
    loop->active = -1;
    return loop;
}

void zcsr_frame_loop_shutdown(zcsr_frame_loop* loop, void* host) {
    size_t i;
    if (loop == NULL) return;
    for (i = 0; i < loop->count; ++i) {
        FrameModule* module = &loop->modules[i];
        if (!module->initialized) continue;
        if (module->desc.shutdown != NULL)
            module->desc.shutdown(host, module->desc.user);
        module->initialized = false;
    }
}

void zcsr_frame_loop_destroy(zcsr_frame_loop* loop) {
    if (loop == NULL) return;
    zcsr_frame_loop_shutdown(loop, NULL);
    free(loop->modules);
    free(loop);
}

bool zcsr_frame_loop_add_module(zcsr_frame_loop* loop,
                                const zcsr_frame_module_desc* desc) {
    if (loop == NULL || desc == NULL) return false;
    if (loop->count >= loop->capacity) return false;
    loop->modules[loop->count].desc = *desc;
    loop->modules[loop->count].initialized = false;
    loop->count++;
    return true;
}

size_t zcsr_frame_loop_module_count(const zcsr_frame_loop* loop) {
    return loop != NULL ? loop->count : 0u;
}

bool zcsr_frame_loop_set_active(zcsr_frame_loop* loop, int index) {
    if (loop == NULL) return false;
    if (index >= (int)loop->count) return false;
    if (index < -1) index = -1;
    loop->active = index;
    return true;
}

int zcsr_frame_loop_active(const zcsr_frame_loop* loop) {
    return loop != NULL ? loop->active : -1;
}

void zcsr_frame_loop_set_fixed_step(zcsr_frame_loop* loop, double seconds) {
    if (loop == NULL) return;
    loop->fixed_step = seconds > 0.0 ? seconds : 0.0;
    loop->accumulator = 0.0;
}

double zcsr_frame_loop_fixed_step(const zcsr_frame_loop* loop) {
    return loop != NULL ? loop->fixed_step : 0.0;
}

static bool frame_module_ready(zcsr_frame_loop* loop, FrameModule* module, void* host) {
    if (module->initialized) return true;
    if (module->desc.init != NULL && !module->desc.init(host, module->desc.user))
        return false;
    module->initialized = true;
    return true;
}

static bool frame_run_once(zcsr_frame_loop* loop, void* host, double dt) {
    size_t i;
    for (i = 0; i < loop->count; ++i) {
        FrameModule* module = &loop->modules[i];
        if (!module->desc.service) continue;
        if (!frame_module_ready(loop, module, host)) return false;
        if (module->desc.tick != NULL &&
            !module->desc.tick(host, dt, module->desc.user))
            return false;
    }
    if (loop->active >= 0 && (size_t)loop->active < loop->count) {
        FrameModule* module = &loop->modules[loop->active];
        if (!module->desc.service) {
            if (!frame_module_ready(loop, module, host)) return false;
            if (module->desc.tick != NULL &&
                !module->desc.tick(host, dt, module->desc.user))
                return false;
        }
    }
    loop->frame_index++;
    return true;
}

bool zcsr_frame_loop_tick(zcsr_frame_loop* loop, void* host, double elapsed_seconds) {
    if (loop == NULL) return false;
    if (elapsed_seconds < 0.0) elapsed_seconds = 0.0;
    if (loop->fixed_step > 0.0) {
        size_t steps = 0u;
        loop->accumulator += elapsed_seconds;
        /* Avoid runaway catch-up after a debugger pause. */
        if (loop->accumulator > loop->fixed_step * 8.0)
            loop->accumulator = loop->fixed_step * 8.0;
        while (loop->accumulator >= loop->fixed_step && steps < 8u) {
            if (!frame_run_once(loop, host, loop->fixed_step)) return false;
            loop->accumulator -= loop->fixed_step;
            steps++;
        }
        return true;
    }
    return frame_run_once(loop, host, elapsed_seconds);
}

unsigned long long zcsr_frame_loop_frame_index(const zcsr_frame_loop* loop) {
    return loop != NULL ? loop->frame_index : 0ull;
}

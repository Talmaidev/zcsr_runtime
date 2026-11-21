#include "zcsr_frame_loop.h"
#include "zcsr_task_scheduler.h"

#include <stdio.h>

typedef struct HostState {
    int init_count;
    int tick_count;
    int shutdown_count;
    double last_dt;
} HostState;

static bool module_init(void* host, void* user) {
    HostState* h = (HostState*)host;
    (void)user;
    h->init_count++;
    return true;
}
static bool module_tick(void* host, double dt, void* user) {
    HostState* h = (HostState*)host;
    (void)user;
    h->tick_count++;
    h->last_dt = dt;
    return true;
}
static void module_shutdown(void* host, void* user) {
    HostState* h = (HostState*)host;
    (void)user;
    h->shutdown_count++;
}

static int g_slots[1000];
static void add_one(void* user, int index) {
    (void)user;
    g_slots[index] = index + 1;
}

int main(void) {
    HostState host = {0};
    zcsr_frame_loop* loop = zcsr_frame_loop_create(2);
    zcsr_frame_module_desc a, b;
    zcsr_task_scheduler* tasks;
    if (!loop) return 1;
    a = (zcsr_frame_module_desc){0};
    a.title = "a"; a.service = false; a.init = module_init; a.tick = module_tick;
    a.shutdown = module_shutdown;
    b = (zcsr_frame_module_desc){0};
    b.title = "b"; b.service = true; b.init = module_init; b.tick = module_tick;
    b.shutdown = module_shutdown;
    if (!zcsr_frame_loop_add_module(loop, &a) ||
        !zcsr_frame_loop_add_module(loop, &b)) return 2;

    zcsr_frame_loop_set_active(loop, 0);
    if (!zcsr_frame_loop_tick(loop, &host, 1.0 / 60.0)) return 3;
    if (host.init_count != 2 || host.tick_count != 2) return 4;
    /* Switching active must not re-init and must keep the service ticking. */
    zcsr_frame_loop_set_active(loop, -1);
    if (!zcsr_frame_loop_tick(loop, &host, 1.0 / 60.0)) return 5;
    if (host.tick_count != 3) return 6;
    zcsr_frame_loop_set_active(loop, 0);
    if (!zcsr_frame_loop_tick(loop, &host, 1.0 / 60.0)) return 7;
    if (host.tick_count != 5) return 8;

    zcsr_frame_loop_set_fixed_step(loop, 1.0 / 60.0);
    if (!zcsr_frame_loop_tick(loop, &host, 2.0 / 60.0 + 0.000001)) return 9;
    if (host.tick_count < 7) return 10;

    zcsr_frame_loop_shutdown(loop, &host);
    if (host.shutdown_count != 2) return 11;
    zcsr_frame_loop_destroy(loop);

    tasks = zcsr_task_scheduler_create(4);
    if (!tasks) return 12;
    zcsr_task_scheduler_parallel_for(tasks, 0, 1000, 10, add_one, NULL);
    {
        int i;
        int sum = 0;
        for (i = 0; i < 1000; ++i) sum += g_slots[i];
        if (sum != 500500) return 13;
    }
    zcsr_task_scheduler_destroy(tasks);
    printf("frame_loop/task_scheduler: PASS\n");
    return 0;
}

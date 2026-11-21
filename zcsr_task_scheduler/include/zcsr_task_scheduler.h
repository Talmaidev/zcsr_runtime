#ifndef ZCSR_TASK_SCHEDULER_H
#define ZCSR_TASK_SCHEDULER_H
/* Runtime-owned general thread pool. It has no rendering, UI or file knowledge;
 * modules submit plain function pointers. Runtime shell creates one instance and
 * exposes it through zcsr_runtime_shell_tasks(). */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zcsr_task_scheduler zcsr_task_scheduler;
typedef void (*zcsr_task_fn)(void* user);
typedef void (*zcsr_parallel_fn)(void* user, int index);

/* worker_count <= 0 lets the implementation choose a conservative default.
 * Returns NULL only on allocation/thread-creation failure. */
zcsr_task_scheduler* zcsr_task_scheduler_create(int worker_count);
/* Stops workers, joins them, and frees the queue. Safe on NULL. */
void zcsr_task_scheduler_destroy(zcsr_task_scheduler* scheduler);

size_t zcsr_task_scheduler_worker_count(const zcsr_task_scheduler* scheduler);

/* Non-blocking enqueue. Returns false when the queue is full. */
bool zcsr_task_scheduler_submit(zcsr_task_scheduler* scheduler,
                                zcsr_task_fn fn, void* user);

/* Block until every submitted task has completed. Do not call from a worker. */
void zcsr_task_scheduler_wait(zcsr_task_scheduler* scheduler);

/* Split [begin,end) into grain-sized chunks and run them on the pool. Blocks.
 * If scheduling fails, the remaining chunk runs inline so work is never lost. */
void zcsr_task_scheduler_parallel_for(zcsr_task_scheduler* scheduler,
                                      int begin, int end, int grain,
                                      zcsr_parallel_fn fn, void* user);

#ifdef __cplusplus
}
#endif
#endif

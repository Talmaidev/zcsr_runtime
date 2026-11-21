#include "zcsr_task_scheduler.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#endif

#define ZCSR_TASK_QUEUE_CAP 4096u
#define ZCSR_TASK_MAX_WORKERS 16

typedef struct ZcsrTask {
    zcsr_task_fn fn;
    void* user;
} ZcsrTask;

struct zcsr_task_scheduler {
    ZcsrTask queue[ZCSR_TASK_QUEUE_CAP];
    size_t head;
    size_t tail;
    size_t count;
    size_t pending;
    bool stop;
    int workers;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE work_cv;
    CONDITION_VARIABLE done_cv;
    HANDLE threads[ZCSR_TASK_MAX_WORKERS];
#else
    pthread_mutex_t lock;
    pthread_cond_t work_cv;
    pthread_cond_t done_cv;
    pthread_t threads[ZCSR_TASK_MAX_WORKERS];
#endif
};

#if defined(_WIN32)
static DWORD WINAPI task_worker(LPVOID arg) {
    zcsr_task_scheduler* s = (zcsr_task_scheduler*)arg;
    for (;;) {
        ZcsrTask task;
        EnterCriticalSection(&s->lock);
        while (s->count == 0u && !s->stop)
            SleepConditionVariableCS(&s->work_cv, &s->lock, INFINITE);
        if (s->stop && s->count == 0u) {
            LeaveCriticalSection(&s->lock);
            return 0;
        }
        task = s->queue[s->head];
        s->head = (s->head + 1u) % ZCSR_TASK_QUEUE_CAP;
        s->count--;
        LeaveCriticalSection(&s->lock);
        if (task.fn != NULL) task.fn(task.user);
        EnterCriticalSection(&s->lock);
        if (s->pending > 0u) s->pending--;
        if (s->pending == 0u) WakeAllConditionVariable(&s->done_cv);
        LeaveCriticalSection(&s->lock);
    }
}
#else
static void* task_worker(void* arg) {
    zcsr_task_scheduler* s = (zcsr_task_scheduler*)arg;
    for (;;) {
        ZcsrTask task;
        pthread_mutex_lock(&s->lock);
        while (s->count == 0u && !s->stop)
            pthread_cond_wait(&s->work_cv, &s->lock);
        if (s->stop && s->count == 0u) {
            pthread_mutex_unlock(&s->lock);
            return NULL;
        }
        task = s->queue[s->head];
        s->head = (s->head + 1u) % ZCSR_TASK_QUEUE_CAP;
        s->count--;
        pthread_mutex_unlock(&s->lock);
        if (task.fn != NULL) task.fn(task.user);
        pthread_mutex_lock(&s->lock);
        if (s->pending > 0u) s->pending--;
        if (s->pending == 0u) pthread_cond_broadcast(&s->done_cv);
        pthread_mutex_unlock(&s->lock);
    }
}
#endif

zcsr_task_scheduler* zcsr_task_scheduler_create(int worker_count) {
    zcsr_task_scheduler* s;
    int i;
    if (worker_count <= 0) worker_count = 4;
    if (worker_count > ZCSR_TASK_MAX_WORKERS) worker_count = ZCSR_TASK_MAX_WORKERS;
    s = (zcsr_task_scheduler*)calloc(1u, sizeof(*s));
    if (s == NULL) return NULL;
#if defined(_WIN32)
    InitializeCriticalSection(&s->lock);
    InitializeConditionVariable(&s->work_cv);
    InitializeConditionVariable(&s->done_cv);
#else
    if (pthread_mutex_init(&s->lock, NULL) != 0) {
        free(s);
        return NULL;
    }
    if (pthread_cond_init(&s->work_cv, NULL) != 0) {
        pthread_mutex_destroy(&s->lock);
        free(s);
        return NULL;
    }
    if (pthread_cond_init(&s->done_cv, NULL) != 0) {
        pthread_cond_destroy(&s->work_cv);
        pthread_mutex_destroy(&s->lock);
        free(s);
        return NULL;
    }
#endif
    for (i = 0; i < worker_count; ++i) {
#if defined(_WIN32)
        s->threads[i] = CreateThread(NULL, 0, task_worker, s, 0, NULL);
        if (s->threads[i] == NULL) break;
#else
        if (pthread_create(&s->threads[i], NULL, task_worker, s) != 0) break;
#endif
        s->workers++;
    }
    return s;
}

void zcsr_task_scheduler_destroy(zcsr_task_scheduler* s) {
    int i;
    if (s == NULL) return;
#if defined(_WIN32)
    EnterCriticalSection(&s->lock);
    s->stop = true;
    s->count = 0u;
    s->pending = 0u;
    WakeAllConditionVariable(&s->work_cv);
    WakeAllConditionVariable(&s->done_cv);
    LeaveCriticalSection(&s->lock);
    for (i = 0; i < s->workers; ++i) {
        if (s->threads[i] != NULL) {
            WaitForSingleObject(s->threads[i], INFINITE);
            CloseHandle(s->threads[i]);
        }
    }
    DeleteCriticalSection(&s->lock);
#else
    pthread_mutex_lock(&s->lock);
    s->stop = true;
    s->count = 0u;
    s->pending = 0u;
    pthread_cond_broadcast(&s->work_cv);
    pthread_cond_broadcast(&s->done_cv);
    pthread_mutex_unlock(&s->lock);
    for (i = 0; i < s->workers; ++i) pthread_join(s->threads[i], NULL);
    pthread_cond_destroy(&s->work_cv);
    pthread_cond_destroy(&s->done_cv);
    pthread_mutex_destroy(&s->lock);
#endif
    free(s);
}

size_t zcsr_task_scheduler_worker_count(const zcsr_task_scheduler* s) {
    return s != NULL ? (size_t)s->workers : 0u;
}

bool zcsr_task_scheduler_submit(zcsr_task_scheduler* s, zcsr_task_fn fn, void* user) {
    bool ok = false;
    if (s == NULL || fn == NULL) return false;
    if (s->workers == 0) {
        fn(user);
        return true;
    }
#if defined(_WIN32)
    EnterCriticalSection(&s->lock);
#else
    pthread_mutex_lock(&s->lock);
#endif
    if (s->count < ZCSR_TASK_QUEUE_CAP) {
        s->queue[s->tail].fn = fn;
        s->queue[s->tail].user = user;
        s->tail = (s->tail + 1u) % ZCSR_TASK_QUEUE_CAP;
        s->count++;
        s->pending++;
        ok = true;
#if defined(_WIN32)
        WakeConditionVariable(&s->work_cv);
        LeaveCriticalSection(&s->lock);
#else
        pthread_cond_signal(&s->work_cv);
        pthread_mutex_unlock(&s->lock);
#endif
        return true;
    }
#if defined(_WIN32)
    LeaveCriticalSection(&s->lock);
#else
    pthread_mutex_unlock(&s->lock);
#endif
    return ok;
}

void zcsr_task_scheduler_wait(zcsr_task_scheduler* s) {
    if (s == NULL) return;
#if defined(_WIN32)
    EnterCriticalSection(&s->lock);
    while (s->pending > 0u) SleepConditionVariableCS(&s->done_cv, &s->lock, INFINITE);
    LeaveCriticalSection(&s->lock);
#else
    pthread_mutex_lock(&s->lock);
    while (s->pending > 0u) pthread_cond_wait(&s->done_cv, &s->lock);
    pthread_mutex_unlock(&s->lock);
#endif
}

typedef struct ZcsrRangeTask {
    zcsr_parallel_fn fn;
    void* user;
    int begin;
    int end;
} ZcsrRangeTask;

static void range_task_run(void* user) {
    ZcsrRangeTask* range = (ZcsrRangeTask*)user;
    int i;
    if (range == NULL) return;
    for (i = range->begin; i < range->end; ++i) range->fn(range->user, i);
    free(range);
}

void zcsr_task_scheduler_parallel_for(zcsr_task_scheduler* s,
                                      int begin, int end, int grain,
                                      zcsr_parallel_fn fn, void* user) {
    int i;
    if (fn == NULL || end <= begin) return;
    if (grain <= 0) grain = 1;
    if (s == NULL || s->workers == 0) {
        for (i = begin; i < end; ++i) fn(user, i);
        return;
    }
    /* Bounded chunks: never enqueue more chunks than the queue can hold. */
    {
        int total = end - begin;
        int chunks = (total + grain - 1) / grain;
        if (chunks > (int)ZCSR_TASK_QUEUE_CAP) chunks = (int)ZCSR_TASK_QUEUE_CAP;
        if (chunks < 1) chunks = 1;
        for (i = 0; i < chunks; ++i) {
            int lo = begin + (int)((long long)total * i / chunks);
            int hi = begin + (int)((long long)total * (i + 1) / chunks);
            ZcsrRangeTask* range = (ZcsrRangeTask*)malloc(sizeof(*range));
            if (range == NULL) {
                int k;
                for (k = lo; k < hi; ++k) fn(user, k);
                continue;
            }
            range->fn = fn;
            range->user = user;
            range->begin = lo;
            range->end = hi;
            if (!zcsr_task_scheduler_submit(s, range_task_run, range)) {
                int k;
                for (k = lo; k < hi; ++k) fn(user, k);
                free(range);
            }
        }
    }
    zcsr_task_scheduler_wait(s);
}

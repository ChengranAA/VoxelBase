#include "pool.h"
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

struct ThreadPool {
    int        num;
    pthread_t *threads;
    int        running;
    int        finished_count;   /* num+1 = idle, 0..num = working */
    PoolTask   task_fn;
    void      *task_arg;
    int        task_total;
    volatile int task_next;
    pthread_mutex_t mutex;
    pthread_cond_t  cv;
};

ThreadPool *g_pool = NULL;

static void *worker(void *arg) {
    ThreadPool *p = (ThreadPool *)arg;
    while (1) {
        pthread_mutex_lock(&p->mutex);
        /* Wait until there's work (finished_count < num+1) */
        while (p->finished_count >= p->num + 1 && p->running)
            pthread_cond_wait(&p->cv, &p->mutex);
        if (!p->running) { pthread_mutex_unlock(&p->mutex); break; }

        int num = p->num;
        int total = p->task_total;
        PoolTask fn = p->task_fn;
        void *task_arg = p->task_arg;
        pthread_mutex_unlock(&p->mutex);

        /* Work-stealing: claim chunks atomically */
        uint32_t chunk = (uint32_t)(total / num);
        if (chunk < 1) chunk = 1;
        int idx = __sync_fetch_and_add(&p->task_next, 1);
        while (idx < num) {
            int start = idx * chunk;
            int end = (idx == num - 1) ? total : start + chunk;
            if (start < total)
                fn(task_arg, start, end, idx);
            idx = __sync_fetch_and_add(&p->task_next, 1);
        }

        /* Done working — wait for all to finish */
        pthread_mutex_lock(&p->mutex);
        p->finished_count++;
        if (p->finished_count >= p->num)
            pthread_cond_broadcast(&p->cv);
        /* Wait for pool_submit to reset us */
        while (p->finished_count < p->num + 1 && p->running)
            pthread_cond_wait(&p->cv, &p->mutex);
        pthread_mutex_unlock(&p->mutex);
    }
    return NULL;
}

ThreadPool *pool_create(int num_threads) {
    if (num_threads < 1) {
        num_threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (num_threads < 1) num_threads = 4;
    }
    ThreadPool *p = calloc(1, sizeof(ThreadPool));
    p->num = num_threads;
    p->running = 1;
    p->finished_count = num_threads + 1; /* idle */
    p->threads = calloc(num_threads, sizeof(pthread_t));
    pthread_mutex_init(&p->mutex, NULL);
    pthread_cond_init(&p->cv, NULL);
    for (int i = 0; i < num_threads; i++)
        pthread_create(&p->threads[i], NULL, worker, p);
    return p;
}

void pool_submit(ThreadPool *p, PoolTask fn, void *arg, int total) {
    pthread_mutex_lock(&p->mutex);
    p->task_fn = fn;
    p->task_arg = arg;
    p->task_total = total;
    p->task_next = 0;
    p->finished_count = 0; /* signal: work available */
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->mutex);

    /* Wait for all workers to finish */
    pthread_mutex_lock(&p->mutex);
    while (p->finished_count < p->num)
        pthread_cond_wait(&p->cv, &p->mutex);
    p->finished_count = p->num + 1; /* back to idle */
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->mutex);
}

void pool_destroy(ThreadPool *p) {
    if (!p) return;
    pthread_mutex_lock(&p->mutex);
    p->running = 0;
    p->finished_count = p->num + 1;
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->mutex);
    for (int i = 0; i < p->num; i++)
        pthread_join(p->threads[i], NULL);
    free(p->threads);
    pthread_mutex_destroy(&p->mutex);
    pthread_cond_destroy(&p->cv);
    free(p);
}

int pool_thread_count(ThreadPool *p) {
    return p ? p->num : 0;
}

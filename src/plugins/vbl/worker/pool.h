#ifndef VBL_POOL_H
#define VBL_POOL_H

typedef void (*PoolTask)(void *arg, int start, int end, int thread_id);

typedef struct ThreadPool ThreadPool;

ThreadPool *pool_create(int num_threads);
void pool_destroy(ThreadPool *p);
void pool_submit(ThreadPool *p, PoolTask fn, void *arg, int total);
int pool_thread_count(ThreadPool *p);

extern ThreadPool *g_pool;

#endif

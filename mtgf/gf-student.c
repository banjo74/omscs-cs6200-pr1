#include "gf-student.h"

#include "steque.h"

#include <assert.h>
#include <memory.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

/////////////////////////////////////////////////////////////
// Concurrent Queue
/////////////////////////////////////////////////////////////

struct QueueTag {
    steque_t        base;
    pthread_mutex_t mutex;
    pthread_cond_t  notEmpty;
};

Queue* queue_create() {
    Queue* out = (Queue*)calloc(1, sizeof(Queue));
    steque_init(&out->base);
    pthread_mutex_init(&out->mutex, NULL);
    pthread_cond_init(&out->notEmpty, NULL);
    return out;
}

void queue_enqueue_n(Queue* const q, QueueItem* const items, size_t const n) {
    if (n == 0) {
        return;
    }
    pthread_mutex_lock(&q->mutex);
    for (size_t i = 0; i < n; ++i) {
        steque_enqueue(&q->base, items[i]);
    }
    pthread_mutex_unlock(&q->mutex);
    if (n == 1) {
        pthread_cond_signal(&q->notEmpty);
    } else {
        pthread_cond_broadcast(&q->notEmpty);
    }
}

void queue_enqueue(Queue* const q, QueueItem item) {
    queue_enqueue_n(q, &item, 1);
}

QueueItem queue_dequeue(Queue* q) {
    pthread_mutex_lock(&q->mutex);
    while (steque_isempty(&q->base)) {
        pthread_cond_wait(&q->notEmpty, &q->mutex);
    }
    QueueItem const out = steque_pop(&q->base);
    pthread_mutex_unlock(&q->mutex);
    return out;
}

bool queue_empty(Queue const* q) {
    pthread_mutex_lock((pthread_mutex_t*)&q->mutex);
    bool const out = steque_isempty((steque_t*)&q->base);
    pthread_mutex_unlock((pthread_mutex_t*)&q->mutex);
    return out;
}

void queue_destroy(Queue* q) {
    pthread_mutex_lock(&q->mutex);
    steque_destroy(&q->base);
    pthread_cond_destroy(&q->notEmpty);
    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
    free(q);
}

/////////////////////////////////////////////////////////////
// Worker Pool
/////////////////////////////////////////////////////////////

typedef struct {
    WpWork work;
    void*  workerData;
    Queue* queue;
} WpWorker;

static void* const worker_pill_ = (void*)UINTPTR_MAX;

static void worker_init_(WpWorker*    worker,
                         WpWork       work,
                         void* const  workerData,
                         Queue* const queue) {
    memset(worker, 0, sizeof(WpWorker));
    worker->work       = work;
    worker->workerData = workerData;
    worker->queue      = queue;
}

static void worker_do_task_(WpWorker* const worker, WpTask task) {
    worker->work(task, worker->workerData);
}

static void* worker_work_(void* worker_) {
    WpWorker* const worker = (WpWorker*)worker_;
    WpTask          task   = worker_pill_;
    while ((task = queue_dequeue(worker->queue)) != worker_pill_) {
        worker_do_task_(worker, task);
    }
    return NULL;
}

struct WorkerPoolTag {
    size_t    numWorkers;
    WpWorker* workers;

    Queue*     queue;
    pthread_t* threads;
};

WorkerPool* wp_start(size_t const       numWorkers,
                     WpWork             work,
                     WpCreateWorkerData createWorkerData,
                     void* const        globalData) {
    assert(numWorkers > 0);
    WorkerPool* out = (WorkerPool*)calloc(1, sizeof(WorkerPool));
    out->numWorkers = numWorkers;
    out->workers    = (WpWorker*)calloc(out->numWorkers, sizeof(WpWorker));
    out->queue      = queue_create();
    out->threads    = (pthread_t*)calloc(out->numWorkers, sizeof(pthread_t));
    for (size_t i = 0; i < out->numWorkers; ++i) {
        void* const workerData =
            createWorkerData ? createWorkerData(globalData) : NULL;
        worker_init_(out->workers + i, work, workerData, out->queue);
        pthread_create(out->threads + i, NULL, worker_work_, out->workers + i);
    }
    return out;
}

void wp_add_tasks(WorkerPool* const wp,
                  WpTask* const     tasks,
                  size_t const      numTasks) {
    queue_enqueue_n(wp->queue, tasks, numTasks);
}

void wp_add_task(WorkerPool* const wp, WpTask const task) {
    queue_enqueue(wp->queue, task);
}

void wp_finish(WorkerPool* const   wp,
               WpDestroyWorkerData destroyWorkerData,
               void*               globalData) {
    WpTask* pills = (WpTask*)calloc(wp->numWorkers, sizeof(WpTask));
    for (size_t i = 0; i < wp->numWorkers; ++i) {
        pills[i] = worker_pill_;
    }
    wp_add_tasks(wp, pills, wp->numWorkers);
    free(pills);
    for (size_t i = 0; i < wp->numWorkers; ++i) {
        pthread_join(wp->threads[i], NULL);
        if (destroyWorkerData) {
            destroyWorkerData(wp->workers[i].workerData, globalData);
        }
    }
    free(wp->threads);
    free(wp->workers);
    queue_destroy(wp->queue);
    free(wp);
}

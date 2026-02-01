#ifndef __GF_STUDENT_H__
#define __GF_STUDENT_H__

#include <sys/types.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////
// Concurrent Queue
/////////////////////////////////////////////////////////////

typedef struct QueueTag Queue;
typedef void*           QueueItem;

// create a concurrent queue
Queue* queue_create();

// atomically push many values onto the queue
void queue_enqueue_n(Queue*, QueueItem*, size_t numItems);

// push a value onto the queue
void queue_enqueue(Queue*, QueueItem);

// block until an item is available on the queue.  remove and return the
// earliest inserted item.
QueueItem queue_dequeue(Queue*);

// return true if empty
bool queue_empty(Queue const*);

// destroy the queue, woe betide anybody trying to enqueue or dequeue at the
// same time.
void queue_destroy(Queue*);

/////////////////////////////////////////////////////////////
// Worker Pool
/////////////////////////////////////////////////////////////

typedef struct WorkerPoolTag WorkerPool;
typedef void*                WpTask;

typedef void* (*WpCreateWorkerData)(void* globalData);
typedef void (*WpDestroyWorkerData)(void* workerData, void* globalData);
typedef void (*WpWork)(WpTask, void* workerData);

// Start a WorkerPool thread pool with numWorkers.
// work is the function to apply to each task.  when the Worker Pool is ready,
// it will invoke work on a task.
//
// Each worker can have its own local data.  This is created with
// createWorkerData and is invoked once, sequentially, for each worker.
WorkerPool* wp_start(size_t             numWorkers,
                     WpWork             work,
                     WpCreateWorkerData createWorkerData,
                     void*              globalData);

// Atomically added numTasks to the set of tasks to be done.  Tasks will be
// *started* in the order in which they were added.
void wp_add_tasks(WorkerPool* wp, WpTask* tasks, size_t numTasks);

// Add one task to the set of tasks to be done.  Tasks will be
// *started* in the order in which they were added.
void wp_add_task(WorkerPool* wp, WpTask);

// Finish processing all tasks and, destroy worker data with destroyWorkerData
// (sequentailly) and destroy the WorkerPool
void wp_finish(WorkerPool*,
               WpDestroyWorkerData destroyWorkerData,
               void*               globalData);

#ifdef __cplusplus
}
#endif

#endif // __GF_STUDENT_H__

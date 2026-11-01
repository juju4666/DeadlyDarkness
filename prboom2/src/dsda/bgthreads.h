// Begotten: a small pool of worker threads for the software renderer (see bgdefer.c for what runs on them).
#ifndef __BGTHREADS_H__
#define __BGTHREADS_H__

#if defined(_MSC_VER)
#define BG_TLS __declspec(thread)
#else
#define BG_TLS __thread
#endif

// how many threads draw (1 = everything on the main thread, as before). Decided once: the begotten_render_threads
// setting (0 = one per logical processor, up to 8; 1 = off) or the BEGOTTEN_THREADS environment variable.
int bgt_Count(void);

// runs fn(ctx, task, worker) for task = 0 .. ntasks - 1, spread over the threads; the caller takes part and the call
// returns when all are done. `worker` is 0 for the calling thread and 1.. for the others.
void bgt_Run(void (*fn)(void* ctx, int task, int worker), void* ctx, int ntasks);

// a lock for the few caches that the threads fill in as they go
void bgt_Lock(void);
void bgt_Unlock(void);

// worker number of the running thread (0 = main)
int bgt_Worker(void);

#endif

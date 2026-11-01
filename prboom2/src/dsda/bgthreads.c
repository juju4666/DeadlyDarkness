// Begotten: a small pool of worker threads for the software renderer. Tasks are handed out one at a time from an
// atomic counter, so a slow task (a strip with many lights on it) does not hold the others up.

#include <stdlib.h>

#include <SDL.h>

#include "dsda/bgthreads.h"
#include "dsda/configuration.h"
#include "lprintf.h"

#define BGT_MAX 8

static int num_threads = -1;                 // threads in all, counting the main one
static SDL_Thread* threads[BGT_MAX];
static SDL_sem* start_sem[BGT_MAX];
static SDL_sem* done_sem;
static SDL_mutex* cache_lock;
static SDL_atomic_t next_task;
static void (*job_fn)(void*, int, int);
static void* job_ctx;
static int job_tasks;
static volatile int running;
static BG_TLS int my_worker;
static int task_count[BGT_MAX], runs, stats_on = -1;       // (a test aid: BEGOTTEN_THREADSTATS)

int bgt_Count(void)
{
  if (num_threads < 0)
  {
    const char* e = getenv("BEGOTTEN_THREADS");
    int n = e ? atoi(e) : dsda_IntConfig(dsda_config_begotten_render_threads);

    if (n <= 0)
      n = SDL_GetCPUCount();
    if (n < 1)
      n = 1;
    if (n > BGT_MAX)
      n = BGT_MAX;
    num_threads = n;
    lprintf(LO_INFO, "BGTHREADS: %d rendering thread%s\n", n, n == 1 ? "" : "s");
  }
  return num_threads;
}

int bgt_Worker(void)
{
  return my_worker;
}

void bgt_Lock(void)
{
  if (cache_lock)
    SDL_LockMutex(cache_lock);
}

void bgt_Unlock(void)
{
  if (cache_lock)
    SDL_UnlockMutex(cache_lock);
}

static void RunTasks(int worker)
{
  for (;;)
  {
    int t = SDL_AtomicAdd(&next_task, 1);

    if (t >= job_tasks)
      break;
    job_fn(job_ctx, t, worker);
    task_count[worker]++;
  }
}

static int WorkerMain(void* data)
{
  int id = (int) (size_t) data;

  my_worker = id;
  for (;;)
  {
    SDL_SemWait(start_sem[id]);
    RunTasks(id);
    SDL_SemPost(done_sem);
  }
  return 0;
}

static void StartThreads(void)
{
  int i;

  cache_lock = SDL_CreateMutex();
  done_sem = SDL_CreateSemaphore(0);
  for (i = 1; i < num_threads; ++i)
  {
    char name[24];

    start_sem[i] = SDL_CreateSemaphore(0);
    SDL_snprintf(name, sizeof name, "begotten-render-%d", i);
    threads[i] = SDL_CreateThread(WorkerMain, name, (void*) (size_t) i);
    if (!threads[i])
    {
      lprintf(LO_WARN, "BGTHREADS: could not start thread %d: %s\n", i, SDL_GetError());
      num_threads = i;
      break;
    }
    SDL_DetachThread(threads[i]);
  }
  running = 1;
}

void bgt_Run(void (*fn)(void*, int, int), void* ctx, int ntasks)
{
  int i, workers;

  if (ntasks <= 0)
    return;
  if (bgt_Count() <= 1 || ntasks == 1)
  {
    for (i = 0; i < ntasks; ++i)
      fn(ctx, i, 0);
    return;
  }
  if (!running)
    StartThreads();
  workers = num_threads - 1;
  job_fn = fn;
  job_ctx = ctx;
  job_tasks = ntasks;
  SDL_AtomicSet(&next_task, 0);
  SDL_MemoryBarrierRelease();
  for (i = 1; i <= workers; ++i)
    SDL_SemPost(start_sem[i]);
  RunTasks(0);
  for (i = 0; i < workers; ++i)
    SDL_SemWait(done_sem);
  if (stats_on < 0)
    stats_on = getenv("BEGOTTEN_THREADSTATS") != NULL;
  if (stats_on && ++runs % 240 == 0)
  {
    lprintf(LO_INFO, "BGTHREADS: tasks per thread over the last 240 runs:");
    for (i = 0; i < num_threads; ++i)
      lprintf(LO_INFO, " %d", task_count[i]), task_count[i] = 0;
    lprintf(LO_INFO, "\n");
  }
}

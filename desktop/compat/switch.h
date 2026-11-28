//! The handful of libnx names the launcher's sources use, for building it on a PC.
//!
//! It is only on the include path for the desktop target (see ../build-desktop.sh): the Switch build
//! never sees it. Nothing here does anything — the point is that the same sources compile and run off
//! the console, so the UI can be looked at without copying an NRO to the SD card.
#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <pthread.h>

using Result = uint32_t;

static inline Result romfsInit() { return 0; }
static inline void romfsExit() {}
static inline void socketInitializeDefault() {}
static inline void socketExit() {}

typedef enum
{
    AppletType_None = -2,
    AppletType_Application = 0,
    AppletType_SystemApplication = 1,
} AppletType;

static inline AppletType appletGetAppletType() { return AppletType_Application; }

/// The hand-over to the game: on a PC there is nothing to hand over to, so it says what it would
/// have done.
static inline Result envSetNextLoad(const char* path, const char* args)
{
    ::printf("[desktop] would start %s %s\n", path, args);
    return 0;
}

static inline bool envHasNextLoad() { return true; }

#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res) ((res) != 0)

// libnx's mutexes and threads, over the C++ ones, so the worker runs unchanged on a PC.
struct RMutex
{
    pthread_mutex_t mutex;
};

static inline void rmutexInit(RMutex* mutex)
{
    pthread_mutex_init(&mutex->mutex, nullptr);
}

static inline void rmutexLock(RMutex* mutex) { pthread_mutex_lock(&mutex->mutex); }
static inline void rmutexUnlock(RMutex* mutex) { pthread_mutex_unlock(&mutex->mutex); }

typedef struct
{
    pthread_t thread;
    void (*entry)(void*);
    void* arg;
} Thread;

static inline void* threadTrampoline(void* data)
{
    Thread* thread = (Thread*)data;
    thread->entry(thread->arg);
    return nullptr;
}

static inline Result threadCreate(Thread* thread, void (*entry)(void*), void* arg, void* stack,
    size_t size, int priority, int core)
{
    (void)stack;
    (void)size;
    (void)priority;
    (void)core;
    thread->entry = entry;
    thread->arg   = arg;
    return 0;
}

static inline Result threadStart(Thread* thread)
{
    return pthread_create(&thread->thread, nullptr, threadTrampoline, thread) == 0 ? 0 : 1;
}

static inline Result threadWaitForExit(Thread* thread)
{
    pthread_join(thread->thread, nullptr);
    return 0;
}

static inline Result threadClose(Thread* thread)
{
    (void)thread;
    return 0;
}

/// The launcher's worker sleeps with svcSleepThread(nanoseconds).
static inline void svcSleepThread(int64_t nanoseconds)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(nanoseconds / 1000000000LL);
    ts.tv_nsec = (long)(nanoseconds % 1000000000LL);
    nanosleep(&ts, nullptr);
}

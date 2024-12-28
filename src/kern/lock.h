// lock.h - A sleep lock
//

#ifdef LOCK_TRACE
#define TRACE
#endif

#ifdef LOCK_DEBUG
#define DEBUG
#endif

#ifndef _LOCK_H_
#define _LOCK_H_

#include "thread.h"
#include "halt.h"
#include "console.h"
#include "intr.h"

struct lock {
    struct condition cond;
    int tid; // thread holding lock or -1
};

static inline void lock_init(struct lock * lk, const char * name);
static inline void lock_acquire(struct lock * lk);
static inline void lock_release(struct lock * lk);

// INLINE FUNCTION DEFINITIONS
//

static inline void lock_init(struct lock * lk, const char * name) {
    trace("%s(<%s:%p>", __func__, name, lk);
    condition_init(&lk->cond, name);
    lk->tid = -1;
}

static inline void lock_acquire(struct lock * lk) {
    // TODO: FIXME implement this
    trace("Thread <%s:%d> || %s(<%s:%p>", thread_name(running_thread()), running_thread(), __func__, lk->cond.name, lk);
    // while loop makes sure lock is successfully acquired. race condition
    while(1) {
        int saved_intr_state = intr_disable();
        // check if the lock is free
        if (lk->tid == -1) {
            lk->tid = running_thread(); // acquire the lock
            // trace("Thread <%s:%d> || %s(<%s:%p> acquired", thread_name(running_thread()), running_thread(), __func__, lk->cond.name, lk);
            intr_restore(saved_intr_state);
            debug("Thread <%s:%d> acquired lock <%s:%p>",
                thread_name(running_thread()), running_thread(),
                lk->cond.name, lk);
            return;
        }
        // lock is occupied
        intr_restore(saved_intr_state);
        // wait for the lock
        // trace("Thread <%s:%d> || %s(<%s:%p> waiting for lock", thread_name(running_thread()), running_thread(), __func__, lk->cond.name, lk);
        condition_wait(&lk->cond);
    }
    // trace("Thread <%s:%d> || %s(<%s:%p> acquired", thread_name(running_thread()), running_thread(), __func__, lk->cond.name, lk);
}

static inline void lock_release(struct lock * lk) {
    trace("%s(<%s:%p>", __func__, lk->cond.name, lk);

    assert (lk->tid == running_thread());
    
    lk->tid = -1;
    condition_broadcast(&lk->cond);

    debug("Thread <%s:%d> released lock <%s:%p>",
        thread_name(running_thread()), running_thread(),
        lk->cond.name, lk);
}

#endif // _LOCK_H_
// process.h - User process
//

#ifndef _PROCESS_H_
#define _PROCESS_H_

#ifndef PROCESS_IOMAX
#define PROCESS_IOMAX 16
#endif

#include "config.h"
#include "io.h"
#include "thread.h"
#include <stdint.h>
#include "error.h"
#include "memory.h"
#include "elf.h"
#include "halt.h"
#include "csr.h"
#include "config.h"
#include "intr.h"
#include "heap.h"

// EXPORTED TYPE DEFINITIONS
//

struct process {
    int id; // process id of this process
    int tid; // thread id of associated thread
    uintptr_t mtag; // memory space identifier
    struct io_intf * iotab[PROCESS_IOMAX];
};

// EXPORTED VARIABLES DECLARATIONS
//

extern char procmgr_initialized;
extern struct process * proctab[];

// EXPORTED FUNCTION DECLARATIONS
//

// Initializes processes globally by initializing a process structure for the main user process (init).
// The init process should always be assigned process ID (PID) 0.
extern void procmgr_init(void);
// Executes a program referred to by the I/O interface passed in as an argument.
extern int process_exec(struct io_intf * exeio);
// Cleans up after a finished process by reclaiming the resources of the process.
extern void __attribute__ ((noreturn)) process_exit(void);
// Terminate the process by pid, and reclaime the resources of the process.
extern void process_terminate(int pid);
// Returns the process struct associated with the currently running thread.
static inline struct process * current_process(void);
// Returns the process ID of the process associated with the currently running thread.
static inline int current_pid(void);
// Fork a child process
extern struct process * process_fork(int pid);

// INLINE FUNCTION DEFINITIONS
// 

/**
 * static inline struct process * current_process(void);
 *
 * Returns the process struct associated with the currently running thread.
 *
 * Inputs:
 *          None.
 * Outputs:
 *          the process struct that is currently running.
 * Side Effects:
 *          None.
 */
static inline struct process * current_process(void) {
    return thread_process(running_thread());
}

/**
 * static inline int current_pid(void);
 *
 * Returns the process ID of the process associated with the currently running thread.
 *
 * Inputs:
 *          None.
 * Outputs:
 *          the process id that is currently running.
 * Side Effects:
 *          None.
 */
static inline int current_pid(void) {
    return thread_process(running_thread())->id;
}

#endif // _PROCESS_H_

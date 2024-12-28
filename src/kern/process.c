// process.c - user process
//

#include "process.h"

#ifdef PROCESS_TRACE
#define TRACE
#endif

#ifdef PROCESS_DEBUG
#define DEBUG
#endif


// COMPILE-TIME PARAMETERS
//

// NPROC is the maximum number of processes

#ifndef NPROC
#define NPROC 16
#endif

// INTERNAL FUNCTION DECLARATIONS
//

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_PID 0

// The main user process struct

static struct process main_proc;

// A table of pointers to all user processes in the system

struct process * proctab[NPROC] = {
    [MAIN_PID] = &main_proc
};

// EXPORTED GLOBAL VARIABLES
//

char procmgr_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

/**
 * void procmgr_init(void);
 *
 * Initializes processes globally by initializing a process structure for the main user process (init).
 * The init process should always be assigned process ID (PID) 0.
 *
 * Inputs/Outputs/Side Effects:
 *          None.
 */
void procmgr_init(void) {
    trace("%s: initialze the process.", __func__);
    // initialize main_proc struct
    main_proc.id = MAIN_PID;
    main_proc.tid = running_thread();
    main_proc.mtag = active_memory_space();
    //set process for the running thread
    thread_set_process(main_proc.tid, &main_proc);
    // initialize the file descriptor table
    for (int i = 0; i < PROCESS_IOMAX; i++) {
        main_proc.iotab[i] = NULL;
    }

    // mark process as initialized
    procmgr_initialized = 1;
}

/**
 * int process_exec(struct io_intf * exeio);
 *
 * Executes a program referred to by the I/O interface passed in as an argument.
 *
 * Inputs:
 *          exeio - struct io_intf*, pointer of the io interface we need to execute.
 * Outputs:
 *          if process executes, this function should not return
 * Side Effects:
 *          unmap all user pages and allocate new pages to contain new process
 */
int process_exec(struct io_intf * exeio) {
    // get current process, "convert" the current running process to to-be-executed thread
//    struct process *proc = current_process();

    // Step 1: any virtual memory mappings belonging to other user processes should be unmapped.
    memory_unmap_and_free_user();

    // Step 2: a fresh 2nd level (root) page table should be created and initialized with the default mappings for a user process
    // proc->mtag = memory_space_create(proc->id);
    // // switch to the new memory space
    // memory_space_switch(proc->mtag);

    // void* ptr = memory_alloc_page();
    // proc->mtag = ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
    //     pageptr_to_pagenum(ptr);
    // memory_set_range_flags();

    // Step 3: the executable should be loaded from the I/O interface provided as an argument into the mapped pages. (Hint: elf load)
    void (*entry_point)(void);
    int ret = elf_load(exeio, &entry_point);
    if (ret < 0) {
        return ret;
    }
    debug("Pass elf loader with entry_point = %p", entry_point);

    // Step 4: the thread associated with the process needs to be started in user-mode. 
    // (Hint: An assembly function in thrasm.s would be useful here)
    // It is up to the process exec function to fill in the proper values for SPP and SPIE
    // so that sret can properly jump to a userspace function
    intr_disable();                   // disable interrupt
    intptr_t sstatus = csrr_sstatus();
    sstatus &= ~RISCV_SSTATUS_SPP;    // Set SPP bit to 0 (user mode)
    sstatus |= RISCV_SSTATUS_SPIE;    // Set SPIE bit to 1 (interrupt enable)
    csrs_sstatus(sstatus);
    thread_jump_to_user(USER_STACK_VMA, (uintptr_t) entry_point);

    // _thread_finish_jump is noreturn, so we wont reach here
    return 0;
}

/**
 * void __attribute__ ((noreturn)) process_exit(void);
 *
 * Cleans up after a finished process by reclaiming the resources of the process.
 *
 * Inputs/Outputs:
 *          None.
 *
 * Side Effects:
 *          Anything that was associated with the process at initial execution will be released
 *          including memory space, open io interaces, associated kernel thread.
 */
void __attribute__ ((noreturn)) process_exit(void) {
    trace("%s: process %d exits.", __func__, current_process()->id);
    // release memory space
    memory_unmap_and_free_user();
    memory_space_reclaim();
    // terminate current process
    struct process* proc = current_process();
    for (int i = 0; i < PROCESS_IOMAX; i++) {
        if (proc->iotab[i]) {
            proc->iotab[i]->ops->close(proc->iotab[i]);
            proc->iotab[i] = NULL;
        }
    }
    // release the proctab entry
    proctab[proc->id] = NULL;
    // release the thread
    thread_exit();
}

/**
 * struct process* process_fork(int pid);
 *
 * Allocate the child process.
 *
 * Inputs:
 *          pid - int, process if of the parent process.
 *
 * Outputs:
 *          struct process* - allocated child process
 *
 * Side Effects:
 *          None.
 */
struct process* process_fork(int pid) {
    // find an empty slot in the proctab
    int new_pid = -1;
    for (int i = 0; i < NPROC; i++) {
        if (proctab[i] == NULL) {
            new_pid = i;
            break;
        }
    }
    // if no process available return NULL
    if (new_pid == -1) return NULL;

    // create child process
    struct process* child_proc = kmalloc(sizeof(struct process));
    child_proc->id = new_pid;

    // clone parent memory space to child's
    child_proc->mtag = memory_space_clone(child_proc->id);
    // clone iotab
    for (int i = 0; i < PROCESS_IOMAX; i ++) {
        if (proctab[pid]->iotab[i] != NULL) {
            child_proc->iotab[i] = proctab[pid]->iotab[i];
            proctab[pid]->iotab[i]->refcnt = ioref(proctab[pid]->iotab[i]);
        } else {
            child_proc->iotab[i] = NULL;
        }
    }

    proctab[new_pid] = child_proc;

    return child_proc;
}

/**
 * void process_terminate(int pid);
 *
 * Terminate the process by pid, and reclaime the resources of the process.
 *
 * Inputs:
 *          pid - int, id of the process we want to terminate.
 *
 * Outputs:
 *          None.
 *
 * Side Effects:
 *          Anything that was associated with the process at initial execution will be released
 *          including memory space, open io interaces, associated kernel thread.
 *          May panic if there is no thread with such pid
 */
// void process_terminate(int pid) {
//     trace("%s: termiante thread with pid = %d", __func__, pid);
//     // sanity check
//     if (pid < 0 || pid >= NPROC || proctab[pid] == NULL) {
//         panic("No such thread");
//     }
//     // get such process
//     struct process *proc = proctab[pid];

//     for (int i = 0; i < PROCESS_IOMAX; i++) {
//         if (proc->iotab[i]) {
//             proc->iotab[i]->ops->close(proc->iotab[i]);
//             proc->iotab[i] = NULL;
//         }
//     }
//     // release the proctab entry
//     proctab[proc->id] = NULL;
//     // release the thread. how to get its thread? there is no such func thread_exit(tid)

// }
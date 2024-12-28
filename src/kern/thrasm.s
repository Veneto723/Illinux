# thrasm.s - Special functions called from thread.c
#

# struct thread * _thread_swtch(struct thread * resuming_thread)

# Switches from the currently running thread to another thread and returns when
# the current thread is scheduled to run again. Argument /resuming_thread/ is
# the thread to be resumed. Returns a pointer to the previously-scheduled
# thread. This function is called in thread.c. The spelling of swtch is
# historic.

        .text
        .global _thread_swtch
        .type   _thread_swtch, @function

_thread_swtch:

        # We only need to save the ra and s0 - s12 registers. Save them on
        # the stack and then save the stack pointer. Our declaration is:
        #
        #   struct thread * _thread_swtch(struct thread * resuming_thread);
        #
        # The currently running thread is suspended and resuming_thread is
        # restored to execution. swtch returns when execution is switched back
        # to the calling thread. The return value is the previously executing
        # thread. Interrupts are enabled when swtch returns.
        #
        # tp = pointer to struct thread of current thread (to be suspended)
        # a0 = pointer to struct thread of thread to be resumed
        #

        sd      s0, 0*8(tp)
        sd      s1, 1*8(tp)
        sd      s2, 2*8(tp)
        sd      s3, 3*8(tp)
        sd      s4, 4*8(tp)
        sd      s5, 5*8(tp)
        sd      s6, 6*8(tp)
        sd      s7, 7*8(tp)
        sd      s8, 8*8(tp)
        sd      s9, 9*8(tp)
        sd      s10, 10*8(tp)
        sd      s11, 11*8(tp)
        sd      ra, 12*8(tp)
        sd      sp, 13*8(tp)

        mv      tp, a0

        ld      sp, 13*8(tp)
        ld      ra, 12*8(tp)
        ld      s11, 11*8(tp)
        ld      s10, 10*8(tp)
        ld      s9, 9*8(tp)
        ld      s8, 8*8(tp)
        ld      s7, 7*8(tp)
        ld      s6, 6*8(tp)
        ld      s5, 5*8(tp)
        ld      s4, 4*8(tp)
        ld      s3, 3*8(tp)
        ld      s2, 2*8(tp)
        ld      s1, 1*8(tp)
        ld      s0, 0*8(tp)

        ret

        .global _thread_setup
        .type   _thread_setup, @function

# void _thread_setup (
#      struct thread * thr,             in a0
#      void * sp,                       in a1
#      void (*start)(void *, void *),   in a2
#      void * arg)                      in a3
#
# Sets up the initial context for a new thread. The thread will begin execution
# in /start/, receiving the five arguments passed to _thread_set after /start/.

_thread_setup:
        # Write initial register values into struct thread_context, which is the
        # first member of struct thread.

        sd      a1, 13*8(a0)    # Initial sp
        sd      a2, 11*8(a0)    # s11 <- start
        sd      a3, 0*8(a0)     # s0 <- arg 0
        sd      a4, 1*8(a0)     # s1 <- arg 1
        sd      a5, 2*8(a0)     # s2 <- arg 2
        sd      a6, 3*8(a0)     # s3 <- arg 3
        sd      a7, 4*8(a0)     # s4 <- arg 4

        # put address of thread entry glue into t1 and continue execution at 1f

        jal     t0, 1f

        # The glue code below is executed when we first switch into the new thread

        la      ra, thread_exit # child will return to thread_exit
        mv      a0, s0          # get arg argument to child from s0
        mv      a1, s1          # get arg argument to child from s0
        mv      fp, sp          # frame pointer = stack pointer
        jr      s11             # jump to child entry point (in s1)

1:      # Execution of _thread_setup continues here

        sd      t0, 12*8(a0)    # put address of above glue code into ra slot

        ret

        .global _thread_finish_jump
        .type   _thread_finish_jump, @function

# void __attribute__ ((noreturn)) _thread_finish_jump (
#      struct thread_stack_anchor * stack_anchor,
#      uintptr_t usp, uintptr_t upc, ...);
#
# Inputs:
#       a0 - stack_anchor, CURTHR->stack_base
#       a1 - usp, user stack pointer
#       a2 - upc, user program point
# Side Effects:
#
_thread_finish_jump:
        # While in user mode, sscratch points to a struct thread_stack_anchor
        # located at the base of the stack, which contains the current thread
        # pointer and serves as our starting stack pointer.

        # TODO: FIXME your code here
        # Set stvec to the address of _trap_entry_from_umode
        la t0, _trap_entry_from_umode
        csrw stvec, t0
        csrw sscratch, a0# store the stack anchor
        mv sp, a1       # set stack pointer to user stack pointer
        csrw sepc, a2   # set sepc to entry point
        sret            # return to user mode

        .global _thread_finish_fork
        .type   _thread_finish_fork, @function

# void _thread_finish_fork (
#      struct thread * child,
#      const struct trap_frame * parent_tfr);
#
# Inputs:
#       a0 - struct thread*, child thread pointer
#       a1 - const struct trap_frame, parent trap frame
# Side Effects:
#       None.
_thread_finish_fork:
        # Set up stvec to point to the U-mode trap entry handler
        la t0, _trap_entry_from_umode
        csrw stvec, t0
        ld t0, 15*8(a0)         # 15*8(a0) = thread->stack_base
        csrw sscratch, t0

        # restore sstatus and sepc from the trap frame
        ld                            t6, 33*8(a1)
        csrw                          sepc, t6
        ld                            t6, 32*8(a1)
        csrw                          sstatus, t6
        
        sd s0, 0*8(tp)
        sd s1, 1*8(tp)
        sd s2, 2*8(tp)
        sd s3, 3*8(tp)
        sd s4, 4*8(tp)
        sd s5, 5*8(tp)
        sd s6, 6*8(tp)
        sd s7, 7*8(tp)
        sd s8, 8*8(tp)
        sd s9, 9*8(tp)
        sd s10, 10*8(tp)
        sd s11, 11*8(tp)
        sd ra, 12*8(tp)
        sd sp, 13*8(tp)

        mv tp, a0

        # restore registers from the trap frame
        ld                            x31, 31*8(a1)                     # x30 is t6
        ld                            x30, 30*8(a1)                     # x30 is t5
        ld                            x29, 29*8(a1)                     # x29 is t4
        ld                            x28, 28*8(a1)                     # x28 is t3
        ld                            x27, 27*8(a1)                     # x27 is s11
        ld                            x26, 26*8(a1)                     # x26 is s10
        ld                            x25, 25*8(a1)                     # x25 is s9
        ld                            x24, 24*8(a1)                     # x24 is s8
        ld                            x23, 23*8(a1)                     # x23 is s7
        ld                            x22, 22*8(a1)                     # x22 is s6
        ld                            x21, 21*8(a1)                     # x21 is s5
        ld                            x20, 20*8(a1)                     # x20 is s4
        ld                            x19, 19*8(a1)                     # x19 is s3
        ld                            x18, 18*8(a1)                     # x18 is s2
        ld                            x17, 17*8(a1)                     # x17 is a7
        ld                            x16, 16*8(a1)                     # x16 is a6
        ld                            x15, 15*8(a1)                     # x15 is a5
        ld                            x14, 14*8(a1)                     # x14 is a4
        ld                            x13, 13*8(a1)                     # x13 is a3
        ld                            x12, 12*8(a1)                     # x12 is a2
        li                            x10, 0                            # Set x10 (a0) in trap frame to 0
        ld                            x9, 9*8(a1)                       # x9 is s1
        ld                            x8, 8*8(a1)                       # x8 is s0/fp
        ld                            x7, 7*8(a1)                       # x7 is t2
        ld                            x6, 6*8(a1)                       # x6 is t1
        ld                            x5, 5*8(a1)                       # x5 is t0
        # ld                            x4, 4*8(a1)                       # x4 is tp
        ld                            x3, 3*8(a1)                       # x3 is gp
        ld                            x2, 2*8(a1)                       # x2 is sp
        ld                            x1, 1*8(a1)                       # x1 is ra
        ld                            x11, 11*8(a1)                     # x11 is a1

        sret

# Statically allocated stack for the idle thread.

        .section        .data.stack, "wa", @progbits
        .balign          16
        
        .equ            IDLE_STACK_SIZE, 4096

        .global         _idle_stack_lowest
        .type           _idle_stack_lowest, @object
        .size           _idle_stack_lowest, IDLE_STACK_SIZE

        .global         _idle_stack_anchor
        .type           _idle_stack_anchor, @object
        .size           _idle_stack_anchor, 2*8

_idle_stack_lowest:
        .fill   IDLE_STACK_SIZE, 1, 0xA5

_idle_stack_anchor:
        .global idle_thread # from thread.c
        .dword  idle_thread
        .fill   8
        .end

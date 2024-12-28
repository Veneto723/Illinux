// excp.c - Exception handlers
//

#include "trap.h"
#include "csr.h"
#include "halt.h"
#include "memory.h"
#include "syscall.h"

#include <stddef.h>

// EXPORTED FUNCTION DECLARATIONS
//

// Called to handle an exception in S mode and U mode, respectively

extern void smode_excp_handler(unsigned int code, struct trap_frame *tfr);
extern void umode_excp_handler(unsigned int code, struct trap_frame *tfr);

// INTERNAL FUNCTION DECLARATIONS
//

static void __attribute__((noreturn)) default_excp_handler(
    unsigned int code, const struct trap_frame *tfr);

// IMPORTED FUNCTION DECLARATIONS
//

extern void syscall_handler(struct trap_frame *tfr); // syscall.c

// INTERNAL GLOBAL VARIABLES
//

static const char *const excp_names[] = {
    [RISCV_SCAUSE_INSTR_ADDR_MISALIGNED] = "Misaligned instruction address",
    [RISCV_SCAUSE_INSTR_ACCESS_FAULT] = "Instruction access fault",
    [RISCV_SCAUSE_ILLEGAL_INSTR] = "Illegal instruction",
    [RISCV_SCAUSE_BREAKPOINT] = "Breakpoint",
    [RISCV_SCAUSE_LOAD_ADDR_MISALIGNED] = "Misaligned load address",
    [RISCV_SCAUSE_LOAD_ACCESS_FAULT] = "Load access fault",
    [RISCV_SCAUSE_STORE_ADDR_MISALIGNED] = "Misaligned store address",
    [RISCV_SCAUSE_STORE_ACCESS_FAULT] = "Store access fault",
    [RISCV_SCAUSE_ECALL_FROM_UMODE] = "Environment call from U mode",
    [RISCV_SCAUSE_ECALL_FROM_SMODE] = "Environment call from S mode",
    [RISCV_SCAUSE_INSTR_PAGE_FAULT] = "Instruction page fault",
    [RISCV_SCAUSE_LOAD_PAGE_FAULT] = "Load page fault",
    [RISCV_SCAUSE_STORE_PAGE_FAULT] = "Store page fault"};

// EXPORTED FUNCTION DEFINITIONS
//

void smode_excp_handler(unsigned int code, struct trap_frame *tfr)
{
    default_excp_handler(code, tfr);
}

/*******************************************************************************
 * Function: umode_excp_handler
 *
 * Description: Handles exceptions that occur in U mode.
 *
 * Inputs:
 * code (unsigned int) - Exception code from scause register
 * tfr (struct trap_frame *) - Trap frame containing exception information
 *
 * Output: None
 *
 * Side Effects:
 * - Updates sepc in trap frame to advance past ecall instruction on syscall
 * - Calls appropriate exception handler
 ******************************************************************************/
void umode_excp_handler(unsigned int code, struct trap_frame *tfr)
{
    debug("%s: %s @ tfr = %p", __func__, excp_names[code], tfr);
    switch (code)
    {
    // TODO: FIXME dispatch to various U mode exception handlers
    case RISCV_SCAUSE_ECALL_FROM_UMODE:
        // Handle system call
        debug("%s: Handling syscall with sepc=%p sp=%p", __func__, (void *)tfr->sepc, (void *)tfr->x[TFR_SP]);
        // Advance PC past ecall instruction
        tfr->sepc += 4;
        syscall_handler(tfr);
        debug("%s: After syscall with sepc=%p sp=%p", __func__, (void *)tfr->sepc, (void *)tfr->x[TFR_SP]);
        break;
    case RISCV_SCAUSE_STORE_PAGE_FAULT:
    case RISCV_SCAUSE_LOAD_PAGE_FAULT:
        memory_handle_page_fault((void *)tfr->x[TFR_SP]);
        break;
    case RISCV_SCAUSE_INSTR_PAGE_FAULT:
        debug("%s: Instruction page fault at sepc=%p sp=%p", __func__, (void *)tfr->sepc, (void *)tfr->x[TFR_SP]);
    default:
        default_excp_handler(code, tfr);
        break;
    }
}

void default_excp_handler(
    unsigned int code, const struct trap_frame *tfr)
{
    const char *name = NULL;

    if (0 <= code && code < sizeof(excp_names) / sizeof(excp_names[0]))
        name = excp_names[code];

    if (name == NULL)
        kprintf("Exception %d at %p\n", code, (void *)tfr->sepc);
    else
        kprintf("%s at %p\n", name, (void *)tfr->sepc);

    panic(NULL);
}

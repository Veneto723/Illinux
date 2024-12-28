// plic.c - RISC-V PLIC
//

#include "plic.h"
#include "console.h"

#include <stdint.h>

// COMPILE-TIME CONFIGURATION
//

// *** Note to student: you MUST use PLIC_IOBASE for all address calculations,
// as this will be used for testing!

#ifndef PLIC_IOBASE
#define PLIC_IOBASE 0x0C000000
#endif

#define PLIC_SRCCNT 0x400
#define PLIC_CTXCNT 2

#define PLIC_SOURCE_PRIO 0x000004 // address offset of interrupt source 1 priority

#define PLIC_PENDING 0x1000 // address offset of pending bits for interrupt sources

#define PLIC_ENABLE 0x002000  // address offset of interrupt enable bits for context 1 (same for disable)
#define NEXT_PLIC_ENABLE 0x80 // address between contexts for interrupt enable bits (same for disable)

#define PLIC_PRIO_TR 0x200000    // address offset of priority threshold for context 1
#define NEXT_PLIC_PRIO_TR 0x1000 // address between contexts for priority threshold

#define PLIC_CLAIM_COMPLETE 0x200004    // address offset of claim/complete for context 1
#define NEXT_PLIC_CLAIM_COMPLETE 0x1000 // address between contexts for claim/complete

// INTERNAL FUNCTION DECLARATIONS
//

// *** Note to student: the following MUST be declared extern. Do not change these
// function delcarations!

extern void plic_set_source_priority(uint32_t srcno, uint32_t level);
extern int plic_source_pending(uint32_t srcno);
extern void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_set_context_threshold(uint32_t ctxno, uint32_t level);
extern uint32_t plic_claim_context_interrupt(uint32_t ctxno);
extern void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno);

// Currently supports only single-hart operation. The low-level PLIC functions
// already understand contexts, so we only need to modify the high-level
// functions (plic_init, plic_claim, plic_complete).

// EXPORTED FUNCTION DEFINITIONS
//

void plic_init(void)
{
    int i;

    // Disable all sources by setting priority to 0, enable all sources for
    // context 1 (S mode on hart 0).

    for (i = 0; i < PLIC_SRCCNT; i++)
    {
        plic_set_source_priority(i, 0);
        // plice_enable_source_for_context(0, i);
        plic_enable_source_for_context(1, i);
    }
}

extern void plic_enable_irq(int irqno, int prio)
{
    trace("%s(irqno=%d,prio=%d)", __func__, irqno, prio);
    plic_set_source_priority(irqno, prio);
}

extern void plic_disable_irq(int irqno)
{
    if (0 < irqno)
        plic_set_source_priority(irqno, 0);
    else
        debug("plic_disable_irq called with irqno = %d", irqno);
}

extern int plic_claim_irq(void)
{
    // Hardwired context 1 (S mode on hart 0)
    trace("%s()", __func__);
    // return plic_claim_context_interrupt(0)
    return plic_claim_context_interrupt(1);
}

extern void plic_close_irq(int irqno)
{
    // Hardwired context 1 (S mode on hart 0)
    trace("%s(irqno=%d)", __func__, irqno);
    // plic_complete_context_interrupt(0, irqno);
    plic_complete_context_interrupt(1, irqno);
}

// INTERNAL FUNCTION DEFINITIONS
//

/*******************************************************************************
 * Function: plic_set_source_priority
 *
 * Description: Sets the priority level for a given interrupt source.
 *
 * Inputs:
 * srcno (uint32_t) - The source number of the interrupt
 * level (uint32_t) - The priority level to set for the interrupt source
 *
 * Output: None
 *
 * Side Effects:
 * - Modifies the priority level of the specified interrupt source in the PLIC
 ******************************************************************************/
void plic_set_source_priority(uint32_t srcno, uint32_t level)
{
    // FIXME your code goes here
    if (srcno < PLIC_SRCCNT && level <= PLIC_PRIO_MAX)
    {
        *(volatile uint32_t *)(uintptr_t)(PLIC_IOBASE + PLIC_SOURCE_PRIO * srcno) = level;
    }
}

/*******************************************************************************
 * Function: plic_source_pending
 *
 * Description: Checks if an interrupt is pending for a given interrupt source.
 *
 * Inputs:
 * srcno (uint32_t) - The source number of the interrupt
 *
 * Output:
 * Returns 1 if the interrupt is pending, 0 otherwise
 *
 * Side Effects:
 * - Accesses pending bit for the specified interrupt source in the PLIC
 ******************************************************************************/
int plic_source_pending(uint32_t srcno)
{
    // FIXME your code goes here
    if (srcno < PLIC_SRCCNT)
    {
        // (srcno / 32) is register offset
        // Multiply by 4 because each register is 4 bytes apart
        // Bit shift right by (srcno % 32) to get bit offset
        return (*(volatile uint32_t *)(uintptr_t)(PLIC_IOBASE + PLIC_PENDING + (srcno / 32) * 4) >> (srcno % 32)) & 1;
    }
    return 0;
}

/*******************************************************************************
 * Function: plic_enable_source_for_context
 *
 * Description: Enables an interrupt source for a given context in the PLIC.
 *
 * Inputs:
 * ctxno (uint32_t) - The context number
 * srcno (uint32_t) - The source number of the interrupt to enable
 *
 * Output: None
 *
 * Side Effects:
 * - Modifies enables registers by setting bit
 ******************************************************************************/
void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno)
{
    // FIXME your code goes here
    if (ctxno < PLIC_CTXCNT && srcno < PLIC_SRCCNT)
    {
        // (srcno / 32) is register offset
        // Multiply by 4 because each register is 4 bytes apart
        // Set bit (srcno % 32) to 1
        *(volatile uint32_t *)(uintptr_t)(PLIC_IOBASE + PLIC_ENABLE + ctxno * NEXT_PLIC_ENABLE + (srcno / 32) * 4) |= (1 << (srcno % 32));
    }
}

/*******************************************************************************
 * Function: plic_disable_source_for_context
 *
 * Description: Disables an interrupt source for a given context in the PLIC.
 *
 * Inputs:
 * ctxno (uint32_t) - The context number
 * srcno (uint32_t) - The source number of the interrupt to disable
 *
 * Output: None
 *
 * Side Effects:
 * - Modifies enables registers by clearing bit
 ******************************************************************************/
void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcid)
{
    // FIXME your code goes here
    if (ctxno < PLIC_CTXCNT && srcid < PLIC_SRCCNT)
    {
        // (srcno / 32) is register offset
        // Multiply by 4 because each register is 4 bytes apart
        // Clear bit (srcno % 32) to 0
        *(volatile uint32_t *)(uintptr_t)(PLIC_IOBASE + PLIC_ENABLE + ctxno * NEXT_PLIC_ENABLE + (srcid / 32) * 4) &= ~(1 << (srcid % 32));
    }
}

/*******************************************************************************
 * Function: plic_set_context_threshold
 *
 * Description: Sets the interrupt priority threshold for a given context in the PLIC.
 *
 * Inputs:
 * ctxno (uint32_t) - The context number
 * level (uint32_t) - The priority threshold level to set
 *
 * Output: None
 *
 * Side Effects:
 * - Modifies the priority threshold for the specified context in the PLIC
 ******************************************************************************/
void plic_set_context_threshold(uint32_t ctxno, uint32_t level)
{
    // FIXME your code goes here
    if (ctxno < PLIC_CTXCNT && level <= PLIC_PRIO_MAX)
    {
        *(volatile uint32_t *)(uintptr_t)(PLIC_IOBASE + PLIC_PRIO_TR + ctxno * NEXT_PLIC_PRIO_TR) = level;
    }
}

/*******************************************************************************
 * Function: plic_claim_context_interrupt
 *
 * Description: Claims the highest priority pending interrupt for a given context.
 *
 * Inputs:
 * ctxno (uint32_t) - The context number
 *
 * Output:
 * Returns the ID of the highest priority pending interrupt, 0 otherwise
 *
 * Side Effects:
 * - Reads from the claim/complete register of the PLIC
 * - If successful, corresponding pending bit is cleared atomically
 ******************************************************************************/
uint32_t plic_claim_context_interrupt(uint32_t ctxno)
{
    // FIXME your code goes here
    if (ctxno < PLIC_CTXCNT)
    {
        return *(volatile uint32_t *)(uintptr_t)(PLIC_IOBASE + PLIC_CLAIM_COMPLETE + ctxno * NEXT_PLIC_CLAIM_COMPLETE);
    }
    return 0;
}

/*******************************************************************************
 * Function: plic_complete_context_interrupt
 *
 * Description: Completes a previously claimed interrupt for a given context.
 *
 * Inputs:
 * ctxno (uint32_t) - The context number
 * srcno (uint32_t) - The source number of the interrupt to complete
 *
 * Output: None
 *
 * Side Effects:
 * - Writes the interrupt ID to the claim/complete register of the PLIC
 ******************************************************************************/
void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno)
{
    // FIXME your code goes here
    if (ctxno < PLIC_CTXCNT && srcno < PLIC_SRCCNT)
    {
        *(volatile uint32_t *)(uintptr_t)(PLIC_IOBASE + PLIC_CLAIM_COMPLETE + ctxno * NEXT_PLIC_CLAIM_COMPLETE) = srcno;
    }
}

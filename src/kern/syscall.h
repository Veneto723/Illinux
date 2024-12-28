#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#include "trap.h"

// syscall handler called from trap handler
void syscall_handler(struct trap_frame *tfr);

#endif /* _SYSCALL_H_ */
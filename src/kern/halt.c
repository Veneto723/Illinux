// halt.c
//

#include "halt.h"
#include "trap.h"
#include "console.h"

#include <stdint.h>

// The halt_success and halt_failure functions use the virt test device to
// terminate. Will not work on real hardware.

void halt_success(void) {
	*(int*)0x100000 = 0x5555; // success
	for (;;) continue; // just in case
}

void halt_failure(void) {
	*(int*)0x100000 = 0x3333; // failure
	for (;;) continue; // just in case
}

void panic(const char * msg) {
	if (msg != NULL)
		console_puts(msg);
	
	halt_failure();
}

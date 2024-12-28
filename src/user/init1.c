
#include "syscall.h"
#include "string.h"

/*******************************************************************************
 * Function: main
 *
 * Description: Test for _write and _close
 *
 * Inputs: None
 *
 * Output: None
 *
 * Side Effects:
 * - Opens ser1 device
 * - Writes "Hello, world!" to ser1 100 times
 * - Closes ser1 device
 ******************************************************************************/
void main(void)
{
    const char *const greeting = "Hello, world!\r\n";
    size_t slen;
    int result;

    // Open ser1 device as fd=0
    result = _devopen(0, "ser", 1);

    if (result < 0)
        return;

    slen = strlen(greeting);

    for (int i = 0; i < 100; i++)
    {
        _write(0, greeting, slen);
        for (int j = 0; j < 100000000; j++)
        {
        }
    }
    _close(0);
}

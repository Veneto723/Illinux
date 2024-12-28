
#include "syscall.h"
#include "string.h"

/*******************************************************************************
 * Function: main
 *
 * Description: Test for _msgout, _devopen, _fsopen, and _exec
 *
 * Inputs: None
 *
 * Output: None
 *
 * Side Effects:
 * - Opens ser1 device
 * - Opens trek file and executes it
 ******************************************************************************/
void main(void)
{
    int result;

    // Open ser1 device as fd=0
    _msgout("_devopen starting...\n");
    result = _devopen(0, "ser", 1);

    if (result < 0)
    {
        _msgout("_devopen failed\n");
        _exit();
    }

    _msgout("_devopen successful\n");

    // ... run trek

    _msgout("_fsopen starting...\n");
    result = _fsopen(1, "trek");

    if (result < 0)
    {
        _msgout("_fsopen failed\n");
        _exit();
    }
    _msgout("_fsopen successful\n");

    _msgout("_exec starting...\n");
    _exec(1);
}

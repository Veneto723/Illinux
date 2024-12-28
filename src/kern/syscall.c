#include "syscall.h"
#include "process.h"
#include "string.h"
#include "device.h"
#include "io.h"
#include "console.h"
#include "trap.h"
#include "scnum.h"
#include "elf.h"
#include "memory.h"
#include "timer.h"
#include "thread.h"

// Add declarations
extern int fs_open(const char *name, struct io_intf **io_ptr);
extern void fs_close(struct io_intf *io);

/*******************************************************************************
 * Function: sysexit
 *
 * Description: Exits the current process.
 *
 * Inputs: None
 *
 * Output:
 * Return -1 (Should never reach here)
 *
 * Side Effects:
 * - Calls process_exit to exit the current process
 ******************************************************************************/
static int sysexit(void)
{
    // Exit current process
    debug("sysexit: Process exiting\n");
    process_exit();
    return -1; // Should never reach here
}

/*******************************************************************************
 * Function: sysmsgout
 *
 * Description: Prints a user message to the console.
 *
 * Inputs:
 * msg (const char *) - The message to print
 *
 * Output:
 * Retuns 0 on success, negative error code on failure
 *
 * Side Effects:
 * - Writes output to system console
 ******************************************************************************/
static int sysmsgout(const char *msg)
{
    debug("sysmsgout: Printing message...\n");
    // int result;

    // Validate memory of message
    // result = memory_validate_vstr(msg, PTE_U | PTE_R);
    // if (result != 0)
    // {
    //     return result;
    // }

    // Print message to console
    kprintf("Thread <%s:%d> says: %s\n", thread_name(running_thread()), running_thread(), msg);

    return 0;
}

/*******************************************************************************
 * Function: sysdevopen
 *
 * Description: Opens a device and associate it with a file descriptor.
 *
 * Inputs:
 * fd (int) - Requested fd number (negative to auto-assign)
 * name (const char *) - Device name
 * instno (int) - Device instance number
 *
 * Output:
 * Returns fd number on success, negative error code on failure
 *
 * Side Effects:
 * - Opens device and updates process's iotab
 ******************************************************************************/
static int sysdevopen(int fd, const char *name, int instno)
{
    debug("sysdevopen: Opening device name=%s, instno=%d, fd=%d\n",
          name, instno, fd);
    struct io_intf *io;
    struct process *proc = current_process();
    int new_fd;

    // Validate name
    if (name == NULL)
    {
        debug("sysdevopen: NULL device name\n");
        return -EINVAL;
    }

    // Call device_open
    int ret = device_open(&io, name, instno);
    if (ret < 0)
    {
        debug("sysdevopen: device_open failed with error %d\n", ret);
        return ret;
    }

    if (fd >= 0)
    {
        // Check if fd is already in use
        if (fd >= PROCESS_IOMAX)
        {
            debug("sysdevopen: Out of range fd=%d\n", fd);
            ioclose(io);
            return -EMFILE;
        }
        if (proc->iotab[fd] != NULL)
        {
            debug("sysdevopen: Requested fd=%d already in use\n", fd);
            ioclose(io);
            return -EBADFD;
        }
        new_fd = fd;
    }
    else
    {
        // Find next available fd
        for (new_fd = 0; new_fd < PROCESS_IOMAX; new_fd++)
        {
            if (proc->iotab[new_fd] == NULL)
            {
                break;
            }
        }
        // No free fd
        if (new_fd == PROCESS_IOMAX)
        {
            debug("sysdevopen: No free fd available\n");
            ioclose(io);
            return -EMFILE;
        }
    }

    // Store io
    proc->iotab[new_fd] = io;
    proc->iotab[new_fd]->refcnt = 1;
    debug("sysdevopen: Successfully opened device at fd=%d\n", new_fd);
    return new_fd;
}

/*******************************************************************************
 * Function: sysfsopen
 *
 * Description: Opens a file and associate it with a file descriptor.
 *
 * Inputs:
 * fd (int) - Requested fd number (negative to auto-assign)
 * name (const char *) - File name
 *
 * Output:
 * Returns fd number on success, negative error code on failure
 *
 * Side Effects:
 * - Opens file and updates process's iotab
 ******************************************************************************/
static int sysfsopen(int fd, const char *name)
{
    debug("sysfsopen: Opening file name=%s, fd=%d\n", name, fd);
    struct io_intf *io;
    struct process *proc = current_process();
    int new_fd;

    // Validate name
    if (name == NULL)
    {
        debug("sysfsopen: NULL file name\n");
        return -EINVAL;
    }

    // Open file
    int ret = fs_open(name, &io);
    if (ret < 0)
    {
        debug("sysfsopen: fs_open failed with error %d\n", ret);
        return ret;
    }

    if (fd >= 0)
    {
        // Check if fd is valid
        if (fd >= PROCESS_IOMAX)
        {
            debug("sysfsopen: Out of range fd=%d\n", fd);
            fs_close(io);
            return -EMFILE;
        }
        if (proc->iotab[fd] != NULL)
        {
            debug("sysfsopen: Requested fd=%d already in use\n", fd);
            fs_close(io);
            return -EBADFD;
        }
        new_fd = fd;
    }
    else
    {
        // Find next available fd
        for (new_fd = 0; new_fd < PROCESS_IOMAX; new_fd++)
        {
            if (proc->iotab[new_fd] == NULL)
            {
                break;
            }
        }
        // No free fd
        if (new_fd == PROCESS_IOMAX)
        {
            debug("sysfsopen: No free fd available\n");
            fs_close(io);
            return -EMFILE;
        }
    }

    // Store io
    proc->iotab[new_fd] = io;
    proc->iotab[new_fd]->refcnt = 1;
    debug("sysfsopen: Successfully opened file at fd=%d\n", new_fd);
    return new_fd;
}

/*******************************************************************************
 * Function: sysclose
 *
 * Description: Closes an open fd.
 *
 * Inputs:
 * fd (int) - fd number to close
 *
 * Output:
 * Returns 0 on success, negative error code on failure
 *
 * Side Effects:
 * - Closes io interface and clears fd entry in process's iotab
 ******************************************************************************/
static int sysclose(int fd)
{
    debug("sysclose: Closing fd=%d\n", fd);
    struct process *proc = current_process();

    // Validate fd
    if (fd < 0 || fd >= PROCESS_IOMAX)
    {
        debug("sysclose: Out of range fd=%d\n", fd);
        return -EBADFD;
    }

    // Get io interface
    struct io_intf *io = proc->iotab[fd];
    if (io == NULL)
    {
        debug("sysclose: Non-open fd=%d\n", fd);
        return -EBADFD;
    }

    // Close io
    ioclose(io);

    // Clear fd
    proc->iotab[fd] = NULL;

    debug("sysclose: Successfully closed file at fd=%d\n", fd);
    return 0;
}

/*******************************************************************************
 * Function: sysread
 *
 * Description: Reads from an open fd into a buffer.
 *
 * Inputs:
 * fd (int) - fd number to read from
 * buf (void *) - Buffer to read into
 * bufsz (size_t) - Size of buffer
 *
 * Output:
 * Returns number of bytes read on success, negative error code on failure
 *
 * Side Effects:
 * - Performs io read operation and modifies contents of provided buffer
 ******************************************************************************/
static long sysread(int fd, void *buf, size_t bufsz)
{
    debug("sysread: Reading fd=%d, buf=%p, size=%zu\n", fd, buf, bufsz);
    struct process *proc = current_process();

    // Validate fd
    if (fd < 0 || fd >= PROCESS_IOMAX)
    {
        debug("sysread: Out of range fd=%d\n", fd);
        return -EBADFD;
    }

    // Get io interface
    struct io_intf *io = proc->iotab[fd];
    if (io == NULL)
    {
        debug("sysread: Non-open fd=%d\n", fd);
        return -EBADFD;
    }

    // Validate buffer
    if (buf == NULL)
    {
        debug("sysread: NULL buffer\n");
        return -EINVAL;
    }

    // Perform read operation
    long bytes_read = ioread(io, buf, bufsz);

    if (bytes_read < 0)
    {
        debug("sysread: Read failed with error %ld\n", bytes_read);
        return bytes_read;
    }

    debug("sysread: Successfully read %ld bytes\n", bytes_read);
    return bytes_read;
}

/*******************************************************************************
 * Function: syswrite
 *
 * Description: Writes to an open fd from a buffer.
 *
 * Inputs:
 * fd (int) - fd number to write to
 * buf (void *) - Buffer to write from
 * len (size_t) - Size of buffer
 *
 * Output:
 * Returns number of bytes written on success, negative error code on failure
 *
 * Side Effects:
 * - Performs io write operation and modifies the io object
 ******************************************************************************/
static long syswrite(int fd, const void *buf, size_t len)
{
    debug("syswrite: Writing fd=%d, buf=%p, size=%zu\n", fd, buf, len);
    struct process *proc = current_process();

    // Validate fd
    if (fd < 0 || fd >= PROCESS_IOMAX)
    {
        debug("syswrite: Out of range fd=%d\n", fd);
        return -EBADFD;
    }

    // Get io interface
    struct io_intf *io = proc->iotab[fd];
    if (io == NULL)
    {
        debug("syswrite: Non-open fd=%d\n", fd);
        return -EBADFD;
    }

    // Validate buffer
    if (buf == NULL)
    {
        debug("syswrite: NULL buffer\n");
        return -EINVAL;
    }

    // Perform write operation
    long bytes_written = iowrite(io, buf, len);

    if (bytes_written < 0)
    {
        debug("syswrite: Write failed with error %ld\n", bytes_written);
        return bytes_written;
    }

    debug("syswrite: Successfully wrote %ld bytes\n", bytes_written);
    return bytes_written;
}

/*******************************************************************************
 * Function: sysioctl
 *
 * Description: Performs an ioctl operation on an open fd.
 *
 * Inputs:
 * fd (int) - fd number to read from
 * cmd (int) - ioctl command
 * arg (void *) - Argument for ioctl command
 *
 * Output:
 * Returns 0 on success, negative error code on failure
 *
 * Side Effects:
 * - Performs ioctl operation and may modify the io object
 ******************************************************************************/
static int sysioctl(int fd, int cmd, void *arg)
{
    debug("sysioctl: fd=%d, cmd=%d, arg=%p\n", fd, cmd, arg);
    struct process *proc = current_process();

    // Validate fd
    if (fd < 0 || fd >= PROCESS_IOMAX)
    {
        debug("sysioctl: Out of range fd=%d\n", fd);
        return -EBADFD;
    }

    // Get io interface
    struct io_intf *io = proc->iotab[fd];
    if (io == NULL)
    {
        debug("sysioctl: Non-open fd=%d\n", fd);
        return -EBADFD;
    }

    // Run ioctl
    int result = ioctl(io, cmd, arg);

    if (result < 0)
    {
        debug("sysioctl: Command failed with error %ld\n", result);
        return result;
    }

    debug("sysioctl: Command completed\n");
    return 0;
}

/*******************************************************************************
 * Function: sysexec
 *
 * Description: Executes a new program using an open fd.
 *
 * Inputs:
 * fd (int) - fd number to execute
 *
 * Output:
 * Returns negative error code on failure (should not return on success)
 *
 * Side Effects:
 * - Closes current process and starts a new process
 ******************************************************************************/
static int sysexec(int fd)
{
    debug("sysexec: Executing fd=%d\n", fd);
    struct process *proc = current_process();

    // Validate fd
    if (fd < 0 || fd >= PROCESS_IOMAX)
    {
        debug("sysioctl: Out of range fd=%d\n", fd);
        return -EBADFD;
    }

    // Get io interface
    struct io_intf *io = proc->iotab[fd];
    if (io == NULL)
    {
        debug("sysexec: Non-open fd=%d\n", fd);
        return -EBADFD;
    }

    // Zero the iotab entry since io object will be closed
    current_process()->iotab[fd] = NULL;

    // Call process_exec
    int ret = process_exec(io);

    // Should not reach here
    debug("sysexec: process_exec failed with error %d\n", ret);
    return ret;
}

/*******************************************************************************
 * Function: sysfork
 *
 * Description: Forks a new child process.
 *
 * Inputs:
 * tfr (const struct trap_frame *) - Trap frame of parent process
 *
 * Output:
 * Returns child thread ID in parent process, 0 in child process, negative error code on failure
 *
 * Side Effects:
 * - Calls process_fork to create a new child process
 ******************************************************************************/
static int sysfork(const struct trap_frame *tfr)
{
    debug("sysfork: Starting\n");
    // get the current process
    struct process *proc = current_process();
    // create the child process based on the parent process
    struct process *child = process_fork(proc->id);

    if (child == NULL) return -EBUSY;

    // Fork
    int child_pid = thread_fork_to_user(child, tfr);
    if (child_pid < 0)
    {
        debug("sysfork: thread_fork_to_user failed with error %d\n", child_pid);
        return child_pid;
    }

    debug("sysfork: Successful, returning child pid=%d\n", child_pid);
    return child_pid;
}

/*******************************************************************************
 * Function: syswait
 *
 * Description: Waits for a child thread (created with fork) to exit.
 *
 * Inputs:
 * tid (int) - Child thread ID to wait for (0 for any)
 *
 * Output:
 * Returns child thread ID on success, negative error code on failure
 *
 * Side Effects:
 * - Calls thread_join_any or thread_join to wait for child thread to exit
 ******************************************************************************/
static int syswait(int tid)
{
    // Copied from the slides
    debug("syswait: Waiting for thread tid=%d\n", tid);

    if (tid == 0)
    {
        return thread_join_any();
    }
    else
    {
        return thread_join(tid);
    }
}

/*******************************************************************************
 * Function: sysusleep
 *
 * Description: Sleeps for a specified number of microseconds.
 *
 * Inputs:
 * us (unsigned long) - Number of microseconds to sleep
 *
 * Output:
 * Returns 0 on success
 *
 * Side Effects:
 * - Initializes alarm from timer.c
 * - Calls alarm_sleep to sleep
 ******************************************************************************/
static int sysusleep(unsigned long us)
{
    debug("sysusleep: Sleeping for %lu microseconds\n", us);

    // Calculate wake time (1 second = 1000000 microseconds)
    uint64_t wake = us * TIMER_FREQ / 1000000;

    // Initialize alarm
    struct alarm al;
    alarm_init(&al, "usleep");

    // Sleep
    debug("sysusleep: Sleeping for %lu ticks\n", wake);
    alarm_sleep(&al, wake);

    return 0;
}

/*******************************************************************************
 * Function: syspioref
 *
 * Description: Print the io->refcnt to the console.
 *
 * Inputs: None
 *
 * Output:
 * Returns 0 on success
 *
 * Side Effects: None
 ******************************************************************************/
static int syspioref(){
    struct process *current = current_process();

    for(int i = 0; i < PROCESS_IOMAX; i++){
    // Print message to console
        if (current->iotab[i] != NULL)
            kprintf("Thread <%s:%d> says: refcnt = %d\n", thread_name(running_thread()), running_thread(), current->iotab[i]->refcnt);
    }

    return 0;
}

/*******************************************************************************
 * Function: syscall_handler
 *
 * Description: Handles system calls based on syscall number.
 *
 * Inputs:
 * tfr (struct trap_frame *) - Trap frame containing syscall number and arguments
 *
 * Output: None
 *
 * Side Effects:
 * - Calls appropriate syscall function
 * - Updates trap frame with return value (a0)
 ******************************************************************************/
void syscall_handler(struct trap_frame *tfr)
{
    debug("syscall_handler: Starting\n");
    // Get syscall number
    uint64_t syscall_num = tfr->x[TFR_A7];

    // Get arguments
    uint64_t a0 = tfr->x[TFR_A0];
    uint64_t a1 = tfr->x[TFR_A1];
    uint64_t a2 = tfr->x[TFR_A2];

    debug("syscall_handler: syscall=%lu, a0=%lu, a1=%lu, a2=%lu\n",
          syscall_num, a0, a1, a2);

    // Return value
    int64_t ret;

    // Handle syscall based on number
    switch (syscall_num)
    {
    case SYSCALL_EXIT:
        ret = sysexit();
        break;

    case SYSCALL_MSGOUT:
        ret = sysmsgout((const char *)a0);
        break;

    case SYSCALL_DEVOPEN:
        ret = sysdevopen((int)a0, (const char *)a1, (int)a2);
        break;

    case SYSCALL_FSOPEN:
        ret = sysfsopen((int)a0, (const char *)a1);
        break;

    case SYSCALL_CLOSE:
        ret = sysclose((int)a0);
        break;

    case SYSCALL_READ:
        ret = sysread((int)a0, (void *)a1, (size_t)a2);
        break;

    case SYSCALL_WRITE:
        ret = syswrite((int)a0, (const void *)a1, (size_t)a2);
        break;

    case SYSCALL_IOCTL:
        ret = sysioctl((int)a0, (int)a1, (void *)a2);
        break;

    case SYSCALL_EXEC:
        ret = sysexec((int)a0);
        break;

    case SYSCALL_FORK:
        ret = sysfork(tfr);
        break;

    case SYSCALL_WAIT:
        ret = syswait((int)a0);
        break;

    case SYSCALL_USLEEP:
        ret = sysusleep((unsigned long)a0);
        break;

    case SYSCALL_PIOREF:
        ret = syspioref();
        break;

    default:
        debug("syscall_handler: Invalid syscall number %lu\n", syscall_num);
        ret = -ENOTSUP;
        break;
    }

    // Store return value
    debug("syscall_handler: storing return value %d in a0", ret);
    tfr->x[TFR_A0] = ret;
}
#include "syscall.h"
#include "string.h"
#define IOCTL_SETPOS        4
#define IOCTL_GETLEN 1
#define BUF_SZ  256

/*
 * User program to demonstrate locking functionality. Does so by doing the following:
    1. Opens a file
    2. Forks into a parent and child program
    3. Parent program conducts at least three writes and waits for the child to exit
    4. Child program conducts at least three writes
    5. Child program closes the file and exits
    6. Parent program prints the contents of the file to console
    7. Parent program closes the file and exits
*/
void main(void){
    const char * buffer_parent = "      ";
    const char * buffer_child = "     ";
    int i;
    size_t slen_parent = strlen(buffer_parent);
    size_t slen_child = strlen(buffer_child);
    long bytes_read;
    char buffer[BUF_SZ];
    int pos = 0;

    // open the file
    _fsopen(0, "test_lock.txt");

    if(_fork()){
        // write from the parent thread
        for(i = 0; i < 3; i++) {
            pos = slen_child * 3 + slen_parent * i;
            _ioctl(0, IOCTL_SETPOS, &pos);
            _write(0, buffer_parent, slen_parent);   
            bytes_read = _read(0, buffer, BUF_SZ);
            // Add null terminator
            buffer[bytes_read] = '\0';
            // print file contents to console
            _msgout("File contents: ");
            _msgout(buffer);
        }
        // wait for child to exit
        _wait(1);
        
        // print the contents of the file to the console
        // read the file contents
        bytes_read = _read(0, buffer, BUF_SZ);
        // Add null terminator
        buffer[bytes_read] = '\0';
        // print file contents to console
        _msgout("File contents: ");
        _msgout(buffer);

        // close and exit
        _close(0);
        _exit();
    }

    else{
        //write from the child
        for(i = 0; i < 3; i++) {
            pos = slen_child * i;
            _ioctl(0, IOCTL_SETPOS, &pos);
            _write(0, buffer_child, slen_child);
            bytes_read = _read(0, buffer, BUF_SZ);
            // Add null terminator
            buffer[bytes_read] = '\0';
            // print file contents to console
            _msgout("File contents: ");
            _msgout(buffer);
        }
        // close and exit
        _close(0);
        _exit();
    }
}
#include "syscall.h"
#include "string.h"

#define IOCTL_GETLEN 1


/*
User program to test reference counting functionality. Does so by doing the following:
    1. Opens a file from the file system
    2. Forks into a parent and child program
    3. Parents program closes the file and waits for child to exit.
        - The file closing shouldn't affect the child
    4. Child program conducts reads and writes on the open file
    5. Child closes the file and then exits
    6. Parent program exits
*/
void main(void){
    char buffer[256];
    long length, bytes_read;

    _msgout("Before _fsopen (shouldn't print anything):");
    _pioref();
    _fsopen(0, "test.txt");
    _msgout("After _fsopen:");
    _pioref();

    if(_fork()){
        _msgout("After forking:");
        _pioref();
    // close the parent
        _close(0);
    // wait for chold to exit
        _msgout("Starting to wait...");
        _wait(1);
        _exit();
    }
    else{
        _msgout("After forking:");
        _pioref();
        //in child
        // Get file length using _ioctl
        _msgout("Copy some text from file and print to console");
        _ioctl(0, IOCTL_GETLEN, &length);
        // Read the file contents
        bytes_read = _read(0, buffer, length);
        // Add null terminator
        // write contents
        _write(0, buffer, bytes_read);

        // read contents
        _ioctl(0, IOCTL_GETLEN, &length);
        // Read the file contents
        bytes_read += _read(0, buffer, length);
        // Add null terminator
        buffer[bytes_read] = '\0';
        // print file contents to console
        _msgout("File contents: ");
        _msgout(buffer);
        _msgout("After printing:");
        _pioref();

        // close the file and exit
        _close(0);
        _msgout("After closing child: (shouldn't print anything)"); 
        _pioref();
        _exit();
    }
}
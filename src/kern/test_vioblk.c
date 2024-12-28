#include "virtio.h"
#include "intr.h"
#include "halt.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "device.h"
#include "thread.h"
#include "vioblk.c"
#include "console.h"

#define VIRT0_IOBASE 0x10001000
#define VIRT1_IOBASE 0x10002000
#define VIRT0_IRQNO 1

#define USER_START 0x80100000UL

#define UART0_IOBASE 0x10000000
#define UART1_IOBASE 0x10000100
#define UART0_IRQNO 10

extern char _kimg_end[];

//#include <stdint.h>

// internal function declarations

static int test_vioblk_close(struct io_intf *io);

//static int test_vioblk_read(struct io_intf *restrict io, void *restrict buf, unsigned long bufsz);

static int test_vioblk_write(struct io_intf *restrict io, void *restrict buf,unsigned long n);

/*
Inputs: struct io_intf * io: pointer to the io_intf struct used
Outputs: 1 if correct, -1 if incorrect
Description: Calls vioblk_close. If the opened flag is changed to 0, 
            it passes the test. I could not check if the virtqueues were
            properly reset as those registers are not readable by the driver.
*/
int test_vioblk_close(struct io_intf *io){
    struct vioblk_device * const dev = (void*)io - offsetof(struct vioblk_device, io_intf);
    vioblk_close(io);

    if(dev->opened){
        debug("Device opened flag not reset");
        return -1;
    }
    // cannot test virtqueue flags because they are not readable by driver
    return 1;
}

/*
Inputs: struct io_intf * restrict io: pointer to the io_intf struct used
        void * restrict buf: buffer to read to
        unsigned long bufsz: the size of the read
Outputs: 1 if correct, -1 if incorrect
Description: Calls vioblk_read. Checks that the whole data was read.
            Checks that the data was stored in the descriptor. Checks
            that the type was correct. Checks if read returns an error code.
*/
/*int test_vioblk_read(struct io_intf *restrict io, void *restrict buf, unsigned long bufsz){
    struct vioblk_device * const dev = (void*)io - offsetof(struct vioblk_device, io_intf);
    uint8_t * data = buf;
    struct vioblk_request_header * request = (void*)dev->vq.desc[1].addr;
    long read_size = vioblk_read(io, buf, bufsz);

    // if read function does not return an error, make sure it behaves as expected
    if(read_size >= 0){
        
        // check if the size read is as expected
        // if the bufsz is less than the max size, the whole read should happen
        if((read_size < bufsz) && (bufsz <= VIRTIO_BLK_F_BLK_SIZE)){
            debug("Did not read correct amount");
            return -1;
        }

        // check if the descriptors are correct
        // check if data was properly stored in its descriptor
        if((bufsz == read_size) && (*data != *(uint64_t *)(dev->vq.desc[2].addr + read_size))){
            debug("Buffer not properly stored in descriptor");
            return -1;
        }
        // make sure the type was processed as a read
        if(request->type != VIRTIO_BLK_T_IN){
            debug("Type was not a read");
            return -1;
        }
    return 1;
    }

    // if read funtion returns an error, print an error and return -1
    debug("Read returned an error code");
    return -1;
}*/

/*
Inputs: struct io_intf * restrict io: pointer to the io_intf struct used
        void * restrict buf: buffer to write to
        unsigned long n: the size of the write
Outputs: 1 if correct, -1 if incorrect
Description: Calls vioblk_write. Checks that the whole data was written. Checks
            that the type was correct. Checks if write returns an error code.
*/
int test_vioblk_write(struct io_intf *restrict io, void *restrict buf,unsigned long n){
    struct vioblk_device * const dev = (void*)io - offsetof(struct vioblk_device, io_intf);
    struct vioblk_request_header * request = (void*)dev->vq.desc[1].addr;
    long write_size = vioblk_write(io, buf, n);

    // if read function does not return an error, make sure it behaves as expected
    if(write_size >= 0){
        
        // check if the size write is as expected
        // if n is less than the max size, the whole write should happen
        if((write_size < n) && (n <= VIRTIO_BLK_F_BLK_SIZE)){
            debug("Did not write correct amount");
            return -1;
        }

        // make sure the type was processed as a read
        if(request->type != VIRTIO_BLK_T_OUT){
            debug("Type was not a write");
            return -1;
        }
    return 1;
    }

    // if write funtion returns an error, print an error and return -1
    debug("Write returned an error code");
    return -1;
}

/*
Inputs: None
Outputs: 0
Description: Attaches and opens a virtio block driver following the steps
            in main_shell.c. Tests that the ioctl functions work properly.
            If they do, no messages are displayed except that the tests are 
            complete. Tests the write function, tests the close function, reopens
            the device, then tests the read function.
*/
int main(void){
    struct io_intf * blkio;
    void * mmio_base;
    int open_success, write_success, close_success, result, i; 
    uint32_t found_size; 
    uint64_t found_pos, found_len;
    uint32_t * found_size_ptr = &found_size;
    uint64_t * found_pos_ptr = &found_pos;
    uint64_t * found_len_ptr = &found_len;

    console_init();
    intr_init();
    devmgr_init();
    thread_init();
    heap_init(_kimg_end, (void*)USER_START);

    // set up the mmio base
    // based on main_shell, so it uses 8
    for (i = 0; i < 8; i++) {
        mmio_base = (void*)VIRT0_IOBASE;
        mmio_base += (VIRT1_IOBASE-VIRT0_IOBASE)*i;
        virtio_attach(mmio_base, VIRT0_IRQNO+i);
    }

     intr_enable();

    open_success = device_open(&blkio, "blk", 0);
    debug("Open: %d", open_success);

    // start testing io_ctl functions
    struct vioblk_device * const dev = (void *)blkio - offsetof(struct vioblk_device, io_intf); 

    result = vioblk_getblksz(dev, found_size_ptr);
    if(*found_size_ptr != result)
        debug("Found Size Pointer's value is %d but should be %d.", *found_size_ptr, result);
    
    if(result != dev->blksz)
        debug("Size found was %d when it should have been %d.", result, dev->blksz);

    debug("vioblk_getblksz tests complete.");

    result = vioblk_getlen(dev, found_len_ptr);
    if(*found_len_ptr != result)
        debug("Found Length Pointer's value is %d but should be %d.", *found_len_ptr, result);
    
    if(result != dev->size)
        debug("Size found was %d when it should have been %d.", result, dev->size);

    debug("vioblk_getlen tests complete.");

    // 42 is an arbitrary choice to test the function
    found_pos = 42;
    found_pos_ptr = &found_pos;
    result = vioblk_setpos(dev, found_pos_ptr);
    if(*found_pos_ptr != result)
        debug("Found Position Pointer's value is %d when it should be %d.", *found_pos_ptr, result);

    if(result != 42)
        debug("Position is %d but should be 42.", result);

    if(result != dev->pos)
        debug("Device position should be %d but is %d.", result, dev->pos);
    
    debug("vioblk_setpos tests complete.");

    result = vioblk_getpos(dev, found_pos_ptr);
    if(*found_pos_ptr != result)
        debug("Found Position Pointer's value is %d when it should be %d.", *found_pos_ptr, result);

    if(found_pos != dev->pos)
        debug("Position is %d but should be %d.", result, dev->pos);

    debug("vioblk_getpos tests complete.");


    // start testing read/write functions
    // 4096 was chosen to match the raw file
    char buf[4096];

    // I wanted some data to make it easy to check that the position was changing, so I filled two blocks with arbitrary chars
    for(int i = 0; i < 1024; i++){
        buf[i] = i;
    }

    // 4096 was chosen size of buf
    write_success = test_vioblk_write(blkio, &buf, 4096);
    debug("Write: %d", write_success);

    close_success = test_vioblk_close(blkio);
    debug("Close: %d", close_success);

    open_success = device_open(&blkio, "blk", 0);
    debug("Open: %d", open_success);

    return 0;
}
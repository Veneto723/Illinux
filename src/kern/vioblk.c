//            vioblk.c - VirtIO serial port (console)
//           

#include "virtio.h"
#include "intr.h"
#include "halt.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "lock.h"

//            COMPILE-TIME PARAMETERS
//           

#define VIOBLK_IRQ_PRIO 1

//            INTERNAL CONSTANT DEFINITIONS
//           

//# define USED_UPDATED_MASK 1 << 1
//            VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX 1
#define VIRTIO_BLK_F_SEG_MAX 2
#define VIRTIO_BLK_F_GEOMETRY 4
#define VIRTIO_BLK_F_RO 5
#define VIRTIO_BLK_F_BLK_SIZE 6
#define VIRTIO_BLK_F_FLUSH 9
#define VIRTIO_BLK_F_TOPOLOGY 10
#define VIRTIO_BLK_F_CONFIG_WCE 11
#define VIRTIO_BLK_F_MQ 12
#define VIRTIO_BLK_F_DISCARD 13
#define VIRTIO_BLK_F_WRITE_ZEROES 14

//            INTERNAL TYPE DEFINITIONS
//           

//            All VirtIO block device requests consist of a request header, defined below,
//            followed by data, followed by a status byte. The header is device-read-only,
//            the data may be device-read-only or device-written (depending on request
//            type), and the status byte is device-written.

struct vioblk_request_header
{
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

#define VIRTIO_REQUEST_HEADER_SIZE  16
#define VIRTIO_STATUS_SIZE          1
#define VIRTIO_DESC_SIZE            16
#define VIRTIO_QUEUE_SZ             1
#define VIRTIO_QUEUE_ID             0

//           Request type (for vioblk_request_header)

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1

//            Status byte values

#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_S_IOERR 1
#define VIRTIO_BLK_S_UNSUPP 2

//            Main device structure.
//           
//            FIXME You may modify this structure in any way you want. It is given as a
//            hint to help you, but you may have your own (better!) way of doing things.

struct vioblk_device
{
    volatile struct virtio_mmio_regs *regs;
    struct io_intf io_intf;
    uint16_t instno;
    uint16_t irqno;
    int8_t opened;
    int8_t readonly;

    //            optimal block size
    uint32_t blksz;
    //            current position
    uint64_t pos;
    //           size of device in bytes
    uint64_t size;
    //            size of device in blksz blocks
    uint64_t blkcnt;

    struct
    {
        //            signaled from ISR
        struct condition used_updated;

        //            We use a simple scheme of one transaction at a time.

        union
        {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        };

        union
        {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        //            The first descriptor is an indirect descriptor and is the one used in
        //            the avail and used rings. The second descriptor points to the header,
        //            the third points to the data, and the fourth to the status byte.

        struct virtq_desc desc[4];
        struct vioblk_request_header req_header;
        uint8_t req_status;
    } vq;

    //            Block currently in block buffer
    uint64_t bufblkno;
    //            Block buffer
    char *blkbuf;
};

static struct lock vio_lock;                        // vioblk lock

//            INTERNAL FUNCTION DECLARATIONS
//           

static int vioblk_open(struct io_intf **ioptr, void *aux);

static void vioblk_close(struct io_intf *io);

static long vioblk_read(struct io_intf *restrict io, void *restrict buf, unsigned long bufsz);

static long operation_single_blk(struct vioblk_device * dev, uint32_t sector, unsigned long datasz, uint32_t type);

static long vioblk_write(
    struct io_intf *restrict io,
    const void *restrict buf,
    unsigned long n);

static int vioblk_ioctl(struct io_intf *restrict io, int cmd, void *restrict arg);

static void vioblk_isr(int irqno, void *aux);

//            IOCTLs

static int vioblk_getlen(const struct vioblk_device *dev, uint64_t *lenptr);
static int vioblk_getpos(const struct vioblk_device *dev, uint64_t *posptr);
static int vioblk_setpos(struct vioblk_device *dev, const uint64_t *posptr);
static int vioblk_getblksz(const struct vioblk_device *dev, uint32_t *blkszptr);

//            EXPORTED FUNCTION DEFINITIONS
//           

//            Attaches a VirtIO block device. Declared and called directly from virtio.c.

/*
Inputs: virtio_mmio_regs *regs: mmio registers for the virtio block
        int irqno: the interrupt request numbers
Outputs: None
Effect: Fills in block device struct and fields in the MMIO registers
Description: Initializes the virtio block device with necessary IO operation functions. Initializaes the driver device
            according to the steps laid out in Section 3.1.1 of the Virtio Specs. (Sets status bits, negotiates feautures 
            with the block device. Fills in the virtio block struct. Attaches the virtqueues to the device. Registers the 
            interrupt service routine and the device. Sets the status bit so the device is "live")
*/
void vioblk_attach(volatile struct virtio_mmio_regs *regs, int irqno)
{
    // initilaize virtio block device with necessary IO operation functions (close, read, write, ctl)
    static const struct io_ops virtio_ops = {
        .close = vioblk_close,
        .read = vioblk_read,
        .write = vioblk_write,
        .ctl = vioblk_ioctl
    };

    // Steps 1-3 of device initialization
    // reset the device
    regs->status = 0;
    // set the acknowledge status bit
    regs->status |= VIRTIO_STAT_ACKNOWLEDGE;
    // set the driver status bit
    regs->status |= VIRTIO_STAT_DRIVER;

    // Step 4 of device initialization
    // read device feature bits and write the features understood to the driver
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_device *dev;
    uint_fast32_t blksz;
    int result;

    assert(regs->device_id == VIRTIO_ID_BLOCK);

    //            Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    //            fence o,io
    __sync_synchronize();

    //            Negotiate features. We need:
    //             - VIRTIO_F_RING_RESET and
    //             - VIRTIO_F_INDIRECT_DESC
    //            We want:
    //             - VIRTIO_BLK_F_BLK_SIZE and
    //             - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    // Step 5-6 of device initialization
    // setting the feature bit and re-reading devce status are included in negotiate features
    result = virtio_negotiate_features(regs, enabled_features, wanted_features, needed_features);

    if (result != 0)
    {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    // Step 7 of device initialization: device specific setup
    //            If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

//    debug("%p: virtio block device block size is %lu", regs, (long)blksz);

    //            Allocate initialize device struct

    dev = kmalloc(sizeof(struct vioblk_device) + blksz);
    memset(dev, 0, sizeof(struct vioblk_device));

    condition_init(&dev->vq.used_updated, "Used Updated");

    dev->regs = regs;
    dev->io_intf.ops = &virtio_ops;
    dev->irqno = irqno;
    dev->opened = 0;
    dev->readonly = 0;
    dev->blksz = blksz;
    dev->pos = 0;
    dev->size = sizeof(struct vioblk_device);
    dev->blkcnt = dev->size / dev->blksz;
    dev->bufblkno = 0;
    dev->blkbuf = (char*) dev + sizeof(struct vioblk_device);

    // fill out the descriptors in the virtq struct
    dev->vq.desc[0].addr = (uint64_t)&dev->vq.desc[1];
    dev->vq.desc[0].len = 3 * VIRTIO_DESC_SIZE; // there are 3 descriptors in the indirect table
    dev->vq.desc[0].flags |= VIRTQ_DESC_F_INDIRECT;

//    debug("Size of virtq desc = %d", sizeof(struct virtq_desc));

    // fill out the request header descriptor
    // next flag is set to chain with the data descriptor
    dev->vq.desc[1].addr = (uint64_t)&dev->vq.req_header;
    dev->vq.desc[1].len = VIRTIO_REQUEST_HEADER_SIZE;
    dev->vq.desc[1].flags |= VIRTQ_DESC_F_NEXT;

    // fill out the data descriptor
    // next flag is set to chain with the status descriptor
    dev->vq.desc[2].addr = (uint64_t)dev->blkbuf;
    dev->vq.desc[2].len = dev->blksz;
    dev->vq.desc[2].flags |= VIRTQ_DESC_F_NEXT;

    // fill out the request status descriptor
    dev->vq.desc[3].addr = (uint64_t)&dev->vq.req_status;
    dev->vq.desc[3].len = VIRTIO_STATUS_SIZE;
    dev->vq.desc[3].flags |= VIRTQ_DESC_F_WRITE;

    dev->vq.desc[1].next = 1;
    dev->vq.desc[2].next = 2;

    // initialize the ring idx
    dev->vq.avail.idx = 0;
    dev->vq.used.idx = 0;

    // attach virtq_avail and virtq_used structs using the virtio_attach_virtq function
    // length is 4 here because we have 4 descriptors in the struct
    virtio_attach_virtq(regs, VIRTIO_QUEUE_ID, VIRTIO_QUEUE_SZ, (uint64_t)&dev->vq.desc, (uint64_t)&dev->vq.used, (uint64_t)&dev->vq.avail);

    // register interrupt service routine and device
    intr_register_isr(irqno, VIOBLK_IRQ_PRIO, vioblk_isr, dev);
    device_register("blk", &vioblk_open, dev);
 
    // Step 7 of device initialization
    // device is live
    regs->status |= VIRTIO_STAT_DRIVER_OK;    
    //           fence o,oi
    __sync_synchronize();

    // initialize the lock
    lock_init(&vio_lock, "vio_lock");
}

/*
Inputs: struct io_intf ** ioptr: pointer to the pointer to the io_intf strcut ioptr
        void * aux: void pointer to auxillary function
Outputs: 0 if opened successfully, negative if unsuccessful
Effect: Enables the interrupt request. Enabling the virtqueues changes a register in the MMIO.
Description: Enables the virtqueues so they are ready for use (using qid 0 as noted in the specs). 
            Enables the interrupt request for the auxillary's interrupt request number. Sets the opened
            flag to 1. Returns the IO operations to the ioptr so they are available for use.
*/
int vioblk_open(struct io_intf ** ioptr, void * aux) {
    struct vioblk_device * const dev = aux; 
    // set virtq_avail and virtq_used queues so they are available for use
    virtio_enable_virtq(dev->regs, 0);
    // if device is already opened, return an error code
    if(dev->opened)
        return -EBUSY;

    // enable the interrupt line for the virtio device and set necessary flags in vioblk_device
    intr_enable_irq(dev->irqno);
    dev->opened = 1;
    dev->pos = 0;

    // return the IO operations to ioptr
    *ioptr = &dev->io_intf;

    return 0;
}

//            Must be called with interrupts enabled to ensure there are no pending
//            interrupts (ISR will not execute after closing).

/*
Inputs: struct io_intf * io: pointer to the io_intf io
Outputs: None
Effect: Resetting the virtqueues changes registers in the MMIO.
Description: Resest the virtqueues. Sets the opened flag to 0.
*/
void vioblk_close(struct io_intf * io) {
    struct vioblk_device * const dev = (void*)io - offsetof(struct vioblk_device, io_intf);

    // ensure close is valid
	assert (io != NULL);
	assert(dev->opened);
    // reset the virtq_avail and virtio_used queues
    virtio_reset_virtq(dev->regs, 0);

    // set necessary flags in vioblk_device
    dev->opened = 0;
}

/*
Inputs: struct io_intf *restrict io: pointer to io_intf struct io
        void *restrict buf: pointer to the data buffer to read from
        unsigned long bufsz: number of bytes to read from buf
Output: number of bytes successfully read from buf
Effect: Calls the helper function to perform a read
Description: Reads a buf of size bufsz. It will split the buf into blocks of size 512 bytes and 
            keep track of how many blocks (and therefore bytes) have been read.
*/
long vioblk_read(struct io_intf *restrict io, void *restrict buf, unsigned long bufsz) {
    struct vioblk_device * const dev =(void*)io - offsetof(struct vioblk_device, io_intf);
    long bytes_read = 0;
    unsigned long sector_size;
    long result;
    // calculate the start sector based on pos
    dev->bufblkno = dev->pos / (uint64_t) dev->blksz;

    // assert requirements for read
    trace("%s(buf=%p,bufsz=%ld)", __func__, buf, bufsz);
    assert (io != NULL);
	assert (dev->opened);
    
    // if there are no bytes to read, return 0 bytes read
	if (bufsz == 0){
        debug("bufsz was 0");
		return 0;
    }
    // if the request size is not aligned, return not supported
    if(bufsz % dev->blksz != 0){
        debug("Buf Size not valid");
        return -ENOTSUP;
    }

    // try to acquire the lock
    lock_acquire(&vio_lock);

    // Read in sectors until we've read the requested number of bytes
    while (bufsz > 0) {
        // Calculate how much to read in this sector (up to `sector_size` or remaining bytes)
        sector_size = (bufsz < dev->blksz) ? bufsz : dev->blksz;

        debug("Reading sector %u into address %p with chunk size %lu", dev->bufblkno, buf, sector_size);
        // perform a read on a single block
        result = operation_single_blk(dev, dev->bufblkno, sector_size, VIRTIO_BLK_T_IN);
        // ensure that the read happened without an error
        if(result < 0){
            debug("Error with read");
            // release the lock
            lock_release(&vio_lock);
            return -EIO;
        }
        // copy the reading sector to buf
        memcpy(buf, dev->blkbuf, dev->blksz);
        // update the number of bytes we have left to read
        bytes_read += result;
        buf += result;
        bufsz -= result;
        dev->bufblkno += 1;
        dev->pos += dev->blksz;
//        debug("Total bytes read so far: %d", bytes_read);
    }

    // release the lock
    lock_release(&vio_lock);
    // return the number of bytes successfully read
    return bytes_read;
}

/*
Inputs: struct io_intf *restrict io: pointer to io_intf strcut io
        void *restrict buf: pointer to the data buffer to write to
        unsigned long n: number of bytes to write to buf
Output: number of bytes successfully written to buf
Effect: Signals for a condtion_wait and therefore switches threads. Also fills out data descriptor of dev
        and sets flags for the descriptors and virtqueues to allow for notification.
Description: Writes a single block (size 512 bytes) of data. Achieves this by following the steps laid out in 
            Section 7.7.13 of the specifications. The thread sleeps while waiting for the device to make the
            buffer used, and this is signaled using the virtqueue condtion used_updated.
*/
long vioblk_write(struct io_intf *restrict io, const void *restrict buf, unsigned long n) {
    struct vioblk_device * const dev =(void*)io - offsetof(struct vioblk_device, io_intf);
    long bytes_written = 0;
    int sector = 0;
    unsigned long sector_size;
    long result;

    // calculate the start sector based on pos
    dev->bufblkno = dev->pos / (uint64_t) dev->blksz;

    // assert requirements for write
	trace("%s(n=%ld)", __func__, n);
	assert (io != NULL);
	assert (dev->opened);

    // if device is supposed to be read-only, return an error
    if(dev->readonly)
        return -EIO;
    
    // if n is 0, there are no bytes to write
    if(n == 0)
        return 0;

    // try to acquire the lock
    lock_acquire(&vio_lock);

    // Read in sectors until we've read the requested number of bytes
    while (n > 0) {
        // Calculate how much to read in this sector (up to `sector_size` or remaining bytes)
        sector_size = (n < dev->blksz) ? n : dev->blksz;
        // copy data from the buffer into the block buffer
        memcpy(dev->blkbuf, buf, dev->blksz);

        debug("Writing to sector %d from address %p with chunk size %lu", sector, buf, sector_size);
        // write a simgle block
        result = operation_single_blk(dev, sector, sector_size, VIRTIO_BLK_T_OUT);
        // ensure that the write did not produce an error
        if(result < 0){
            debug("Error with write");
            // release the lock
            lock_release(&vio_lock);
            return -EIO;
        }

        // update the remaining number of bytes to be written
        bytes_written += result;
        buf += result;
        n -= result;
        sector += 1;
        dev->pos += result;
//        debug("Posititon: %u", dev->pos);

//        debug("Total bytes written so far: %d", bytes_written);
    }

    // release the lock
    lock_release(&vio_lock);
    // return the number of bytes successfully read
    return bytes_written;
}

/*
Inputs: struct vioblk_device: pointer to the vioblk_device dev
        uint32_t sector: the sector being read from
        uint8_t * data: the data to read from
        unsigned long datasz: the size of the data
Output: Number of bytes successfully read from data
Effect: Signals for a condtion_wait and therefore switches threads. Also fills out data descriptor of dev
        and sets flags for the descriptors and virtqueues to allow for notification.
Description: Reads/writes a single block (size 512 bytes) of data. Achieves this by following the steps laid out in 
            Section 7.7.13 of the specifications. The thread sleeps while waiting for the device to make the
            buffer used, and this is signaled using the virtqueue condtion used_updated.
*/
long operation_single_blk(struct vioblk_device * dev, uint32_t sector, unsigned long datasz, uint32_t type){

    struct vioblk_request_header * request = (void *)dev->vq.desc[1].addr;
    uint8_t * status = (uint8_t *)dev->vq.desc[3].addr;
    int saved_intr_state;

    // make sure notifications are enabled
    dev->vq.avail.flags = 0;
    // clear the status bit
    *status = 0;

    // set sector number and type for request
    request->type = type;
    request->sector = sector;

    if(type == VIRTIO_BLK_T_IN)
        dev->vq.desc[2].flags |= VIRTQ_DESC_F_WRITE;

    // place index of head of descriptor into next ring entry of avail virtqueue
    dev->vq.avail.ring[dev->vq.avail.idx % VIRTIO_QUEUE_SZ] = 0;
    // perform memory barrier to ensure device sees updates table and available ring
    __sync_synchronize();
    // avail idx increased by number of descriptor heads added to avail
    dev->vq.avail.idx += 1;
    // perform memory barrier to ensure device updates idx field
    __sync_synchronize();

    saved_intr_state = intr_disable();
    // notify device 
    virtio_notify_avail(dev->regs, 0);

    // thread sleeps while waiting for used vitrqueue to update
    condition_wait(&dev->vq.used_updated);
    intr_restore(saved_intr_state);
    //check status
    if(*status != VIRTIO_BLK_S_OK){
        debug("Error: VIRTIO status= %d", *status);
        return -EIO;
    }
    //return dev->vq.used.ring[dev->vq.used.idx % dev->regs->queue_num].len;
    return dev->blksz;
}

int vioblk_ioctl(struct io_intf *restrict io, int cmd, void *restrict arg)
{
    struct vioblk_device *const dev = (void *)io -
                                      offsetof(struct vioblk_device, io_intf);

    trace("%s(cmd=%d,arg=%p)", __func__, cmd, arg);

    switch (cmd)
    {
    case IOCTL_GETLEN:
        return vioblk_getlen(dev, arg);
    case IOCTL_GETPOS:
        return vioblk_getpos(dev, arg);
    case IOCTL_SETPOS:
        return vioblk_setpos(dev, arg);
    case IOCTL_GETBLKSZ:
        return vioblk_getblksz(dev, arg);
    default:
        return -ENOTSUP;
    }
}

/*
Inputs: int irqno: interrupt request number
        void * aux: auxillary function pointer
Outputs: None
Effect: May broadcast the virtqueue condition used_updated, sets the interrupt acknowledge bit on the auxillary device
Description: Broadcasts the used_updated condition when the used virtqueue has signaled it had updated using the 
            interrupt_status bit. Also acknowledge that the interrupt has happened.
*/
void vioblk_isr(int irqno, void * aux) {
    struct vioblk_device * const dev = aux;
    // bit 0 of interrupt_status is the device saying that the used buffer has been notified, so check if this has happened
    if (dev->regs->interrupt_status & 1) {
        condition_broadcast(&dev->vq.used_updated);
    }
    
    // acknowledge that used buffer notification has been handled
    dev->regs->interrupt_ack = dev->regs->interrupt_status;
    debug("Interrupt acknowledged for IRQ %d", irqno);
}

/*
Inputs: const struct vioblk_device * dev: the device to get the size of
        uint64_t * lenptr: a pointer to the length
Outputs: The size of the device in bytes
Effect: None
Description: Finds and returns the device size
*/
int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr) {
    // return the device size in bytes
    *lenptr = dev->size;
    return dev->size;
}

/*
Inputs: const struct vioblk_device * dev: the device to get the size of
        uint64_t * posptr: a pointer to the position
Outputs: The current position of the device
Effect: None
Description: Finds and returns the device position
*/
int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr) {
    // return the current position in the disk which is currently being written to or read from
    *posptr = dev->pos;
    return dev->pos;
}

/*
Inputs: const struct vioblk_device * dev: the device to get the size of
        uint64_t * posptr: a pointer to the position
Outputs: The new position of the device
Effect: None
Description: Changes the position of the device
*/
int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr) {
    // set the current position in the disk which is currently being written to or read from
    lock_acquire(&vio_lock);
    dev->pos = *posptr;
    // return that position
    lock_release(&vio_lock);
    return dev->pos;
}

/*
Inputs: const struct vioblk_device * dev: the device to get the size of
        uint64_t * blkszptr: a pointer to the block size
Outputs: the block size in bytes
Effect: none
Description: finds and returns the block size
*/
int vioblk_getblksz (const struct vioblk_device * dev, uint32_t * blkszptr){
    // return the device block size
    *blkszptr = dev->blksz;
    return dev->blksz;
}

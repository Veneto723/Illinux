#include "console.h"
#include "heap.h"
#include "timer.h"
#include "kfs.c"
#include "uart.h"
#include "fs.h"

extern char _companion_f_start[];
extern char _companion_f_end[];

#define COM1_BASE 0x10000100

/**
 * void test_iolit(void);
 *
 * Helper function. Test all functionality in iolit: init, read, write, and ioctl;
 *
 * Inputs/Outputs:
 *          None.
 * Side Effects:
 *          Malloc two buffer for testing.
 */
void test_iolit(void) {
    struct io_lit lit;
    struct io_intf *io;
    size_t size = _companion_f_end - _companion_f_start;
    debug("f_start address: %p\n", _companion_f_start);
    debug("f_end point address: %p\n", _companion_f_end);
    debug("companion size: %lu\n", size);

    uint8_t* read_buf = kmalloc(size);
    uint8_t* write_buf = kmalloc(size);

    // Initialize the write buffer with known test data
    for (size_t i = 0; i < size; i++) {
        if (i % size == 0) {
            write_buf[i] = (uint8_t) 3;
        }
        if (i % size == 1) {
            write_buf[i] = (uint8_t) 9;
        }
        if (i % size == 3) {
            write_buf[i] = (uint8_t) 1;
        }
    }

    // initialize iolit
    io = iolit_init(&lit, _companion_f_start, size);

    // Test read operation
    int ret = io->ops->read(io, read_buf, size);
    if (ret != size) {
        debug("iolit read fails, only read %lu data \n", ret);
    } else {
        debug("iolit read succeeds, read %lu data \n", ret);
    }

    int pos = 0;
    io->ops->ctl(io, IOCTL_SETPOS, &pos);

    // Test write operation
    ret = io->ops->write(io, write_buf, size);
    if (ret != size) {
        debug("iolit write fails, only wrote %lu data \n", ret);
    } else {
        debug("iolit write succeeds, wrote %lu data \n", ret);
    }


    // Verify the written data
    io->ops->ctl(io, IOCTL_SETPOS, &pos);
    ret = io->ops->read(io, read_buf, size);
    uint32_t data_matches = 0;
    for (size_t i = 0; i < size; i++) {
        if (read_buf[i] != write_buf[i]) {
            data_matches ++;
            debug("Data mismatch at byte %lu: expected %u, got %u\n", i, write_buf[i], read_buf[i]);
            break;
        }
    }
    if (data_matches == 0) {
        debug("Write Data check passed, read data matches written data.\n");
    } else {
        debug("Write Data check failed.\n");
    }

    // ioctl tests
    io->ops->ctl(io, IOCTL_GETPOS, &pos);

    if (pos == size) {
        debug("GETPOS passed, current position = %lu.\n", pos);
    } else {
        debug("GETPOS failed, current position = %lu.\n", pos);
    }


    uint32_t len = 0;

    io->ops->ctl(io, IOCTL_GETLEN, &len);
    debug("GETLEN, current len = %lu.\n", len);

    // set an arbitary length
    len = 10;
    io->ops->ctl(io, IOCTL_SETLEN, &len);
    debug("SETLEN, set len to %lu.\n", len);

    // verify the set len
    io->ops->ctl(io, IOCTL_GETLEN, &len);
    debug("GETLEN, current len = %lu.\n", len);

    // close io
    io->ops->close(io);
}


/**
 * void check_file_list(void);
 *
 * Helper function. Iterate all files in kfs and print the file that is in-use.
 *
 * Inputs/Outputs/Side Effects:
 *          None.
 */
void check_file_list() {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (file_list[i].flags == F_IN_USE) {
            debug("file_list[%d] is in use (inode: %u)\n", i, file_list[i].inode_number);
        }
    }
}


/**
 * void test_kfs();
 *
 * Helper function. Test all functionality in kfs: mount, open, read, write, and ioctl;
 *
 * Inputs/Outputs:
 *          None.
 * Side Effects:
 *          Malloc buffers for testing.
 */
void test_kfs() {
    struct io_lit lit;
    struct io_intf *io;
    // number of the blk in the simple disk. One bootblock, 2 inode blocks, 3 data blocks.
    int num_boot = 1;
    int num_dentry = 2;
    int num_inodes = 2;
    int num_data = 3;
    size_t disk_size = (FS_BLKSZ) * (num_boot + num_inodes + num_data);
    uint8_t *disk_buffer = kmalloc(disk_size);
    memset(disk_buffer, 0, disk_size); // Initialize all bytes to zero

    // Set up bootblock in disk_buffer
    boot_block_t *boot_block = (boot_block_t *)disk_buffer;
    boot_block->num_dentry = num_dentry;
    boot_block->num_inodes = num_inodes;
    boot_block->num_data = num_data;
    strncpy(boot_block->dir_entries[0].file_name, "hello", FS_NAMELEN);
    boot_block->dir_entries[0].inode = 0; // Inode 0 for "hello"
    strncpy(boot_block->dir_entries[1].file_name, "test", FS_NAMELEN);
    boot_block->dir_entries[1].inode = 1; // Inode 1 for "test"

    // Set up inode list in disk_buffer
    inode_t *inode_list = (inode_t *)(disk_buffer + FS_BLKSZ);
    // Inode 0 for "hello" (1 data block)
    inode_list[0].byte_len = 13; // Length of data in bytes "Hello, World!"
    inode_list[0].data_block_num[0] = 0; // First data block index
    // Inode 1 for "test" (2 data blocks)
    inode_list[1].byte_len = FS_BLKSZ * 2;
    inode_list[1].data_block_num[0] = 1; // First data block index
    inode_list[1].data_block_num[1] = 2; // Second data block index

    // Set up data blocks in disk_buffer
    data_block_t *data_area = (data_block_t *)(disk_buffer + FS_BLKSZ * (1 + 2));
    const char *hello_data = "Hello, World!";
    memcpy(data_area[0].data, hello_data, strlen(hello_data));
    for (int i = 0; i < FS_BLKSZ; i++) {
        data_area[1].data[i] = 'A'; // Fill the first block with 'A'
        data_area[2].data[i] = 'B'; // Fill the second block with 'B'
    }

    // initialize iolit
    io = iolit_init(&lit, disk_buffer, disk_size);
    // mount disk
    int ret = fs_mount(io);
    if (ret != 0) {
        debug("Mount fail. Error: %d\n", ret);
        kfree(disk_buffer);
        return;
    } else {
        debug("Mount succeed.\n");
    }
    check_file_list();
    struct io_intf *file_io;
    ret = fs_open("hello", &file_io);
    check_file_list();
    // Open and test "hello"
    if (ret == 0) {
        char buf[20] = {0};
        fs_read(file_io, buf, sizeof(buf));
        debug("Read from 'hello': %s\n", buf);
        uint32_t pos = 7;
        fs_ioctl(file_io, IOCTL_SETPOS, &pos);
        memset(buf, 0, sizeof(buf));
        fs_read(file_io, buf, sizeof(buf));
        debug("Read from 'hello': %s\n", buf);
        pos = 7;
        fs_ioctl(file_io, IOCTL_SETPOS, &pos);
        // write extra data
        const char *new_content = "ECE391 NOT ALLOWED TO EXTEND ITS LEN";
        fs_write(file_io, new_content, strlen(new_content));
        fs_close(file_io);
        fs_open("hello", &file_io);
        memset(buf, 0, sizeof(buf));
        fs_read(file_io, buf, sizeof(buf));
        debug("Read from 'hello': %s\n", buf);
        fs_close(file_io);
    } else {
        debug("Failed to open 'hello'. Error: %d\n", ret);
    }

    check_file_list();
    // Open and test "test"
    ret = fs_open("test", &file_io);
    check_file_list();
    if (ret == 0) {
        char buf[8192] = {0}; // Buffer to read 2 full blocks (8192 bytes)
        fs_read(file_io, buf, sizeof(buf));
        char *out = kmalloc(8193);
        strncpy(out, buf, 8192);
        out[8192] = '\0';
        debug("Read from 'test': %s\n", out);
        // write some data 
        const char *new_content ="!@#$%^&*()123456789";
        uint32_t pos = FS_BLKSZ - 10;
        fs_ioctl(file_io, IOCTL_SETPOS, &pos);
        fs_write(file_io, new_content, strlen(new_content));
        pos = 0;
        // read the new datablock
        fs_ioctl(file_io, IOCTL_SETPOS, &pos);
        fs_read(file_io, buf, sizeof(buf));
        strncpy(out, buf, 8192);
        out[8192] = '\0';
        debug("Read from 'test': %s\n", out);
        fs_close(file_io);
    } else {
        debug("Failed to open 'test'. Error: %d\n", ret);
    }
    check_file_list();
    // IOCTL test
    debug("IOCTL TEST\n");
    ret = fs_open("hello", &file_io);
    uint32_t ioctl_buf = 10;
    fs_ioctl(file_io, IOCTL_GETBLKSZ, &ioctl_buf);
    debug("IOCTL_GETBLKSZ: %d", ioctl_buf);
    ioctl_buf = 5;
    fs_ioctl(file_io, IOCTL_SETPOS, &ioctl_buf);
    debug("IOCTL_SETPOS: %d", ioctl_buf);
    fs_ioctl(file_io, IOCTL_GETPOS, &ioctl_buf);
    debug("IOCTL_GETPOS: %d", ioctl_buf);
    fs_ioctl(file_io, IOCTL_GETLEN, &ioctl_buf);
    debug("IOCTL_GETLEN: %d", ioctl_buf);
    // test invalid IOCTL
    fs_ioctl(file_io, -391, &ioctl_buf);
    fs_close(file_io);
}


extern char _kimg_end[]; // end of kernel image (defined in kernel.ld)

#ifndef RAM_SIZE
#ifdef RAM_SIZE_MB
#define RAM_SIZE (RAM_SIZE_MB * 1024 * 1024)
#else
#define RAM_SIZE (8 * 1024 * 1024)
#endif
#endif

#ifndef RAM_START
#define RAM_START 0x80000000UL
#endif

/**
 * int main(void);
 *
 * Main test function, calling unit test functions
 *
 * Inputs:
 *          None.
 * Outputs:
 *          dont care.
 * Side Effects:
 *          None.
 *
 */
int main(void)
{
    console_init();
    heap_init(_kimg_end, (void *)0x80000000 + RAM_SIZE);
    test_iolit();
    test_kfs();
}

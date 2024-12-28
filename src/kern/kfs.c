//           kfs.c - Kernal File system
//

#include "io.h"
#include "error.h"
#include "heap.h"
#include "string.h"
#include "console.h"
#include <stdint.h>
#include "lock.h"


#define FS_NAMELEN      32      // max file name length
#define FS_BLKSZ        4096    // each block is 4KB
#define MAX_DENTRY_NUM  63      // 4KB block = 64B boot block + 63 * 64B dentry block
#define MAX_OPEN_FILES  32      // each task can have up to 32 open files
#define F_IN_USE        1       // file is in-use
#define F_NOT_USE       0       // file is not in-use
#define MAX_DB_PER_INODE 1023   // max number of the datablock per inode due to the restriction of block size

//           INTERNAL TYPE DEFINITIONS
//

// File structure
typedef struct {
    struct io_intf io;          // IO interface for file operations
    uint32_t file_pos;          // Current position in the file
    uint32_t file_size;         // Total size of the file in bytes
    uint32_t inode_number;      // Inode number for the file
    uint32_t flags;             // In-use status flag
} file_t;

// Dentry structure
typedef struct {
    char file_name[FS_NAMELEN]; // file name
    uint32_t inode;             // index of the inode in inode list
    uint8_t reserved[28];
}__attribute((packed)) dentry_t;

// Boot block structure
typedef struct {
    uint32_t num_dentry;        // number of dentry
    uint32_t num_inodes;        // number of inodes
    uint32_t num_data;          // number of data blocks
    uint8_t reserved[52];
    dentry_t dir_entries[MAX_DENTRY_NUM];   // dentries
} __attribute__((packed)) boot_block_t;

// Inode structure
typedef struct {
    uint32_t byte_len;          // length in Byte
    uint32_t data_block_num[MAX_DB_PER_INODE];  // ith data block index in the data block list
}__attribute((packed)) inode_t;

// Data block
typedef struct {
    uint8_t data[FS_BLKSZ];     // data in the data block
}__attribute((packed)) data_block_t;



//           INTERNAL FUNCTION DECLARATIONS
//

// Takes the name of the file to be opened and modifies the given pointer to contain the io_intf of the file.
int fs_open(const char * name, struct io_intf ** ioptr);
// Marks the file descriptor associated with io as unused.
void fs_close(struct io_intf* io);
// Writes n bytes from buf into the file associated with io. Updates metadata in the file descriptor as appropriate. Use fs open to get io.
long fs_write(struct io_intf* io, const void* buf, unsigned long n);
// Reads n bytes from the file associated with io into buf. Updates metadata in the file descriptor as appropriate. Use fs open to get io.
long fs_read(struct io_intf* io, void* buf, unsigned long n);
// Performs a device-specific function based on cmd. Note, ioctl functions should return values by using arg.
int fs_ioctl(struct io_intf* io, int cmd, void* arg);

// Helper function for fs_ioctl. Returns the length of the file.
static int fs_getlen(file_t* fd, void* arg);
// Helper function for fs_ioctl. Returns the current position in the file.
static int fs_getpos(file_t* fd, void* arg);
// Helper function for fs_ioctl. Sets the current position in the file.
static int fs_setpos(file_t* fd, void* arg);
// Helper function for fs_ioctl. Returns the block size of the filesystem.
static int fs_getblksz(file_t* fd, void* arg);
// Helper function for fd_ioctl. Return file_t correspond to the io_intf.
static file_t* get_fd_by_io(struct io_intf* io);
// Helper function for fs_mount. Initialize the file_list
static int initialize_file_list();
// Helper function for fs_open. Find a space in file_list and mark it as in-use
static file_t* allocate_file(uint32_t inode_number);
// Helper function. Release the fd_desc, set all values to initialize value.
static int release_file(file_t* fd);
// Helper function. Get the inode from kfs.raw by the inode_number.
static int update_inode(uint32_t inode_number);
// Helper function. Get the data block from kfs.raw by the data_block_num.
static int update_data_block(uint32_t data_block_num);
// write the updated data block back to disk
static int write_data_block(uint32_t data_block_idx);
// write the updated inode back to disk
static int write_inode(uint32_t inode_number);
//// Helper function, get a 4KB data block from vioblk
//static int read_block(struct io_intf* io, void* block);
//// Helper function, write a data into 512B vioblk
//static int write_block(struct io_intf* io, const void* data);

//           INTERNAL VARIABLES DECLARATIONS
//

static boot_block_t boot_block;                     // pointer to the bootblock we loaded in fs_mount
static inode_t inode;                               // the inode the file is using right now
static data_block_t data_block;                     // the data block the file is using right now
static file_t file_list[MAX_OPEN_FILES];            // file array holding the in-use files
static struct io_intf* disk_io;                     // disk_io pointer to get the inodes and data bloks later
static struct lock kfs_lock;                        // kfs lock

// file system io operation struct
static const struct io_ops fs_io_ops = {
    .close = fs_close,
    .read = fs_read,
    .write = fs_write,
    .ctl = fs_ioctl
};


/**
 * int fs_mount(struct io intf* io);
 *
 * Takes an io_intf* to the filesystem provider and sets up the filesystem for future fs open operations.
 * Once you complete this checkpoint, io will come from the vioblk device struct.
 *
 * Disk layout:
 * [ boot block | inodes | data blocks ]
 *
 * Inputs:
 *          io - struct io_intf *, pointer to the io interface struct.
 * Outputs:
 *          return 0, on success.
 *          return -EINVAL, if io is NULL.
 *          return -ENOMEM, if fails in kmalloc
 *          return -EIO, if fails in IO related operations
 * Side Effects:
 *          None.
 */
int fs_mount(struct io_intf * io) {
    if (io == NULL) return -EINVAL;

    // initialize the lock
    lock_init(&kfs_lock, "kfs_lock");

    // Set the position to the beginning of the file
    uint64_t offset = 0;
    int ret = io->ops->ctl(io, IOCTL_SETPOS, &offset);
    if (ret < 0) {
        return ret;
    }
    // initialize the file decriptor array
    ret = initialize_file_list();
    if (ret == -EIO) {
        return -EIO;
    }
    // read the boot block from kfs.raw
    ret = io->ops->read(io, &boot_block, FS_BLKSZ);
    // fail to read the bootblock
    if (ret != FS_BLKSZ) {
        debug("Reading bootblock fail. ret=%d", ret);
        return -EIO;
    }
    // debug print
    debug("number of dentry in bootblock: %d", boot_block.num_dentry);
    debug("number of inodes in bootblock: %d", boot_block.num_inodes);
    debug("number of data in bootblock: %d", boot_block.num_data);

    // store the kfs.raw io for later use.
    disk_io = io;
    return 0;
}

/**
 * int fs_open(const char * name, struct io_intf ** ioptr);
 *
 * Takes the name of the file to be opened and modifies the given pointer to contain the io_intf of the
 * file. This function should also associate a specific file descriptor with the file and mark it as in-use.
 * The user program will use this io_intf to interact with the file.
 *
 * Inputs:
 *          name - const char *, name of the file we need to open.
 *          ioptr - a double pointer that will point to a point of the io_intf of the file
 * Outputs:
 *          return 0, on success.
 *          return -EINVAL, if parameter value is invalid
 *          return -ENOENT, if cannot find a file with such name
 *          return -EBUSY, if the fd_dec_list is full (all fd_desc_t is in-use)
 * Side Effects:
 *          overwrite the ioptr to contain the io_intf of the file.
 */
int fs_open(const char * name, struct io_intf ** ioptr) {
    debug("Running fs_open on %s", name);
    if (name == NULL || ioptr == NULL) {
        return -EINVAL;
    }
    lock_acquire(&kfs_lock);
    int inode_number = -1;
    // find the corresponding inode
    for (uint32_t i = 0; i < boot_block.num_dentry; i++) {
        if (strncmp(boot_block.dir_entries[i].file_name, name, FS_NAMELEN) == 0) {
            inode_number = boot_block.dir_entries[i].inode;
            break;
        }
    }
    // can not find the file with the name
    if (inode_number == -1) {
        debug("Can not find the file with name %s", name);
        lock_release(&kfs_lock);
        return -ENOENT;
    }
    debug("testtttt");
    debug("testtttt");
    debug("testtttt");
    debug("testtttt");
    debug("testtttt");
    debug("testtttt");
    debug("testtttt");
    debug("testtttt");
    // debug("testtttt");

    // update the inode based on inode_number
    int ret = update_inode(inode_number);
    // fail to get inode
    if (ret < 0) {
        
        lock_release(&kfs_lock);
        return -EIO;
    }

    // allocate a file descriptor
    file_t *fd = allocate_file((uint32_t)inode_number);
    if (fd == NULL) {
        debug("No available file descriptor.");

        lock_release(&kfs_lock);
        return -EBUSY;
    }
    // assign the fs operations to the file descriptor
    fd->io.ops = &fs_io_ops;
    // assign it to the ioptr
    *ioptr = &fd->io;
    debug("Open file: %s (inode=%d)", name, inode_number);

    lock_release(&kfs_lock);
    return 0;
}

/**
 * void fs_close(struct io_intf* io);
 *
 * Marks the file descriptor associated with io as unused.
 *
 * Inputs:
 *          io - struct io_intf*, pointer to a io interface struct.
 * Outputs:
 *          None.
 * Side effect:
 *          None.
 */
void fs_close(struct io_intf* io) {
    // get the corresponding file_t by io
    file_t *fd = get_fd_by_io(io);
    // check if it is NULL
    // but theoritically it shouldnt be NULL, since it will be called using fd_desc_t.io->close
    if (fd != NULL) {
        trace("fs_close: Close file (inode: %u, current pos: %u, file size: %u)\n", fd->inode_number, fd->file_pos, fd->file_size);
        // marks the file as unused.
        release_file(fd);
    }
}

/**
 * long fs_write(struct io_intf* io, const void* buf, unsigned long n);
 *
 * Writes n bytes from buf into the file associated with io. Updates metadata in the file descriptor as appropriate.
 *
 * Inputs:
 *          io - struct io_intf*, pointer of the io interface
 *          buf - const void*, the data buf contains the data we need to write
 *          n - unsigned long, number of bytes we need to write
 *
 * Outputs:
 *          return the number of written bytes on success.
 *          return -EINVAL, if paramaters are invalid.
 *          return -EIO, if can not get fd or inode, or if there is no more data block to write data
 * Side Effects:
 *          modify the file descriptor related of the file if needed.
 *          modify the inode of the file if needed
 *          use more data block if needed.
 */
long fs_write(struct io_intf* io, const void* buf, unsigned long n) {
    // check inputs are valid
    if (io == NULL || buf == NULL) {
        return -EINVAL;
    }
    // if nothing needs to be written, just return
    if (n == 0) return n;
    // try to acquire the lock
    lock_acquire(&kfs_lock);
    // get the corresponding file_t by io
    file_t *fd = get_fd_by_io(io);
    // check if it is NULL
    // but theoritically it shouldnt be NULL, since it will be called using fd_desc_t.io->write
    if (fd == NULL) {
        // release the lock
        lock_release(&kfs_lock);
        return -EIO;
    }

    trace("fs_write: Write %lu bytes to file (inode: %u, current pos: %u, file size: %u)\n",
          n, fd->inode_number, fd->file_pos, fd->file_size);

    // get inode from inode list
    if (fd->inode_number >= boot_block.num_inodes) {
        // release the lock
        lock_release(&kfs_lock);
        return -EIO;
    }
    // update the inode based on the inode_number
    int ret = update_inode(fd->inode_number);
    if (ret < 0) {
        // release the lock
        lock_release(&kfs_lock);
        return -EIO;
    }

    // Calculate the number of allocated data blocks (ceiling divide)
    uint32_t allocated_blocks = (inode.byte_len + FS_BLKSZ - 1) / FS_BLKSZ;

    const uint8_t *write_buf = (const uint8_t*) buf; // data type of data in the datablock is uint_8

    // denote the number of bytes we have written
    unsigned long written_bytes = 0;
    // loop until we finish writing
    while (written_bytes < n) {
        // get the current offset we are writing to
        uint32_t byte_offset = fd->file_pos + written_bytes;
        // get the index of data block in the inode->data_block_num we are writing to
        uint32_t block_idx = byte_offset / FS_BLKSZ;
        // get the offset in that data block
        uint32_t block_offset = byte_offset % FS_BLKSZ;

        // check if block_idx is within allocated data blocks, block_idx starts from 0
        if (block_idx >= allocated_blocks || block_idx >= MAX_DB_PER_INODE) {
            break;
        }

        // load the data block
        ret = update_data_block(inode.data_block_num[block_idx]);
        // fail to get next data block
        if (ret < 0) {
            // release the lock
            lock_release(&kfs_lock);
            return -EIO;
        }

        // calculate the remaining space in the data block
        uint32_t bytes_in_block = FS_BLKSZ - block_offset;
        // calculate the number of byte hasnt written
        uint32_t bytes_left_to_write = n - written_bytes;
        // calculate the number of bytes will write
        uint32_t bytes_to_copy = (bytes_in_block < bytes_left_to_write) ? bytes_in_block : bytes_left_to_write;

        // copy data into the data block
        memcpy(data_block.data + block_offset, write_buf + written_bytes, bytes_to_copy);

        // write the data block back to disk
        ret = write_data_block(inode.data_block_num[block_idx]);
        if (ret < 0) {
            // release the lock
            lock_release(&kfs_lock);
            return -EIO;
        }

        // increase the written_bytes
        written_bytes += bytes_to_copy;
    }

    // update file descriptor
    fd->file_pos += written_bytes;

    // Write the inode back to disk
    ret = write_inode(fd->inode_number);
    if (ret < 0) {
        // release the lock
        lock_release(&kfs_lock);
        return -EIO;
    }

    // release the lock
    lock_release(&kfs_lock);

    return written_bytes;
}

/**
 * long fs_read(struct io_intf* io, void* buf, unsigned long n);
 *
 * Reads n bytes from the file associated with io into buf. Updates metadata in the file descriptor as appropriate.
 *
 *
 * Inputs:
 *          io - struct io_intf*, pointer of the io interface
 *          buf - const void*, the data buf that will be loaded the data we need to read
 *          n - unsigned long, number of bytes we need to read
 *
 * Outputs:
 *          return the number of read bytes on success.
 *          return -EINVAL, if paramaters are invalid.
 *          return -EIO, if can not get fd or inode, or if there is no more data block to write data
 * Side Effects:
 *          modifies the file_pos in file_t
 */
long fs_read(struct io_intf* io, void* buf, unsigned long n) {
    // check inputs are valid
    if (io == NULL || buf == NULL) {
        return -EINVAL;
    }
    // if nothing needs to be read, just return
    if (n == 0) return n;

    // try to acquire the lock
    lock_acquire(&kfs_lock);
    // get the corresponding file_t by io
    file_t *fd = get_fd_by_io(io);
    // check if it is NULL
    // but theoritically it shouldnt be NULL, since it will be called using fd_desc_t.io->write
    if (fd == NULL) {
        // release the lock
        lock_release(&kfs_lock);
        return -EIO;
    }

    trace("fs_read: Reading %lu bytes from file (inode: %u, current pos: %u, file size: %u)\n",
          n, fd->inode_number, fd->file_pos, fd->file_size);
    // check if the file position is beyond the file size
    if (fd->file_pos >= fd->file_size) {
        // release the lock
        lock_release(&kfs_lock);
        return 0; // End of file
    }

    // check the inode number
    if (fd->inode_number >= boot_block.num_inodes) {
        // release the lock
        lock_release(&kfs_lock);
        return -EIO;
    }
    // get the inode based on the inode_number
    int ret = update_inode(fd->inode_number);
    if (ret < 0) {
        // release the lock
        lock_release(&kfs_lock);
        return -EIO;
    }

    // Calculate the number of allocated data blocks (ceiling divide)
    uint32_t allocated_blocks = (inode.byte_len + FS_BLKSZ - 1) / FS_BLKSZ;

    // calculate the number of bytes that can be read
    unsigned long bytes_remaining = fd->file_size - fd->file_pos;
    unsigned long bytes_to_read = (n < bytes_remaining) ? n : bytes_remaining;

    uint8_t *read_buf = (uint8_t *)buf; // data type of data in the datablock is uint_8

    // denote the number of bytes we have read
    unsigned long read_bytes = 0;
    // loop until we finish reading
    while (read_bytes < bytes_to_read) {
        // get the current offset we are read from
        uint32_t byte_offset = fd->file_pos + read_bytes;
        // get the index of data block in the inode->data_block_num we are read from
        uint32_t block_idx = byte_offset / FS_BLKSZ;
        // get the offset in that data block
        uint32_t block_offset = byte_offset % FS_BLKSZ;

        // check if block_idx is within allocated data blocks
        if (block_idx >= allocated_blocks || block_idx >= MAX_DB_PER_INODE) {
            // release the lock
            lock_release(&kfs_lock);
            return -EIO; // Invalid block index
        }

        // get the data_block_num from inode
        uint32_t data_block_idx = inode.data_block_num[block_idx];
        if (data_block_idx >= boot_block.num_data) {
            // release the lock
            lock_release(&kfs_lock);
            return -EIO; // Invalid data block number
        }

        // get the data block
        ret = update_data_block(data_block_idx);
        if (ret < 0) {
            // release the lock
            lock_release(&kfs_lock);
            return -EIO;
        }

        // calculate the remaining space in the data block
        uint32_t bytes_in_block = FS_BLKSZ - block_offset;
        // calculate the number of byte hasnt written
        unsigned long bytes_left_to_read = bytes_to_read - read_bytes;
        // calculate the number of bytes will write
        uint32_t bytes_to_copy = (bytes_in_block < bytes_left_to_read) ? bytes_in_block : bytes_left_to_read;

        // copy data into the buffer
        memcpy(read_buf + read_bytes, data_block.data + block_offset, bytes_to_copy);

        // increase the read_bytes
        read_bytes += bytes_to_copy;
    }

    // update file descriptor
    fd->file_pos += read_bytes;

    // release the lock
    lock_release(&kfs_lock);

    return read_bytes;
}

/**
 * int fs_ioctl(struct io_intf* io, int cmd, void* arg);
 *
 * Performs a device-specific function based on cmd. Note, ioctl functions should return values by using arg.
 *
 * Inputs:
 *          io - struct io_intf*, pointer to the io interface.
 *          cmd - command for ioctrol, see io.h for more details
 *          arg - argument needed to be pass to ioctl functions, return value will be stored in it.
 * Outputs:
 *          return 0 if suceeds. otherwise, return negative error numbers.
 * Side Effects:
 *          May modify file descriptor's values.
 */
int fs_ioctl(struct io_intf* io, int cmd, void* arg) {
    // sanity check, also avoid dereference a nullptr
    if (io == NULL || arg == NULL) return -EINVAL;
    // based on the cmd, choose the correct local helper function.
    switch (cmd) {
        case IOCTL_GETLEN:
            return fs_getlen(get_fd_by_io(io), arg);
        case IOCTL_GETPOS:
            return fs_getpos(get_fd_by_io(io), arg);
        case IOCTL_SETPOS:
            return fs_setpos(get_fd_by_io(io), arg);
        case IOCTL_GETBLKSZ:
            return fs_getblksz(get_fd_by_io(io), arg);
        default:
            debug("Not supported IOCTL");
            return -ENOTSUP;
    }
    return -EINVAL;
}

/**
 * static int initialize_file_list();
 *
 * Initializes the file descriptor list by releasing all file descriptors.
 *
 * Inputs:
 *          None.
 * Outputs:
 *          return 0 on success.
 * Side Effects:
 *          all entries in the file descriptor list are marked as not in use.
 */
static int initialize_file_list() {
    // iterate through all file descriptors
    for (uint32_t i = 0; i < MAX_OPEN_FILES; i++) {
        release_file(&file_list[i]); // return value is ignored, since it will be in the range
    }
    return 0;
}

/**
 * static file_t* allocate_file(uint32_t inode_number);
 *
 * Helper function. Finds a unused space in file_list and marks it as in-use.
 *
 * Inputs:
 *          inode_number - uint32_t, the inode number for which to allocate the file descriptor.
 * Outputs:
 *          return a pointer to the allocated file on success.
 *          return NULL if no available file is found or if the inode_number is invalid.
 * Side Effects:
 *          marks an entry in the file list as in use and initializes it.
 */
static file_t* allocate_file(uint32_t inode_number) {
    if (inode_number > boot_block.num_inodes) {
        return NULL; // wrong inode number
    }
    // iterate through all file descriptors
    for (uint32_t i = 0; i < MAX_OPEN_FILES; i++) {
        // find a fd that is not in-use
        if (file_list[i].flags == F_NOT_USE) {
            // assign its values correspondingly
            file_list[i].inode_number = inode_number;
            file_list[i].file_pos = 0;
            file_list[i].file_size = inode.byte_len;
            file_list[i].flags = F_IN_USE;
            return &file_list[i];
        }
    }
    // cannot find an available one, return NULL
    return NULL;
}

/**
 * static int release_file(file_t* dt);
 *
 * releases the given file, marking it as not in use.
 *
 * Inputs:
 *          dt - file_t*, pointer to the file to release.
 * Outputs:
 *          return 0 on success.
 *          return -EINVAL if the file pointer is NULL.
 * Side Effects:
 *          resets the values in file to their default values.
 */
static int release_file(file_t* dt) {
    if (dt == NULL) {
        return -EINVAL;
    }
    // set the value to default value.
    dt->flags = F_NOT_USE;
    dt->io.ops = NULL;
    dt->file_pos = 0;
    dt->file_size = 0;
    dt->inode_number = 0; // inode_number is unsigned, so we can not set it to -1.
    return 0;
}

/**
 * static file_t* get_fd_by_io(struct io_intf* io);
 *
 * Gets the file associated with the given io interface.
 *
 * Inputs:
 *          io - struct io_intf*, pointer to the io interface.
 * Outputs:
 *          return a pointer to the corresponding file descriptor if found.
 *          return NULL if not found or if io is NULL.
 * Side Effects:
 *          None.
 */
static file_t* get_fd_by_io(struct io_intf* io) {
    if (io == NULL) return NULL;
    // io is the first element in file_t struct, so we can simply cast it and return
    return (file_t*) io;
}

/**
 * static int fs_getlen(file_t* fd, void* arg);
 *
 * Helper function for fs_ioctl.
 *
 * Inputs:
 *          fd - file_t*, pointer to the file.
 *          arg - void*, store the return value.
 * Outputs:
 *          return 0 on success.
 *          return -EINVAL if fd is NULL or arg is NULL.
 * Side Effects:
 *          None.
 */
static int fs_getlen(file_t* fd, void* arg) {
    // sanity check, also avoid dereference a nullptr
    if (fd == NULL || arg == NULL) return -EINVAL;
    // assign the file size to arg
    *((uint32_t *)arg) = fd->file_size;
    return 0;
}

/**
 * static int fs_getpos(file_t* fd, void* arg);
 *
 * Helper function for fs_ioctl. Returns the current position in the file.
 *
 * Inputs:
 *          fd - file_t*, pointer to the file.
 *          arg - void*, store the return value.
 * Outputs:
 *          return 0 on success.
 *          return -EINVAL if fd is NULL or arg is NULL.
 * Side Effects:
 *          None.
 */
static int fs_getpos(file_t* fd, void* arg) {
    // sanity check, also avoid dereference a nullptr
    if (fd == NULL || arg == NULL) return -EINVAL;
    // assign the file pos to arg
    *((uint32_t *)arg) = fd->file_pos;
    return 0;
}

/**
 * static int fs_setpos(file_t* fd, void* arg);
 *
 * Helper function for fs_ioctl. Sets the current position in the file.
 *
 * Inputs:
 *          fd - file_t*, pointer to the file descriptor.
 *          arg - void*, pointer to the new position value.
 * Outputs:
 *          return 0 on success.
 *          return -EINVAL if fd or arg is NULL, or if the position is invalid.
 * Side Effects:
 *          Updates the file's file position.
 */
static int fs_setpos(file_t* fd, void* arg) {
    // sanity check, also avoid dereference a nullptr
    if (fd == NULL || arg == NULL) return -EINVAL;
    // type conversion
    lock_acquire(&kfs_lock);
    uint32_t pos = *((uint32_t*) arg);
    // check pos is valid, it needs to be in between [0, fd->file_size]
    if (pos > fd->file_size) {
        lock_release(&kfs_lock);
        return -EINVAL;
    }
    // assign the new file pos
    fd->file_pos = pos;
    lock_release(&kfs_lock);
    return 0;
}

/**
 * static int fs_getblksz(file_t* fd, void* arg);
 *
 * Helper function for fs_ioctl. Returns the block size of the filesystem.
 *
 * Inputs:
 *          fd - file_t*, pointer to the file descriptor.
 *          arg - void*, store the return value..
 * Outputs:
 *          return block size on success.
 *          return -EINVAL if fd is NULL or arg is NULL.
 * Side Effects:
 *          None.
 */
static int fs_getblksz(file_t* fd, void* arg) {
    // sanity check, also avoid dereference a nullptr
    if (fd == NULL || arg == NULL) return -EINVAL;
    // assign arg equals to FS_BLKSZ
    *((uint32_t *)arg) = FS_BLKSZ;
    return 0;
}

/**
 * static int update_inode(uint32_t inode_number);
 *
 * Helper function. Get the inode from kfs.raw by the inode_number.
 *
 * Inputs:
 *          inode_number - uint32_t, index of the inode in the inode list.
 * Outputs:
 *          return 0 on success.
 *          return -EIO if fails to read the disk_io to get inode.
 * Side Effects:
 *          update the inode.
 */
static int update_inode(uint32_t inode_number) {
    uint32_t inode_offset = FS_BLKSZ; // boot block occupies one block
    inode_offset += inode_number * FS_BLKSZ;
    int ret = disk_io->ops->ctl(disk_io, IOCTL_SETPOS, &inode_offset);
    if (ret < 0) {
        return -EIO;
    }
    // read the inode from kfs.raw
    ret = disk_io->ops->read(disk_io, &inode, FS_BLKSZ);
    // check inode reading
    if (ret != FS_BLKSZ) {
        debug("Reading inode fail. ret=%d", ret);
        return -EIO;
    }
    return 0;
}

/**
 * static int update_data_block(uint32_t data_block_idx);
 *
 * Helper function. Get the data block from kfs.raw by the data_block_idx.
 *
 * Inputs:
 *          data_block_idx - uint32_t, index of the data block we need to get from disk_io
 * Outputs:
 *          return 0 on success.
 *          return -EIO if fails to read data block from data block list.
 * Side Effects:
 *          update the data_block.
 */
static int update_data_block(uint32_t data_block_idx) {
    // data block list starts after inode list in the disk
    // boot_block_size + inode_numbers(N) * block_size
    uint32_t data_block_offset = FS_BLKSZ + (boot_block.num_inodes) * FS_BLKSZ;
    data_block_offset += data_block_idx * FS_BLKSZ;
    // set the offset to the start of the data block
    int ret = disk_io->ops->ctl(disk_io, IOCTL_SETPOS, &data_block_offset);
    if (ret < 0) {
        return -EIO;
    }
    // read the data block from kfs.raw
    ret = disk_io->ops->read(disk_io, &data_block, FS_BLKSZ);
    // check data block reading
    if (ret != FS_BLKSZ) {
        debug("Reading data block fail. ret=%d", ret);
        return -EIO;
    }
    return 0;
}

/**
 * static int write_data_block(uint32_t data_block_idx);
 *
 * write the updated data block back to disk.
 *
 * Inputs:
 *          data_block_idx - uint32_t, data block idx of the data block we need to write to
 * Outputs:
 *          return 0 on success.
 *          return -EIO if write fails.
 * Side Effects:
 *          change the data block value in the disk.
 */
static int write_data_block(uint32_t data_block_idx) {
    // calculate the data block list offset
    uint32_t data_block_offset = FS_BLKSZ + (boot_block.num_inodes * FS_BLKSZ);
    // increment the offset to the beginning of the data block we need to write to
    data_block_offset += data_block_idx * FS_BLKSZ;
    // set the offset
    int ret = disk_io->ops->ctl(disk_io, IOCTL_SETPOS, &data_block_offset);
    if (ret < 0) {
        return -EIO;
    }
    // write the data block
    ret = disk_io->ops->write(disk_io, &data_block, FS_BLKSZ);
    if (ret != FS_BLKSZ) {
        debug("Writing data block failed. ret=%d", ret);
        return -EIO;
    }
    return 0;
}

/**
 * static int write_inode(uint32_t inode_number);
 *
 * write the updated inode back to disk
 *
 * Inputs:
 *          inode_number - uint32_t, inode number of the inode we need to write to
 * Outputs:
 *          return 0 on success.
 *          return -EIO if write fails.
 * Side Effects:
 *          change the data block value in the disk.
 */
static int write_inode(uint32_t inode_number) {
    // calculate the inode list offset
    uint32_t inode_offset = FS_BLKSZ + (inode_number * FS_BLKSZ);
    // increment the offset to the beginning of the inode we need to write to
    int ret = disk_io->ops->ctl(disk_io, IOCTL_SETPOS, &inode_offset);
    if (ret < 0) {
        return -EIO;
    }
    // set the offset
    ret = disk_io->ops->write(disk_io, &inode, FS_BLKSZ);
    if (ret < 0) {
        debug("Writing inode failed. ret=%d", ret);
        return -EIO;
    }
    return 0;
}

//           io.c 
//          

#include "io.h"
#include "string.h"
#include "error.h"

#include <stddef.h>
#include <stdint.h>

//           INTERNAL TYPE DEFINITIONS
//          

struct iovprintf_state {
    struct io_intf * io;
    int err;
};

//           INTERNAL FUNCTION DECLARATIONS
//          

static void ioterm_close(struct io_intf * io);
static long ioterm_read(struct io_intf * io, void * buf, size_t len);
static long ioterm_write(struct io_intf * io, const void * buf, size_t len);
static int ioterm_ioctl(struct io_intf * io, int cmd, void * arg);

static void iovprintf_putc(char c, void * aux);

// io interface functions for iolit
void iolit_close(struct io_intf* io);
long iolit_read(struct io_intf * io, void * buf, unsigned long bufsz);
long iolit_write(struct io_intf * io, const void * buf, unsigned long n);
int iolit_ioctl(struct io_intf * io, int cmd, void * arg);
// Helper function. return the io_lit of this io_intf
static struct io_lit* get_iolit_by_io(struct io_intf* io);

// Helper function for fs_ioctl. Sets the length of the file.
static int lit_setlen(struct io_lit* lit, void* arg);
// Helper function for fs_ioctl. Returns the length of the file.
static int lit_getlen(struct io_lit* lit, void* arg);
// Helper function for fs_ioctl. Returns the current position in the file.
static int lit_getpos(struct io_lit* lit, void* arg);
// Helper function for fs_ioctl. Sets the current position in the file.
static int lit_setpos(struct io_lit* lit, void* arg);


// io lit operation struct
static const struct io_ops ops = {
    .close = iolit_close,
    .read = iolit_read,
    .write = iolit_write,
    .ctl = iolit_ioctl,
};


//           EXPORTED FUNCTION DEFINITIONS
//          

long ioread_full(struct io_intf * io, void * buf, unsigned long bufsz) {
    long cnt, acc = 0;

    if (io->ops->read == NULL)
        return -ENOTSUP;

    while (acc < bufsz) {
        cnt = io->ops->read(io, buf+acc, bufsz-acc);
        if (cnt < 0)
            return cnt;
        else if (cnt == 0)
            return acc;
        acc += cnt;
    }

    return acc;
}

long iowrite(struct io_intf * io, const void * buf, unsigned long n) {
    long cnt, acc = 0;

    if (io->ops->write == NULL)
        return -ENOTSUP;

    while (acc < n) {
        cnt = io->ops->write(io, buf+acc, n-acc);
        if (cnt < 0)
            return cnt;
        else if (cnt == 0)
            return acc;
        acc += cnt;
    }

    return acc;
}

//           Initialize an io_lit. This function should be called with an io_lit, a buffer, and the size of the device.
//           It should set up all fields within the io_lit struct so that I/O operations can be performed on the io_lit
//           through the io_intf interface. This function should return a pointer to an io_intf object that can be used 
//           to perform I/O operations on the device.
/**
 * struct io_intf * iolit_init (struct io_lit * lit, void * buf, size_t size);
 *
 * Initialize the io_lit and return the constructed io_intf.
 *
 * Inputs:
 *          lit - struct io_lit*, pointer of the io lit we need to construct
 *          buf - void*, buf that will be treated as a file
 *          size - size_t, size of the file
 * Outputs:
 *          struct io_inft* points the constructed io_inf 
 * Side Effects:
 *          None.
 */
struct io_intf * iolit_init (struct io_lit * lit, void * buf, size_t size) {
    // assign the values in io_list struct
    lit->buf = buf;
    lit->size = size;
    lit->pos = 0;
    // construct io_ops
    lit->io_intf.ops = &ops;
    // return the iointf
    return &lit->io_intf;
}

/**
 * void iolit_close(struct io_intf* io);
 *
 * Close the iolit.
 *
 * Inputs:
 *          io - struct io_intf*, pointer of the io interface
 *
 * Outputs:
 *          None.
 * Side Effects:
 *          Clear the io_lit of the io_intf.
 */
void iolit_close(struct io_intf* io) {
    // get the io_lit struct
    struct io_lit *lit = get_iolit_by_io(io);
    // check if it is NULL
    if (lit == NULL) {
        return;
    } 
    // clear the struct
    lit->io_intf.ops = NULL;
    lit->buf = NULL;
    lit->pos = 0;
    lit->size = 0;
}

/**
 * long iolit_read(struct io_intf * io, void * buf, unsigned long bufsz);
 *
 * Reads n bytes from the file associated with io into buf. Updates metadata in the iolit as appropriate.
 *
 * Inputs:
 *          io - struct io_intf*, pointer of the io interface
 *          buf - const void*, the data buf that will be loaded the data we need to read
 *          bufsz - unsigned long, number of bytes we need to read
 *
 * Outputs:
 *          return the number of read bytes on success.
 *          return -EINVAL, if paramaters are invalid.
 *          return -EIO, if can not get iolit
 * Side Effects:
 *          modifies data in the iolit
 */
long iolit_read(struct io_intf * io, void * buf, unsigned long bufsz) {
    // check valid inputs
    if (buf == NULL || bufsz == 0) {
        return -EINVAL;
    }
    // get the io_lit struct
    struct io_lit *lit = get_iolit_by_io(io);
    // check if it is NULL
    if (lit == NULL) {
        return -EIO;
    } 

    // calculate the number of bytes that can be read
    unsigned long bytes_remaining = lit->size - lit->pos;
    unsigned long bytes_to_read = (bufsz < bytes_remaining) ? bufsz : bytes_remaining;

    // copy data into the buffer
    memcpy(buf, lit->buf + lit->pos, bytes_to_read);
    // update the lit pos
    lit->pos += bytes_to_read;
    // write the # of bytes read
    return bytes_to_read;
}

/**
 * long iolit_write(struct io_intf * io, const void * buf, unsigned long n);
 *
 * Writes n bytes from buf into the iolit associated with io. Updates metadata in the iolit as appropriate.
 *
 * Inputs:
 *          io - struct io_intf*, pointer of the io interface
 *          buf - const void*, the data buf contains the data we need to write
 *          n - unsigned long, number of bytes we need to write
 *
 * Outputs:
 *          return the number of written bytes on success.
 *          return -EINVAL, if paramaters are invalid.
 *          return -EIO, if can not get iolit
 * Side Effects:
 *          modify the iolit related of the io if needed.
 *          modify the inode of the file if needed
 *          use more data block if needed.
 */
long iolit_write(struct io_intf * io, const void * buf, unsigned long n) {
    // check valid inputs
    if (buf == NULL || n == 0) {
        return -EINVAL;
    }
    // get the io_lit struct
    struct io_lit *lit = get_iolit_by_io(io);
    // check if it is NULL
    if (lit == NULL) {
        return -EIO;
    } 

    // calculate the number of bytes that can be read
    unsigned long bytes_remaining = lit->size - lit->pos;
    unsigned long bytes_to_write = (n < bytes_remaining) ? n : bytes_remaining;
    // copy data into the buffer
    memcpy(lit->buf + lit->pos, buf, bytes_to_write);
    // update the lit pos
    lit->pos += bytes_to_write;
    // return the # of bytes written
    return bytes_to_write;
}

/**
 * int iolit_ioctl(struct io_intf * io, int cmd, void * arg);
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
 *          May modify iolit's values.
 */
int iolit_ioctl(struct io_intf * io, int cmd, void * arg) {
    // sanity check, also avoid dereference a nullptr
    if (io == NULL || arg == NULL) return -EINVAL;
    // based on the cmd, choose the correct local helper function.
    switch (cmd) {
        case IOCTL_SETLEN:
            return lit_setlen(get_iolit_by_io(io), arg);
        case IOCTL_GETLEN:
            return lit_getlen(get_iolit_by_io(io), arg);
        case IOCTL_GETPOS:
            return lit_getpos(get_iolit_by_io(io), arg);
        case IOCTL_SETPOS:
            return lit_setpos(get_iolit_by_io(io), arg);
        default:
            return -ENOTSUP;
    }
    return -EINVAL;
}


/**
 * static int lit_setlen(struct io_lit* lit, void* arg);
 *
 * Helper function for iolit_ioctl. Sets the size of the file.
 *
 * Inputs:
 *          lit - struct io_lit*, pointer to the iolit.
 *          arg - void*, new size.
 * Outputs:
 *          return 0 on success.
 *          return -EINVAL if fd is NULL or arg is NULL.
 * Side Effects:
 *          None.
 */
static int lit_setlen(struct io_lit* lit, void* arg) {
    // sanity check, also avoid dereference a nullptr
    if (lit == NULL || arg == NULL) return -EINVAL;
    // type conversion
    uint32_t size = *((uint32_t*) arg);
    // assign the new file size
    lit->size = size;
    return 0;
}

/**
 * static int lit_getlen(struct io_lit* lit, void* arg);
 *
 * Helper function for iolit_ioctl.  Returns the size of the file.
 *
 * Inputs:
 *          lit - struct io_lit*, pointer to the iolit.
 *          arg - void*, store the return value.
 * Outputs:
 *          return 0 on success.
 *          return -EINVAL if fd is NULL or arg is NULL.
 * Side Effects:
 *          None.
 */
static int lit_getlen(struct io_lit* lit, void* arg) {
    // sanity check, also avoid dereference a nullptr
    if (lit == NULL || arg == NULL) return -EINVAL;
    // assign the file size to arg
    *((uint32_t *)arg) = lit->size;
    return 0;
}

/**
 * static int lit_getpos(struct io_lit* lit, void* arg);
 *
 * Helper function for iolit_ioctl. Returns the current position in the file.
 *
 * Inputs:
 *          lit - struct io_lit*, pointer to the iolit.
 *          arg - void*, store the return value.
 * Outputs:
 *          return 0 on success.
 *          return -EINVAL if fd is NULL or arg is NULL.
 * Side Effects:
 *          None.
 */
static int lit_getpos(struct io_lit* lit, void* arg) {
    // sanity check, also avoid dereference a nullptr
    if (lit == NULL || arg == NULL) return -EINVAL;
    // assign the file pos to arg
    *((uint32_t *)arg) = lit->pos;
    return 0;
}

/**
 * static int lit_setpos(struct io_lit* lit, void* arg);
 *
 * Helper function for iolit_ioctl. Sets the current position in the file.
 *
 * Inputs:
 *          lit - struct io_lit*, pointer to the io lit.
 *          arg - void*, pointer to the new position value.
 * Outputs:
 *          return 0 on success.
 *          return -EINVAL if fd or arg is NULL, or if the position is invalid.
 * Side Effects:
 *          Updates the iolit's position.
 */
static int lit_setpos(struct io_lit* lit, void* arg) {
    // sanity check, also avoid dereference a nullptr
    if (lit == NULL || arg == NULL) return -EINVAL;
    // type conversion
    uint32_t pos = *((uint32_t*) arg);
    // check pos is valid, it needs to be in between [0, lit->size]
    if (pos > lit->size) {
        return -EINVAL;
    }
    // assign the new file pos
    lit->pos = pos;
    return 0;
}

/**
 * static struct io_lit* get_iolit_by_io(struct io_intf* io);
 *
 * Gets the iolit associated with the given io interface.
 *
 * Inputs:
 *          io - struct io_intf*, pointer to the io interface.
 * Outputs:
 *          return a pointer to the corresponding iolit.
 *          return NULL if not found or if io is NULL.
 * Side Effects:
 *          None.
 */
static struct io_lit* get_iolit_by_io(struct io_intf* io) {
    if (io == NULL) {
        return NULL;
    }
    // io is the first element in io_lit struct, so we can simply cast it and return
    return (struct io_lit*) io;
}

//           I/O term provides three features:
//          
//               1. Input CRLF normalization. Any of the following character sequences in
//                  the input are converted into a single \n:
//          
//                      (a) \r\n,
//                      (b) \r not followed by \n,
//                      (c) \n not preceeded by \r.
//          
//               2. Output CRLF normalization. Any \n not preceeded by \r, or \r not
//                  followed by \n, is written as \r\n. Sequence \r\n is written as \r\n.
//          
//               3. Line editing. The ioterm_getsn function provides line editing of the
//                  input.
//          
//           Input CRLF normalization works by maintaining one bit of state: cr_in.
//           Initially cr_in = 0. When a character ch is read from rawio:
//           
//           if cr_in = 0 and ch == '\r': return '\n', cr_in <- 1;
//           if cr_in = 0 and ch != '\r': return ch;
//           if cr_in = 1 and ch == '\r': return \n;
//           if cr_in = 1 and ch == '\n': skip, cr_in <- 0;
//           if cr_in = 1 and ch != '\r' and ch != '\n': return ch, cr_in <- 0.
//          
//           Ouput CRLF normalization works by maintaining one bit of state: cr_out.
//           Initially, cr_out = 0. When a character ch is written to I/O term:
//          
//           if cr_out = 0 and ch == '\r': output \r\n to rawio, cr_out <- 1;
//           if cr_out = 0 and ch == '\n': output \r\n to rawio;
//           if cr_out = 0 and ch != '\r' and ch != '\n': output ch to rawio;
//           if cr_out = 1 and ch == '\r': output \r\n to rawio;
//           if cr_out = 1 and ch == '\n': no ouput, cr_out <- 0;
//           if cr_out = 1 and ch != '\r' and ch != '\n': output ch, cr_out <- 0.

struct io_intf * ioterm_init(struct io_term * iot, struct io_intf * rawio) {
    static const struct io_ops ops = {
        .close = ioterm_close,
        .read = ioterm_read,
        .write = ioterm_write,
        .ctl = ioterm_ioctl
    };

    iot->io_intf.ops = &ops;
    iot->rawio = rawio;
    iot->cr_out = 0;
    iot->cr_in = 0;

    return &iot->io_intf;
};

int ioputs(struct io_intf * io, const char * s) {
    const char nl = '\n';
    size_t slen;
    long wlen;

    slen = strlen(s);

    wlen = iowrite(io, s, slen);
    if (wlen < 0)
        return wlen;

    //           Write newline

    wlen = iowrite(io, &nl, 1);
    if (wlen < 0)
        return wlen;
    
    return 0;
}

long ioprintf(struct io_intf * io, const char * fmt, ...) {
	va_list ap;
	long result;

	va_start(ap, fmt);
	result = iovprintf(io, fmt, ap);
	va_end(ap);
	return result;
}

long iovprintf(struct io_intf * io, const char * fmt, va_list ap) {
    //           state.nout is number of chars written or negative error code
    struct iovprintf_state state = { .io = io, .err = 0 };
    size_t nout;

	nout = vgprintf(iovprintf_putc, &state, fmt, ap);
    return state.err ? state.err : nout;
}

char * ioterm_getsn(struct io_term * iot, char * buf, size_t n) {
    char * p = buf;
    int result;
    char c;

    for (;;) {
        //           already CRLF normalized
        c = iogetc(&iot->io_intf);

        switch (c) {
        //           escape
        case '\133':
            iot->cr_in = 0;
            break;
        //           should not happen
        case '\r':
        case '\n':
            result = ioputc(iot->rawio, '\r');
            if (result < 0)
                return NULL;
            result = ioputc(iot->rawio, '\n');
            if (result < 0)
                return NULL;
            *p = '\0';
            return buf;
        //           backspace
        case '\b':
        //           delete
        case '\177':
            if (p != buf) {
                p -= 1;
                n += 1;
                
                result = ioputc(iot->rawio, '\b');
                if (result < 0)
                    return NULL;
                result = ioputc(iot->rawio, ' ');
                if (result < 0)
                    return NULL;
                result = ioputc(iot->rawio, '\b');
            } else
                //           beep
                result = ioputc(iot->rawio, '\a');
            
            if (result < 0)
                return NULL;
            break;

        default:
            if (n > 1) {
                result = ioputc(iot->rawio, c);
                *p++ = c;
                n -= 1;
            } else
                //           beep
                result = ioputc(iot->rawio, '\a');
            
            if (result < 0)
                return NULL;
        }
    }
}

//           INTERNAL FUNCTION DEFINITIONS
//          

void ioterm_close(struct io_intf * io) {
    struct io_term * const iot = (void*)io - offsetof(struct io_term, io_intf);
    ioclose(iot->rawio);
}

long ioterm_read(struct io_intf * io, void * buf, size_t len) {
    struct io_term * const iot = (void*)io - offsetof(struct io_term, io_intf);
    char * rp;
    char * wp;
    long cnt;
    char ch;

    do {
        //           Fill buffer using backing io interface

        cnt = ioread(iot->rawio, buf, len);

        if (cnt < 0)
            return cnt;
        
        //           Scan though buffer and fix up line endings. We may end up removing some
        //           characters from the buffer.  We maintain two pointers /wp/ (write
        //           position) and and /rp/ (read position). Initially, rp = wp, however, as
        //           we delete characters, /rp/ gets ahead of /wp/, and we copy characters
        //           from *rp to *wp to shift the contents of the buffer.
        //           
        //           The processing logic is as follows:
        //           if cr_in = 0 and ch == '\r': return '\n', cr_in <- 1;
        //           if cr_in = 0 and ch != '\r': return ch;
        //           if cr_in = 1 and ch == '\r': return \n;
        //           if cr_in = 1 and ch == '\n': skip, cr_in <- 0;
        //           if cr_in = 1 and ch != '\r' and ch != '\n': return ch, cr_in <- 0.

        wp = rp = buf;
        while ((void*)rp < buf+cnt) {
            ch = *rp++;

            if (iot->cr_in) {
                switch (ch) {
                case '\r':
                    *wp++ = '\n';
                    break;
                case '\n':
                    iot->cr_in = 0;
                    break;
                default:
                    iot->cr_in = 0;
                    *wp++ = ch;
                }
            } else {
                switch (ch) {
                case '\r':
                    iot->cr_in = 1;
                    *wp++ = '\n';
                    break;
                default:
                    *wp++ = ch;
                }
            }
        }

    //           We need to return at least one character, however, it is possible that
    //           the buffer is still empty. (This would happen if it contained a single
    //           '\n' character and cr_in = 1.) If this happens, read more characters.
    } while (wp == buf);

    return (wp - (char*)buf);
}

long ioterm_write(struct io_intf * io, const void * buf, size_t len) {
    struct io_term * const iot = (void*)io - offsetof(struct io_term, io_intf);
    //           how many bytes from the buffer have been written
    long acc = 0;
    //           everything up to /wp/ in buffer has been written out
    const char * wp;
    //           position in buffer we're reading
    const char * rp;
    long cnt;
    char ch;

    //           Scan through buffer and look for cases where we need to modify the line
    //           ending: lone \r and lone \n get converted to \r\n, while existing \r\n
    //           are not modified. We can't modify the buffer, so mwe may need to do
    //           partial writes.
    //           The strategy we want to implement is:
    //           if cr_out = 0 and ch == '\r': output \r\n to rawio, cr_out <- 1;
    //           if cr_out = 0 and ch == '\n': output \r\n to rawio;
    //           if cr_out = 0 and ch != '\r' and ch != '\n': output ch to rawio;
    //           if cr_out = 1 and ch == '\r': output \r\n to rawio;
    //           if cr_out = 1 and ch == '\n': no ouput, cr_out <- 0;
    //           if cr_out = 1 and ch != '\r' and ch != '\n': output ch, cr_out <- 0.

    wp = rp = buf;

    while ((void*)rp < buf+len) {
        ch = *rp++;
        switch (ch) {
        case '\r':
            //           We need to emit a \r\n sequence. If it already occurs in the
            //           buffer, we're all set. Otherwise, we need to write what we have
            //           from the buffer so far, then write \n, and then continue.
            if ((void*)rp < buf+len && *rp == '\n') {
                //           The easy case: buffer already contains \r\n, so keep going.
                iot->cr_out = 0;
                rp += 1;
            } else {
                //           Next character is not '\n' or we're at the end of the buffer.
                //           We need to write out what we have so far and add a \n.
                cnt = iowrite(iot->rawio, wp, rp - wp);
                if (cnt < 0)
                    return cnt;
                else if (cnt == 0)
                    return acc;
                
                acc += cnt;
                wp += cnt;

                //           Now output \n, which does not count toward /acc/.
                cnt = ioputc(iot->rawio, '\n');
                if (cnt < 0)
                    return cnt;
                
                iot->cr_out = 1;
            }
                
            break;
        
        case '\n':
            //           If last character was \r, skip the \n. This should only occur at
            //           the beginning of the buffer, because we check for a \n after a
            //           \r, except if \r is the last character in the buffer. Since we're
            //           at the start of the buffer, we don't have to write anything out.
            if (iot->cr_out) {
                iot->cr_out = 0;
                wp += 1;
                break;
            }
            
            //           Previous character was not \r, so we need to write a \r first,
            //           then the rest of the buffer. But before that, we need to write
            //           out what we have so far, up to, but not including the \n we're
            //           processing.
            if (wp != rp-1) {
                cnt = iowrite(iot->rawio, wp, rp-1 - wp);
                if (cnt < 0)
                    return cnt;
                else if (cnt == 0)
                    return acc;
                acc += cnt;
                wp += cnt;
            }
            
            cnt = ioputc(iot->rawio, '\r');
            if (cnt < 0)
                return cnt;
            
            //           wp should now point to \n. We'll write it when we drain the
            //           buffer later.

            iot->cr_out = 0;
            break;
            
        default:
            iot->cr_out = 0;
        }
    }

    if (rp != wp) {
        cnt = iowrite(iot->rawio, wp, rp - wp);

        if (cnt < 0)
            return cnt;
        else if (cnt == 0)
            return acc;
        acc += cnt;
    }

    return acc;
}

int ioterm_ioctl(struct io_intf * io, int cmd, void * arg) {
    struct io_term * const iot = (void*)io - offsetof(struct io_term, io_intf);

    //           Pass ioctls through to backing io interface. Seeking is not supported,
    //           because we maintain state on the characters output so far.
    if (cmd != IOCTL_SETPOS)
        return ioctl(iot->rawio, cmd, arg);
    else
        return -ENOTSUP;
}

void iovprintf_putc(char c, void * aux) {
    struct iovprintf_state * const state = aux;
    int result;

    if (state->err == 0) {
        result = ioputc(state->io, c);
        if (result < 0)
            state->err = result;
    }
}

#include "console.h"
#include "elf.h"
#include "io.h"
#include "thread.h"
#include "intr.h"
#include "heap.h"
#include "timer.h"
#include "uart.h"

extern char _companion_f_start[];
extern char _companion_f_end[];

// Size of e_ident[]
#define EI_NIDENT 16

typedef struct
{
    unsigned char e_ident[EI_NIDENT]; // ELF identification and other info
    uint16_t e_type;                  // Object file type
    uint16_t e_machine;               // Machine type
    uint32_t e_version;               // Object file version
    uint64_t e_entry;                 // Entry point virtual address
    uint64_t e_phoff;                 // Program header offset
    uint64_t e_shoff;                 // Section header offset
    uint32_t e_flags;                 // Processor-specific flags
    uint16_t e_ehsize;                // ELF header size
    uint16_t e_phentsize;             // Size of program header entry
    uint16_t e_phnum;                 // Number of program header entries
    uint16_t e_shentsize;             // Size of section header entry
    uint16_t e_shnum;                 // Number of section header entries
    uint16_t e_shstrndx;              // Section name string table index
} Elf64_Ehdr;

typedef struct
{
    uint32_t p_type;   // Type of segment
    uint32_t p_flags;  // Flags of segment
    uint64_t p_offset; // Offset in file
    uint64_t p_vaddr;  // Virtual address in memory
    uint64_t p_paddr;  // Physical address in memory
    uint64_t p_filesz; // Size of segment in file
    uint64_t p_memsz;  // Size of segment in memory
    uint64_t p_align;  // Alignment of segment
} Elf64_Phdr;

/*******************************************************************************
 * Function: test_load
 *
 * Description: Test to see if elf_load correctly loads content into memory
 *
 * Inputs: None
 *
 * Output: None
 *
 * Side Effects:
 * - Load elf from companion.o
 * - Calls elf_load to put content into memory
 * - Prints out content in file and memory
 ******************************************************************************/
void test_load(void)
{
    struct io_lit lit;
    struct io_intf *io;
    void (*entry_point)(struct io_intf *);
    size_t size = _companion_f_end - _companion_f_start;
    kprintf("f_start address: %p\n", _companion_f_start);
    kprintf("f_end point address: %p\n", _companion_f_end);
    kprintf("size: %lu\n", size);

    io = iolit_init(&lit, _companion_f_start, size);

    // elf_load
    elf_load(io, &entry_point);
    kprintf("Entry point address: %p\n", entry_point);

    // Original binary dump
    // kprintf("Original binary content:\n");
    // for (size_t i = 0; i < 128; i++)
    // { // First 128 bytes to see ELF header
    //     if (i % 16 == 0)
    //     {
    //         console_printf("\n%04lx: ", i);
    //     }
    //     console_printf("%02x ", (uint8_t)_companion_f_start[i]);
    // }
    // console_printf("\n");

    // Get ELF header
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)_companion_f_start;
    uint64_t entry_vaddr = ehdr->e_entry;

    kprintf("\nELF Header:\n");
    kprintf("   type:   0x%x\n", ehdr->e_type);
    kprintf("   entry point: 0x%x\n", ehdr->e_entry);
    kprintf("   machine type: %d\n", ehdr->e_machine);
    kprintf("   program header offset: 0x%x\n", ehdr->e_phoff);
    kprintf("   program header count: %d\n", ehdr->e_phnum);
    kprintf("   size of program header 0x%x\n", ehdr->e_phentsize);

    kprintf("\nProgram Headers:\n");
    Elf64_Phdr *phdrs = (Elf64_Phdr *)(_companion_f_start + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++)
    {
        kprintf("PHDR %d:\n", i);
        kprintf("   type:   0x%lx\n", phdrs[i].p_type);
        kprintf("   offset: 0x%lx\n", phdrs[i].p_offset);
        kprintf("   vaddr:  0x%lx\n", phdrs[i].p_vaddr);
        kprintf("   filesz: 0x%lx\n", phdrs[i].p_filesz);
    }

    kprintf("\n----------------------------------------------------------------------------------------------------------\n");

    for (int i = 0; i < ehdr->e_phnum; i++)
    {
        if (phdrs[i].p_type == 1) // PT_LOAD = 1
        {

            kprintf("\n\nOriginal file content of PHDR %d:", i);
            for (size_t j = 0; j < 1024; j++)
            {
                if (j % 32 == 0)
                {
                    console_printf("\n%08lx: ", phdrs[i].p_offset + j);
                }
                console_printf("%02x ", ((uint8_t *)_companion_f_start)[phdrs[i].p_offset + j]);
            }

            kprintf("\n\nLoaded content of PHDR %d:", i);
            for (int j = 0; j < 1024; j++)
            {
                if (j % 32 == 0)
                {
                    console_printf("\n%08lx: ", phdrs[i].p_vaddr + j);
                }
                console_printf("%02x ", *((uint8_t *)(phdrs[i].p_vaddr + j)));
            }
        }
        else
        {
            kprintf("PHDR %d not a PT_LOAD section\n", i);
        }
    }
    kprintf("\n----------------------------------------------------------------------------------------------------------\n");
    // kprintf("\nOriginal file content at entry:");
    // for (size_t j = 0; j < 1024; j++)
    // {
    //     if (j % 32 == 0)
    //     {
    //         console_printf("\n%08lx: ", 0);
    //     }
    //     console_printf("%02x ", ((uint8_t *)_companion_f_start)[0]);
    // }
    kprintf("\nLoaded content at entry:");
    for (int j = 0; j < 1024; j++)
    {
        if (j % 32 == 0)
        {
            console_printf("\n%08lx: ", entry_vaddr + j);
        }
        console_printf("%02x ", *((uint8_t *)(entry_vaddr + j)));
    }
    kprintf("\n");
}

// Invalid ELF header
static unsigned char invalid_elf[] = {
    0x7f, 'E', 'L', 'F',                            // e_ident[EI_MAG0-3]
    0x02,                                           // e_ident[EI_CLASS] = ELFCLASS64
    0x02,                                           // no longer little-endian
    0x01,                                           // e_ident[EI_VERSION] = EV_CURRENT
    0x00,                                           // e_ident[EI_OSABI] = ELFOSABI_NONE
    0x00,                                           // e_ident[EI_ABIVERSION] = 0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       // padding
    0x02, 0x00,                                     // e_type = ET_EXEC
    0xf3, 0x00,                                     // e_machine = EM_RISCV
    0x01, 0x00, 0x00, 0x00,                         // e_version = EV_CURRENT
    0x00, 0x00, 0x10, 0x80, 0x00, 0x00, 0x00, 0x00, // e_entry = 0x80100000
};

/*******************************************************************************
 * Function: test_header
 *
 * Description: Test to see if validation of elf header works
 *
 * Inputs: None
 *
 * Output: None
 *
 * Side Effects:
 * - Attempts to load invalid_elf[] into memory
 * - Prints out error code
 ******************************************************************************/
void test_header(void)
{
    struct io_lit lit;
    struct io_intf *io;
    void (*entry_point)(struct io_intf *);

    kprintf("Invalid ELF Header:\n");
    io = iolit_init(&lit, invalid_elf, sizeof(invalid_elf));
    kprintf("Return code: %d\n", elf_load(io, &entry_point));
}

//            end of kernel image (defined in kernel.ld)
extern char _kimg_end[];

#define RAM_SIZE (8 * 1024 * 1024)
#define RAM_START 0x80000000UL
#define KERN_START RAM_START
#define USER_START 0x80100000UL

int main(void)
{
    console_init();
    heap_init(_kimg_end, (void *)USER_START);
    kprintf("-----------Beginning Loading Test----------\n");
    test_load();
    kprintf("----------Beginning Header Test----------\n");
    test_header();
}
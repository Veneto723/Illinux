#include "console.h"
#include "elf.h"
#include "string.h"
#include "config.h"
#include "memory.h"

// Load address for ELF files
// #define LOAD_MIN 0x80100000 // Lower bound for loading
// #define LOAD_MAX 0x81000000 // Upper bound for loading

// Size of e_ident[]
#define EI_NIDENT 16

// ELF identification indexes
#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7
#define EI_PAD 8

// ELF magic numbers
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

// Architecture (e_ident[EI_CLASS])
#define ELFCLASS64 2 // 64-bit architecture

// Data type (e_ident[EI_DATA])
#define ELFDATA2LSB 1 // Two's complement, little-endian

// Version (e_ident[EI_VERSION] & e_version)
#define EV_CURRENT 1 // Current version

// Operating system and ABI (e_ident[EI_OSABI])
#define ELFOSABI_SYSV 0 // UNIX System V ABI

// Object file types (e_type)
#define ET_NONE 0 // No file type
#define ET_REL 1  // Relocatable file
#define ET_EXEC 2 // Executable file
#define ET_DYN 3  // Shared object file
#define ET_CORE 4 // Core file

// Machine type (e_machine)
#define EM_RISCV 243 // RISC-V machine

// Program header types (p_type)
#define PT_LOAD 1 // Loadable segment

// Segment permission flags (p_flags)
#define PF_X 0x1 // Execute
#define PF_W 0x2 // Write
#define PF_R 0x4 // Read

// Structure definitions
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

// Function declaration
static int verify_elf_header(const Elf64_Ehdr *ehdr);

/*******************************************************************************
 * Function: elf_load
 *
 * Description: Loads an executable elf into memory
 *
 * Inputs:
 * io (struct io_intf *) - io interface from which to load the elf
 * void (**entryptr)(struct io_intf *io) - function pointer elf load fills in w/ address of entry point
 *
 * Output:
 * (int) 0 if successful, negative error code if not
 *
 * Side Effects:
 * - Loads data segments with PT_LOAD==1 into memory
 * - Zero-fill parts where memsz>filesz
 * - fill in entry point of function pointer
 ******************************************************************************/
int elf_load(struct io_intf *io, void (**entryptr)(void))
{
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;
    int result;

    debug("Starting ELF load\n");

    // Seek to beginning of file
    uint64_t pos = 0;
    if (ioseek(io, pos) < 0)
    {
        debug("Failed to seek to start of file\n");
        return -EIO;
    }

    // Read ELF header
    if (ioread(io, &ehdr, sizeof(ehdr)) != sizeof(ehdr))
    {
        debug("Failed to read ELF header\n");
        return -EIO;
    }

    debug("ELF Header:\n");
    debug("   Entry point: 0x%lx\n", ehdr.e_entry);
    debug("   Program header offset: 0x%lx\n", ehdr.e_phoff);
    debug("   Program header count: %d\n", ehdr.e_phnum);

    // Verify ELF header
    if ((result = verify_elf_header(&ehdr)) < 0)
    {
        debug("ELF header verification failed: %d\n", result);
        return result;
    }

    // Set entry point
    *entryptr = (void (*)(void))(ehdr.e_entry);
    debug("Set entry point to: 0x%lx\n", (uintptr_t)*entryptr);

    // Process program headers
    for (int i = 0; i < ehdr.e_phnum; i++)
    {
        debug("\nProcessing program header %d\n", i);

        // Seek to program header
        pos = ehdr.e_phoff + (i * sizeof(phdr));
        debug("  Seeking to program header at offset: 0x%lx\n", pos);

        if (ioseek(io, pos) < 0)
        {
            debug("  Failed to seek to program header\n");
            return -EIO;
        }

        // Read program header
        if (ioread(io, &phdr, sizeof(phdr)) != sizeof(phdr))
        {
            debug("  Failed to read program header\n");
            return -EIO;
        }

        debug("  Program Header Info:\n");
        debug("     Type: 0x%lx\n", phdr.p_type);
        debug("     Flags: 0x%lx\n", phdr.p_flags);
        debug("     Offset: 0x%lx\n", phdr.p_offset);
        debug("     VAddr: 0x%lx\n", phdr.p_vaddr);
        debug("     PAddr: 0x%lx\n", phdr.p_paddr);
        debug("     FileSize: 0x%lx\n", phdr.p_filesz);
        debug("     MemSize: 0x%lx\n", phdr.p_memsz);
        debug("     Align: 0x%lx\n", phdr.p_align);

        // Only load PT_LOAD segments
        if (phdr.p_type != PT_LOAD)
        {
            debug("  Skipping non-PT_LOAD segment\n");
            continue;
        }

        // Verify memory range
        if (phdr.p_vaddr < USER_START_VMA || phdr.p_vaddr + phdr.p_memsz > USER_END_VMA)
        {
            debug("  Invalid virtual address\n");
            return -EINVAL;
        }

        // Seek to segment data
        pos = phdr.p_offset;
        debug("  Seeking to segment data at offset: 0x%lx\n", pos);

        if (ioseek(io, pos) < 0)
        {
            debug("  Failed to seek to segment data\n");
            return -EIO;
        }

        // Use virtual address to load
        uintptr_t load_addr = (uintptr_t)phdr.p_vaddr;
        debug("  Loading at address: 0x%lx\n", (uintptr_t)load_addr);

        uint_fast8_t perms = PTE_U | PTE_R | PTE_W;

        if (memory_alloc_and_map_range((uintptr_t) phdr.p_vaddr, phdr.p_memsz, perms) == NULL) {
            debug("memory allocate fails in elf loader");
            return -EBUSY;
        }
        // Load segment into memory
         size_t bytes_read = ioread_full(io, (void*) load_addr, phdr.p_filesz);
         debug("  Read %ld of %ld bytes\n", bytes_read, phdr.p_filesz);

         if (bytes_read != phdr.p_filesz)
         {
             debug("  Failed to read segment data\n");
             return -EIO;
         }

        // Zero-fill the remaining memory
        if (phdr.p_memsz > phdr.p_filesz)
        {
            size_t size = phdr.p_memsz - phdr.p_filesz;
            memset((void*)(load_addr + phdr.p_filesz), 0, size);
            debug("  Zero-filled %ld bytes\n", size);
        }
        // set the actucal page flag based on header flags
        perms = PTE_U;

        // COMMENT OUT THESE PORTIONS TO TEST PAGE FAULTING
        if (phdr.p_flags & PF_X)
            perms |= PTE_X;
        if (phdr.p_flags & PF_W)
            perms |= PTE_W;
        if (phdr.p_flags & PF_R)
            perms |= PTE_R;

        memory_set_range_flags((void*)load_addr, phdr.p_memsz, perms);
    }

    // test page faulting


    debug("ELF loading completed\n");
    return 0;
}

/*******************************************************************************
 * Function: verify_elf_header
 *
 * Description: Checks to see if the elf header is valid for our system
 *
 * Inputs:
 * ehdr (Elf64_Ehdr *) - Elf header struct
 *
 * Output:
 * (int) 0 if successful, negative error code if not
 *
 * Side Effects: None
 ******************************************************************************/
static int verify_elf_header(const Elf64_Ehdr *ehdr)
{
    // Check magic numbers
    // debug("Header Verification: Checking magic numbers\n");
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3)
    {
        debug("Invalid ELF magic number\n");
        return -EINVAL;
    }

    // Check for little-endian
    // debug("Header Verification: Checking data type\n");
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
    {
        debug("Not for two's complement, little-endian machine\n");
        return -EINVAL;
    }

    // Check machine architecture
    // debug("Header Verification: Checking machine architecture\n");
    if (ehdr->e_machine != EM_RISCV)
    {
        debug("Invalid machine type\n");
        return -EINVAL;
    }

    // Check type (should be executable)
    // debug("Header Verification: Checking object file type\n");
    if (ehdr->e_type != ET_EXEC)
    {
        debug("Not an executable\n");
        return -EINVAL;
    }

    // Validate program header offset and count
    // debug("Header Verification: Checking for program headers\n");
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0)
    {
        debug("No program headers found\n");
        return -EINVAL;
    }

    return 0;
}
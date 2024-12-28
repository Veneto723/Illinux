// memory.c - Memory management
//

#ifndef TRACE
#ifdef MEMORY_TRACE
#define TRACE
#endif
#endif

#ifndef DEBUG
#ifdef MEMORY_DEBUG
#define DEBUG
#endif
#endif

#include "config.h"

#include "memory.h"
#include "console.h"
#include "halt.h"
#include "heap.h"
#include "csr.h"
#include "string.h"
#include "error.h"
#include "thread.h"
#include "process.h"

#include <stdint.h>

// EXPORTED VARIABLE DEFINITIONS
//

char memory_initialized = 0;
uintptr_t main_mtag;

// IMPORTED VARIABLE DECLARATIONS
//

// The following are provided by the linker (kernel.ld)

extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// INTERNAL TYPE DEFINITIONS
//

union linked_page {
    union linked_page * next;
    char padding[PAGE_SIZE];
};

struct pte {
    uint64_t flags:8;
    uint64_t rsw:2;
    uint64_t ppn:44;
    uint64_t reserved:7;
    uint64_t pbmt:2;
    uint64_t n:1;
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN2(vma) (((vma) >> (9+9+12)) & 0x1FF)
#define VPN1(vma) (((vma) >> (9+12)) & 0x1FF)
#define VPN0(vma) (((vma) >> 12) & 0x1FF)
#define MIN(a,b) (((a)<(b))?(a):(b))

#define PTE_PER_LEVEL   512
#define ASID_SHIFT      44

// INTERNAL FUNCTION DECLARATIONS
//

static inline int wellformed_vma(uintptr_t vma);
static inline int wellformed_vptr(const void * vp);
static inline int aligned_addr(uintptr_t vma, size_t blksz);
static inline int aligned_ptr(const void * p, size_t blksz);
static inline int aligned_size(size_t size, size_t blksz);

static inline uintptr_t active_space_mtag(void);
static inline struct pte * mtag_to_root(uintptr_t mtag);
static inline struct pte * active_space_root(void);

static inline void * pagenum_to_pageptr(uintptr_t n);
static inline uintptr_t pageptr_to_pagenum(const void * p);

static inline void * round_up_ptr(void * p, size_t blksz);
static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz);
static inline size_t round_up_size(size_t n, size_t blksz);
static inline void * round_down_ptr(void * p, size_t blksz);
static inline size_t round_down_size(size_t n, size_t blksz);
static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz);

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag);
static inline struct pte null_pte(void);

static inline void sfence_vma(void);

// INTERNAL GLOBAL VARIABLES
//

static union linked_page * free_list;

static struct pte main_pt2[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt1_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt0_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));

// EXPORTED VARIABLE DEFINITIONS
//

// EXPORTED FUNCTION DEFINITIONS
// 

/*
 * Inputs: None
 * Outputs: None
 * Description: 
 *  1. Sets up page tables and performs virtual-to-physical 1:1 mapping of the kernel megapage.
 *      a. The first two gigabytes of memory are made gigapages and maps the MMIO region
 *      b. The next gigarange is split into megapages that represents the kernel text, read-only, and data regions
 *  2. Enables Sv39 paging
 *  3. Initializes the heap memory manager
 *  4. Puts free pages on the free pages list
 *  5. Allows S mode access of U mode memory
 * Effects:
*/

void memory_init(void) {
    const void * const text_start = _kimg_text_start;
    const void * const text_end = _kimg_text_end;
    const void * const rodata_start = _kimg_rodata_start;
    const void * const rodata_end = _kimg_rodata_end;
    const void * const data_start = _kimg_data_start;
    union linked_page * page;
    void * heap_start;
    void * heap_end;
    size_t page_cnt;
    uintptr_t pma;
    const void * pp;
    int i;

    trace("%s()", __func__);

    assert (RAM_START == _kimg_start);

    kprintf("           RAM: [%p,%p): %zu MB\n",
        RAM_START, RAM_END, RAM_SIZE / 1024 / 1024);
    kprintf("  Kernel image: [%p,%p)\n", _kimg_start, _kimg_end);

    // Kernel must fit inside 2MB megapage (one level 1 PTE)
    
    if (MEGA_SIZE < _kimg_end - _kimg_start)
        panic("Kernel too large");

    // Initialize main page table with the following direct mapping:
    // 
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // RAM_START to _kimg_end:           RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB
    
    // Identity mapping of two gigabytes (as two gigapage mappings)
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void*)pma, PTE_R | PTE_W | PTE_G);
    
    // Third gigarange has a second-level page table
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First physical megarange of RAM is mapped as individual pages with
    // permissions based on kernel image region.

    main_pt1_0x80000[VPN1(RAM_START_PMA)] =
        ptab_pte(main_pt0_0x80000, PTE_G);

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Enable paging. This part always makes me nervous.

    main_mtag =  // Sv39
        ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
        pageptr_to_pagenum(main_pt2);
    
    csrw_satp(main_mtag);
    sfence_vma();

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end;
    heap_end = round_up_ptr(heap_start, PAGE_SIZE);
    if (heap_end - heap_start < HEAP_INIT_MIN) {
        heap_end += round_up_size (
            HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end)
        panic("Not enough memory");
    
    // Initialize heap memory manager

    heap_init(heap_start, heap_end);

    kprintf("Heap allocator: [%p,%p): %zu KB free\n",
        heap_start, heap_end, (heap_end - heap_start) / 1024);

    free_list = heap_end; // heap_end is page aligned
    page_cnt = (RAM_END - heap_end) / PAGE_SIZE;

    kprintf("Page allocator: [%p,%p): %lu pages free\n",
        free_list, RAM_END, page_cnt);

    // Put free pages on the free page list
    for(i = 0; i < page_cnt; i++){
        // calculate where the current free page is
        page = (union linked_page *)((uintptr_t)free_list + i * PAGE_SIZE);

        // if there is room for a next page, set the next page to the next chunk of memory
        if(i != (page_cnt - 1))
            page->next = (union linked_page *)((uintptr_t)page + PAGE_SIZE);
        // otherwise, the next page should be NULL
        else
            page->next = NULL;
    }
    


    // Allow supervisor to access user memory. We could be more precise by only
    // enabling it when we are accessing user memory, and disable it at other
    // times to catch bugs.

    csrs_sstatus(RISCV_SSTATUS_SUM);

    memory_initialized = 1;
}

uintptr_t memory_space_create(uint_fast16_t asid){
    uintptr_t new_mtag;
    struct pte * new_pt0; 
    struct pte * new_pt1; 
    struct pte * new_pt2;

    // shallow copy kernel memory
    // since this memory is global, we can use the main global variables
    // MMIO
    new_pt2 = main_pt2;
    new_pt2->flags = main_pt2->flags;
    new_pt2->rsw = main_pt2->rsw;
    new_pt2->ppn = main_pt2->ppn;
    new_pt2->reserved = main_pt2->reserved;
    new_pt2->pbmt = main_pt2->pbmt;
    new_pt2->n = main_pt2->n;
    
    // RAM
    new_pt1 = main_pt1_0x80000;
    new_pt1->flags = main_pt1_0x80000->flags;
    new_pt1->rsw = main_pt1_0x80000->rsw;
    new_pt1->ppn = main_pt1_0x80000->ppn;
    new_pt1->reserved = main_pt1_0x80000->reserved;
    new_pt1->pbmt = main_pt1_0x80000->pbmt;
    new_pt1->n = main_pt1_0x80000->n;

    new_pt0 = main_pt0_0x80000;
    new_pt0->flags = main_pt0_0x80000->flags;
    new_pt0->rsw = main_pt0_0x80000->rsw;
    new_pt0->ppn = main_pt0_0x80000->ppn;
    new_pt0->reserved = main_pt0_0x80000->reserved;
    new_pt0->pbmt = main_pt0_0x80000->pbmt;
    new_pt0->n = main_pt0_0x80000->n;

    // set up the new mtag with the asid provided
    new_mtag = ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) | ((uint64_t)asid << ASID_SHIFT) | pageptr_to_pagenum(new_pt2);

    csrw_satp(new_mtag);
    sfence_vma();

    return new_mtag;
}

/*
 * Inputs:
 *  uint_fast16_t asid: the ASID for the satp register
 * Outputs: the new mtag
 * Description:
 *  1. Shallow copies global memory
 *  2. Sets up a new mtag
 *  3. Deep copies all mapped user pages
 * Effects: None
*/
uintptr_t memory_space_clone(uint_fast16_t asid){

    uintptr_t new_mtag;
    struct pte * new_pt2;
    struct pte * entry;
    struct pte * new_entry;
    void * pma;
    void * pp;
    uint64_t i;

    // shallow copy kernel memory
    // since this memory is global, we can use the main global variables
    // MMIO

    new_pt2 = (struct pte*) memory_alloc_page();
    for (uintptr_t pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        // new_pt2[VPN2(pma)] = leaf_pte((void*)pma, PTE_R | PTE_W | PTE_G);
        new_pt2[VPN2(pma)] = main_pt2[VPN2(pma)];
    
    // Third gigarange has a second-level page table
    new_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);
    
    new_pt2->flags = main_pt2->flags;
    new_pt2->rsw = main_pt2->rsw;
    new_pt2->ppn = main_pt2->ppn;
    new_pt2->reserved = main_pt2->reserved;
    new_pt2->pbmt = main_pt2->pbmt;
    new_pt2->n = main_pt2->n;

    // set up the new mtag with the asid provided
     new_mtag =  // Sv39
         ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
         pageptr_to_pagenum(new_pt2);

     debug("new_mtag: %lu", new_mtag);
     debug("main_mtag: %lu", main_mtag);

    // deep copy mapped user memory
    for(i = USER_START_VMA; i < USER_END_VMA; i+=PAGE_SIZE){
        // find the PTE for the mapped page
        entry = walk_pt(active_space_root(), i, 0);
        // if the page is not mapped, keep looking for mapped pages
        // if the page is not valid, keep looking for valid mapped pages
        if(entry == 0 || !(entry[VPN0(i)].flags & PTE_V))
            continue;
        if ((entry[VPN0(i)].flags & PTE_U)) {
             debug("user flag checked, creating page @ vma = %x, with flag=%p", i, entry[VPN0(i)].flags);
            // find the physical memory address of the mapped page
            pma = pagenum_to_pageptr(entry[VPN0(i)].ppn);
            // allocate a new page
            pp = memory_alloc_page();
            // debug("user allocd page @ %x", pp);
            // copy the memory from the original page to the new page
            memcpy(pp, pma, PAGE_SIZE);
            // map the new page with the same flags
            new_entry = walk_pt(mtag_to_root(new_mtag), i, 1);
            new_entry[VPN0(i)].flags = entry[VPN0(i)].flags;
            new_entry[VPN0(i)].ppn = pageptr_to_pagenum(pp);
        }
    }
    return new_mtag;
}


/*
 * Inputs: None
 * Outputs: None
 * Description:
 *  1. Waits for all preceeding updates to active page table to complete
 *  2. Frees all user memory from active user space
 *  3. Switches the memory space to the main memory space
 * Effects: Changes satp csr register
*/
void memory_space_reclaim(void){
    // sfence_vma ensures preceeding updates to the page table have completed
    sfence_vma();

    // reclaim memory from active space
    memory_unmap_and_free_user();

    // switch active memory space to main memory space
    memory_space_switch(main_mtag);


}

/*
 * Inputs: None
 * Outputs: a void pointer to the page
 * Description: Checks if there are any free pages and panics if there are none. Otherwise, it will return the
 *  first available free page and replaces the beginning of the list with the next free page.
 * Effects: 
 *  1. May cause a panic if the request is not satisfiable
 *  2. Changes the free_list
*/
void * memory_alloc_page(void){
    union linked_page * page;

    // panics if there are no free pages available
    if(free_list == NULL)
        panic("No free pages available");
    // replace head of the free_list
    page = free_list;
    free_list = free_list -> next;
    // return a pointer to the allocated page
    return (void *)page;    

}

/*
 * Inputs:
 *  void * pp: pointer to the physical page to be freed
 * Outputs: None
 * Description: Ensure that the page was previously allocated by memory_alloc_page. If it was,
 *  return the page to the free_list. This function requires that the page was previously allocated
 *  using memory_alloc_page.
 * Effects: Changes the head of the free_list
*/
void memory_free_page(void * pp){
    union linked_page * page;

    // cast pp as a linked_page
    page = (union linked_page *)pp;

    // add page back to the free_list
    page->next = free_list;
    free_list = page;
    
    
}

/*
 * Inputs:
 *  uintptr_t vma: the virtual address of the page to map
 *  uint_fast8_t rwxug_flags: an OR of the PTE flags
 * Outputs: void pointer to the virtual memory address
 * Description: 
 *  1. Allocates a page using memory_alloc page function.
 *  2. Walks through the page table using walk_pt helper function.
 *  3. Assigns a physical memory address to the page
 * Effects: May cause a panic if the request is not satisfiable.
*/
void * memory_alloc_and_map_page(uintptr_t vma, uint_fast8_t rwxug_flags){

    struct pte * pt0;
    uintptr_t ppn; 
    uintptr_t pma;

    // walks through the page table with create set to 1
    pt0 = walk_pt(active_space_root(), vma, 1);
    // check if walk was successful
    if(pt0 == 0)
        panic("Walk unable to create valid page");

    // allocate the page
    void *page = memory_alloc_page();
    ppn = pageptr_to_pagenum(page);

    // set flags
    pt0[VPN0(vma)].ppn = ppn;
    pt0[VPN0(vma)].flags |= PTE_V | rwxug_flags | PTE_A | PTE_D;

    // map physical page number to physical memory address
    pma = (uintptr_t)page | (vma & 0xFFF);
    debug("Virtual memory address %x mapped to physical memory address %x", vma, pma);

    sfence_vma();
    return (void *)pma;
}

/*
 * Inputs:
 *  uintptr_t vma: the virtual address of the first page to map
 *  size_t size: size of the page
 *  uint_fast8_t rwxug_flags: an OR of the PTE flags
 * Outputs: void pointer to the first virtual address
 * Description:
 *  1. Checks if the size is page aligned
 *  2. Allocs and maps a page for each page in the range
 * Effects: May panic if size is not aligned
*/
void * memory_alloc_and_map_range (uintptr_t vma, size_t size, uint_fast8_t rwxug_flags){
    int i;
    // uintptr_t cur_vma;

    if((size % PAGE_SIZE) != 0)
        panic("Cannot map range of unaligned size");

    // call memory_alloc_and_map_page for all in the range
    for(i = 0; i < size; i += PAGE_SIZE){
        memory_alloc_and_map_page(vma + i, rwxug_flags);
    }

    return (void *)vma;
}

/*
 * Inputs:
 *  const void * vp: pointer to the virtual page
 *  uint_fast8_t rwxug_flags: flags to set for the virtual page
 * Outputs: None
 * Description:
 *  1. Converts the page number to a page address
 *  2. Walks through the page table to find the entry associated with that address
 *  3. Changes the flags of the associated page table entry
 * Effects: Changes flags for a page table entry
*/
void memory_set_page_flags (const void * vp, uint_fast8_t rwxug_flags){
    uintptr_t vma;
    struct pte * pt0;

    // convert the page number to a page address
    vma = (uintptr_t)vp;

    // find the page table entry
    pt0 = walk_pt(active_space_root(), vma, 0);

    // set the flags for the page table entry and keep V, A, D flags in tact
    pt0[VPN0(vma)].flags = rwxug_flags | PTE_V | PTE_A | PTE_D;
    sfence_vma();
}

/*
 * Inputs:
 *  const void * vp: pointer to the first virtual page
 *  size_t size: size of the page
 *  uint_fast8_t rwxug_flags: flags to set for the virtual pages in range
 * Outputs: None
 * Description:
 *  1. Checks if size is page aligned
 *  2. Sets the flags for each page in the range
 * Effects: May panic if size is unaligned
*/
void memory_set_range_flags (const void * vp, size_t size, uint_fast8_t rwxug_flags){
    int i;

    // check if size is aligned
    if((size % PAGE_SIZE) != 0)
        panic("Cannot set flags on range of unaligned size");
    // call memory_set_page_flags for all in the range
    for(i = 0; i < size; i += PAGE_SIZE){
        memory_set_page_flags(vp + i, rwxug_flags);
    }
}

/*
 * Inputs: None
 * Outputs: None
 * Description: Starting at the active root, walk through the page table and unmap and free
 *  pages with the U flag marked.
 * Effects: Frees all user pages
*/
void memory_unmap_and_free_user(void){
    struct pte * root;

    // start the process at the active root
    root = active_space_root();
    // free all user pages
    walk_and_free_user(root);
}

/*
 * Inputs:
 *  const void * vp: virtual page address
 *  size_t len: Length of the range
 *  uint_fast8_t rwxug_flags: flags that should be set
 * Outputs: 
 *  0 if every virtual page containing the specified virtual address range is mapped with the at least the specified flags
 * Description: Finds the matching page according to the vp. For the length of the range, checks if the page is valid
 *  and if the specified flags are there. If not, returns an error.
 * Effect: None
*/
int memory_validate_vptr_len (const void * vp, size_t len, uint_fast8_t rwxug_flags){
    
    uintptr_t aligned_vp;
    struct pte * page;
    uintptr_t page_addr;
    int n;

    while(len > 0){
        aligned_vp = (uintptr_t)vp & ~(PAGE_SIZE - 1);
        page = walk_pt(active_space_root(), aligned_vp, 0);
        page_addr = (uintptr_t)pagenum_to_pageptr((uintptr_t)page);

        // check if page address is valid
        if(page_addr == 0){
            debug("Validate Length: page address not found");
            return -EBADFMT;
        }
        // check if the pte for the vp+len is valid
        if(!(page->flags && PTE_V)){
            debug("Validate Length: found invalid page");
            return -EBADFMT;
        }
        // check if the pte for the vp+len has the specified flags
        if((page->flags && rwxug_flags) != rwxug_flags){
            debug("Validate Length: flags don't match");
            return -EBADFMT;
        }

        // adjust vp and len to continue the validation check
        n = PAGE_SIZE - ((uintptr_t)vp - aligned_vp);
        len -= n;
        vp = (const void*)(aligned_vp + PAGE_SIZE);
    }
    return 0;
}

/*
 * Inputs:
 *  const char * vs: The address of the string
 *  uint_fast8_t ug_flags: the flags the bytes should have set
 * Outputs: 
 *  0 if address range starting at vs going up to and including a null character is readable with the specified flags
 *  A negative bad format error if otherwise
 * Description: Loops through the string until the end is found. For each address, checks if the flags of the matching page are correct 
 *  and if the page is valid. If not, return an error.
 * Effects: None
*/
int memory_validate_vstr (const char * vs, uint_fast8_t ug_flags){
    // TODO: complete function (last)
    char * p;
    uintptr_t aligned_vs;
    struct pte * page;
    uintptr_t page_addr;
    // should have ug flags and be readable
    //uint_fast8_t flags = ug_flags | PTE_R;
    int found_end = 0;
    int n;

    while(found_end == 0){
        aligned_vs = (uintptr_t)vs & ~(PAGE_SIZE - 1);
        page = walk_pt(active_space_root(), aligned_vs, 0);
        page_addr = (uintptr_t)pagenum_to_pageptr((uintptr_t)page); 

        // check if page address is valid
        if(page_addr == 0){
            debug("Validate String: page address not found");
            return -EBADFMT;
        }

        // find current character
        p = (char*)(page_addr + (vs - aligned_vs));
        // set n to keep track of bytes left in page
        n = PAGE_SIZE - ((uintptr_t)vs - aligned_vs);
        // search for NULL character
        while(n > 0){
            // if NULL character has been found, end search 
            if(*p == '\0'){
                found_end = 1;
                n = -1;
            }
            // check if flags match
            if((page->flags && ug_flags) != ug_flags){
                debug("Validate String: flags incorrect");
                return -EBADFMT;   
            }
            // check if page is valid
            if(!(page->flags && PTE_V)){
            debug("Validate String: found invalid page");
            return -EBADFMT;
            }

            --n;
            p++;
        }
        // continue to the next
        vs = (const char*)(aligned_vs + PAGE_SIZE);
    }

    return 0;
}


/*
 * Inputs:
 *  const void * vptr: pointer to the page that caused the fault
 * Outputs: None
 * Description: Allocates and maps a RW page at the faulting page number
 * Effects: May panic if the requested page is out of user bounds
*/
void memory_handle_page_fault(const void * vptr){
    
    // make sure the pointer is aligned
    uintptr_t aligned_vptr = (uintptr_t)vptr & ~(PAGE_SIZE - 1);

    // panic if the requested page is out of user bounds
    if ((uintptr_t)aligned_vptr < USER_START_VMA || (uintptr_t)aligned_vptr + PAGE_SIZE > USER_END_VMA) {
        panic("Out of USER bound");
    }

    // allocate and map RW page at the faulting page number
    debug("handle page fault @ %p, aligned vptr=%p", vptr, aligned_vptr);
    vptr = memory_alloc_and_map_page((uintptr_t)aligned_vptr, (PTE_R | PTE_W | PTE_U));
    sfence_vma();
}

// HELPER FUNCTIONS
//

/*
 * Inputs:
 *  struct pte * root: pointer to active root page table
 *  uintptr_t vma: virtual memory address
 *  int create: whether to create a page table or not
 * Outputs: pointer to the page table entry that represents the 4kB page containing vma
 * Description:
 *  1. Starts at the root (Level 2 of page table)
 *  2. Find level 1 of page table from the root. Check if it is valid, if not, allocs it depending on create.
 *  The alloced page will have V, A, and D flags set to 1.
 *  3. Find level 0 of page table from level 1. Check if it is valid, if not, allocs it depending on create.
 *  The alloced page will have V, A, and D flags set to 1.
 * Effects: May create new pages
*/
struct pte * walk_pt(struct pte * root, uintptr_t vma, int create) {
    struct pte *pte2 = &root[VPN2(vma)];
    struct pte *pt1, *pt0;

    // if the page is valid, change it to a page table pointer
    if(pte2->flags & PTE_V)
        pt1 = pagenum_to_pageptr(pte2->ppn);
    // if it is not valid, decide what to do based on create
    else{
        // if create is set to 0, it is not a valid page and we return
        if(!create){
            debug("Level 1 of Page Table was not valid and create was not 1.");
            return 0;
        }
        // if create is 1, allocate a new page table page and put its physical address in PTE
        // mark that it is a valid page
        pt1 = memory_alloc_page();
        // set the PTE in level 2 to point to the new level 1 page table
        pte2->ppn = pageptr_to_pagenum(pt1);
        pte2->flags = PTE_V;
    }

    // get the PTE for VPN[1]
    struct pte *pte1 = &pt1[VPN1(vma)];
    // if the page is valid, change it to a page table pointer
    if(pte1->flags & PTE_V) {
        pt0 = pagenum_to_pageptr(pte1->ppn);
    // if it is not valid, decide what to do based on create
    }else{
        // if create is set to 0, it is not a valid page and we return
        if(!create){
            // debug("Level 0 of Page Table was not valid and create was not 1. %x", vma);
            return 0;
        }
        // if create is 1, allocate a new page table page and put its physical address in PTE
        // mark that it is a valid page
        pt0 = memory_alloc_page();
        pte1->ppn = pageptr_to_pagenum(pt0);
        pte1->flags = PTE_V;
    }

    return pt0;
}

/*
 * Inputs:
 *  struct pte * root: pointer to active root page table
 * Outputs: None
 * Description:
 *  1. Starts at the root
 *  2. For each page in the root, check if it is a user page and a leaf page
 *  3. If it is a user page, free that page
 * Effects: Frees all user pages
*/
void walk_and_free_user(struct pte * root){
    int i;
    uint64_t page_num;
    uint64_t page_flags;
    //struct pte * page;
    uintptr_t lower_page;

    for(i = 0; i < PTE_PER_LEVEL; i++){
        //page = root[i];
        page_num = root[i].ppn;
        page_flags = root[i].flags;
        // check if page is for the user
        if(page_flags & PTE_U){
            // check if entry is not a leaf and recurse
            if((page_flags & PTE_V) && (page_flags & (PTE_R | PTE_W | PTE_X)) == 0){
                lower_page = (uintptr_t)pagenum_to_pageptr(page_num);
                walk_and_free_user((struct pte*)lower_page);
            }
            // free page by its page ptr
            memory_free_page((void*)pagenum_to_pageptr(page_num));
        }
    }
}
// INTERNAL FUNCTION DEFINITIONS
//

static inline int wellformed_vma(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits+1));
}

static inline int wellformed_vptr(const void * vp) {
    return wellformed_vma((uintptr_t)vp);
}

static inline int aligned_addr(uintptr_t vma, size_t blksz) {
    return ((vma % blksz) == 0);
}

static inline int aligned_ptr(const void * p, size_t blksz) {
    return (aligned_addr((uintptr_t)p, blksz));
}

static inline int aligned_size(size_t size, size_t blksz) {
    return ((size % blksz) == 0);
}

static inline uintptr_t active_space_mtag(void) {
    return csrr_satp();
}

static inline struct pte * mtag_to_root(uintptr_t mtag) {
    return (struct pte *)((mtag << 20) >> 8);
}


static inline struct pte * active_space_root(void) {
    return mtag_to_root(active_space_mtag());
}

static inline void * pagenum_to_pageptr(uintptr_t n) {
    return (void*)(n << PAGE_ORDER);
}

static inline uintptr_t pageptr_to_pagenum(const void * p) {
    return (uintptr_t)p >> PAGE_ORDER;
}

static inline void * round_up_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)(p + blksz-1) / blksz * blksz);
}

static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz) {
    return ((addr + blksz-1) / blksz * blksz);
}

static inline size_t round_up_size(size_t n, size_t blksz) {
    return (n + blksz-1) / blksz * blksz;
}

static inline void * round_down_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)p / blksz * blksz);
}

static inline size_t round_down_size(size_t n, size_t blksz) {
    return n / blksz * blksz;
}

static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz) {
    return (addr / blksz * blksz);
}

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags)
{
    return (struct pte) {
        .flags = rwxug_flags | PTE_A | PTE_D | PTE_V,
        .ppn = pageptr_to_pagenum(pptr)
    };
}

static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag)
{
    return (struct pte) {
        .flags = g_flag | PTE_V,
        .ppn = pageptr_to_pagenum(ptab)
    };
}

static inline struct pte null_pte(void) {
    return (struct pte) { };
}

static inline void sfence_vma(void) {
    asm inline ("sfence.vma" ::: "memory");
}

/* ***************************************************************************
 *  Group member's names : Anmol Arora (aa2640),
 *                         Raunak Negi (rn444)
 *  Username on iLab     : aa2640 | rn444
 *  iLab Server          : ilab.cs.rutgers.edu (ilab1)
 * ***************************************************************************
 */

#include "my_vm.h"

// Page Table Variables
pde_t page_dir;                     /* Page Directory Structure */
void* physical_memory;                     /* Placeholder for the physical space */
tlb tlb_list;

char* virtual_bitmap;               /* Virtual Bitmap */
char* physical_bitmap;              /* Physical Bitmap */
void **addr_space;                        /* A pointer for address space */

int off_bits;                         /* Value of the offset bits in VA/PA */
int tlb_index_bits;
int is_init = 0;                   /* Flag for physical memory init */
int bits_intern;                       /* Used to find bits required for Page Table */
int bits_extern;                       /* Used to find bits required for Page Directory */
int bits_count_pg_dir = 0;
int global = 0;
#define TOTAL_VIRTUAL_PAGES (MAX_MEMSIZE / PGSIZE)    /* Number of virtual pages */
#define TOTAL_PHYSICAL_PAGES (MEMSIZE / PGSIZE)       /* Number of physical pages */

// TLB Variables
int count_tlb = 0;                    /* Count of number of TLB entries */
int count_tlb_hit = 0;                    /* Hit count for TLB miss rate calc */
double count_miss_tlb = 0;                /* Miss count for TLB miss rate calc */
double count_trans_call = 0;               /* Count of translation function calls (either hit or miss) */

// PThread Mutex Variables (for thread-safe behavior)
pthread_mutex_t mem_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t physical_bitmap_lock;
pthread_mutex_t virtual_bitmap_lock;
pthread_mutex_t page_directory_lock;
pthread_mutex_t lock_tlb;

void print_binary(unsigned long num) {
    int i;
    // Determine the number of bits in an unsigned int
    int num_bits = sizeof(unsigned long) * 8;
    // Iterate through each bit, starting from the most significant bit
    for (i = num_bits - 1; i >= 0; i--) {
        // Use bitwise AND to check if the current bit is 1 or 0
        int bit = (num >> i) & 1;
        // (num >> i) & 1 extracts the least significant bit of (num >> i). The result is either 0 or 1, indicating the value of the bit at position i
        // DOUBT: why can't we use OR with 0 to check the bit ?
        // int bit = (num >> i) | 0;
        // Solution: (num >> i) | 0 performs a bitwise OR with 0, which does not change the value of the bits. It effectively returns the original value of (num >> i).
        // Print the bit
        printf("%d", bit);
        // Add a space for better readability, e.g., every 4 bits
        if (i % 4 == 0) {
            printf(" ");
        }
    }
    printf("\n");
}


void print_binary_char(char num) {
    int i;
    // Determine the number of bits in an char
    int num_bits = sizeof(char) * 8;

    // Iterate through each bit, starting from the most significant bit
    for (i = num_bits - 1; i >= 0; i--) {
        bits_count_pg_dir++;
        // Use bitwise AND to check if the current bit is 1 or 0
        int bit = (num >> i) & 1;
        // Print the bit
        // printf("%d", bit);
        // Add a space for better readability, e.g., every 4 bits
        // if (i % 4 == 0) {
        //     printf(" ");
        // }
    }
    // printf("\n");
}


void print_binary_char2(char num) {
    int i;
    // Determine the number of bits in an char
    int num_bits = sizeof(char) * 8;

    // Iterate through each bit, starting from the most significant bit
    for (i = num_bits - 1; i >= 0; i--) {
        // Use bitwise AND to check if the current bit is 1 or 0
        int bit = (num >> i) & 1;
        // Print the bit
        printf("%d", bit);
        // Add a space for better readability, e.g., every 4 bits
        if (i % 4 == 0) {
            printf(" ");
        }
    }
}


unsigned int createMask(unsigned start_index, unsigned end_index)
{
   unsigned int bitmask = 0;
   for (unsigned i=start_index; i<end_index; i++){
        bitmask |= 1 << i;
   }
   return bitmask;
}

/* 
 * Find the offset bits for some virtual address
 */
int get_offset_bits(void *virt_addr) {
    unsigned long offset_mask = createMask(0, off_bits);
    // print_binary(offset_mask);
    return (unsigned long)virt_addr & offset_mask;
}

/*
 * Returns internal bits for Page TABLE entries(i.e index inside the table)
 */
int get_pt_bits(void *virt_addr) {
    unsigned long pt_mask = createMask(off_bits, off_bits+bits_intern);
    // print_binary((unsigned long)virt_addr);
    // print_binary(pt_mask);
    // printf("Result pt_bits: ");
    // print_binary(((unsigned long)virt_addr & pt_mask)>> off_bits);
    return ((unsigned long)virt_addr & pt_mask)>> off_bits;
}

/*
 * Returns external bits for Page DIRECTORY entries(i.e index inside the directory)
 */
int get_pd_bits(void * virt_addr) {
    unsigned long pd_mask = createMask(off_bits+bits_intern, off_bits+bits_intern+bits_extern);
    // print_binary((unsigned long)virt_addr);
    // print_binary(pd_mask);
    // printf("Result pd_bits: ");
    // print_binary(((unsigned long)virt_addr & pd_mask) >> (32 - bits_extern));
    return ((unsigned long)virt_addr & pd_mask) >> (32 - bits_extern);
}

int get_tlb_index(void *virt_addr) {
    unsigned long tlb_mask = createMask(off_bits, off_bits + tlb_index_bits ); //12 , 21
    // print_binary((unsigned long)virt_addr);
    // print_binary(tlb_mask);
    // printf("Result tlb_bits: ");
    // print_binary(((unsigned long)virt_addr & tlb_mask)>> (32 - tlb_index_bits));
    return ((unsigned long)virt_addr & tlb_mask)>> off_bits;
}

unsigned long get_tlb_tag(void *virt_addr) {
    unsigned long tlb_tag_mask = createMask(off_bits + tlb_index_bits, 32);
    // print_binary((unsigned long)virt_addr);
    // print_binary(tlb_tag_mask);
    // printf("Result tlb_tag_bits: ");
    // print_binary(((unsigned long)virt_addr & tlb_tag_mask)>> (off_bits + tlb_index_bits));
    return ((unsigned long)virt_addr & tlb_tag_mask)>> (off_bits + tlb_index_bits);
}

// USED to calculate input parameters of page_map inside t_malloc
/* 
 * calculates the virtual address of a page given its index in the virtual space
 */
void* get_virtual_address(int virtual_idx) {
    void *start = (void*)PGSIZE;
    void *virt_addr = (PGSIZE * virtual_idx) + start;
    return virt_addr;
}

/*
 * calculates the physical address of a page given its index in the physical memory
 */
void* get_physical_address(int physical_index) {
    void* start = physical_memory;
    void* phys_addr = (PGSIZE * physical_index) + start;
    return phys_addr;
}

/*
 * Calculates the virtual index of a page given its virtual_address in the virtual space.
 */
int get_virtual_index(void *virt_addr) {
    void* start = (void*)PGSIZE;
    int idx = (virt_addr - start) / PGSIZE;
    return idx;
}

/*
 * Calculates the physical index of a page given its physical_address in the physical memory
 */
int get_physical_index(void *physical_address) {
    void* start = physical_memory;
    int idx =  (physical_address - start) / PGSIZE;
    return idx;
}

/*
 * Function responsible for allocating and setting your physical memory 
 */
void set_physical_mem() {
	/*
     * Allocate physical memory using mmap or malloc; this is the total size of
     * your memory you are simulating
     * 
     * Implementation: Using malloc
     */
    physical_memory = (void *)malloc(MEMSIZE * sizeof(void));

    /*
     * HINT: Also calculate the number of physical and virtual pages and allocate
     * virtual and physical bitmaps and initialize them
     * 
     * Implementation: 
     */    
    off_bits = log2(PGSIZE);   // Find the number offset bits
    tlb_index_bits = log2(TLB_ENTRIES);


    int vaddr_bits = 32 - off_bits;
    bits_intern = (int)(vaddr_bits / 2); // Page table bits
    bits_extern = vaddr_bits - bits_intern; // Page directory bits

    // printf("Bits: %d %d %d \n",bits_extern, bits_intern, off_bits);
    
    // Initialize virtual bitmap
    int physical_bitmap_size = TOTAL_PHYSICAL_PAGES / 8;
    // printf("%d",physical_bitmap_size );
    // printf("Physical Bitmap:%d ", (int)log2(TOTAL_PHYSICAL_PAGES / 8));
    physical_bitmap = (char*)malloc(physical_bitmap_size);
    
    // for (int i = 0; i < physical_bitmap_size; i++) {
    //     print_binary_char(physical_bitmap[i]);
    // } 
    // printf("%d :%d \n", bits_count_pg_dir, TOTAL_PHYSICAL_PAGES);

    pthread_mutex_init(&physical_bitmap_lock, NULL); // Initialize the physical bitmap mutex
    memset(physical_bitmap,0, physical_bitmap_size); //clear everything

    // Initialize physical bitmap
    int virtual_bitmap_size = TOTAL_VIRTUAL_PAGES / 8;
    virtual_bitmap = (char*)malloc(virtual_bitmap_size);
    pthread_mutex_init(&virtual_bitmap_lock, NULL); // Initialize the virtual bitmap mutex
    memset(virtual_bitmap,0, virtual_bitmap_size); //clear everything

    // If allocation fails (no continous memory), initialize a page directory

    int pde_size = 1 << bits_extern;
    int pte_size = 1 << bits_intern;
    int tlb_size = 1 << tlb_index_bits;

    page_dir = (page_table**)malloc(pde_size * sizeof(page_table**));

    pthread_mutex_init(&page_directory_lock, NULL); // Initialize the page directory mutex

    for (int i = 0; i < pde_size; i++) {
        page_dir[i] = NULL;
    }

    pthread_mutex_init(&lock_tlb, NULL); // Initialize the TLB mutex

    tlb_list = (tlb_node*)malloc(tlb_size * sizeof(tlb_node));
    memset(tlb_list,1, tlb_size); //clear everything
}


/*
 * Part 2: Add a virtual to physical page translation to the TLB.
 * Feel free to extend the function arguments or return type.
 */
int add_TLB(void *virtual_addr, void *physical_addr) {    
    pthread_mutex_lock(&lock_tlb); // Lock the TLB

    int tlb_index = get_tlb_index(virtual_addr);
    unsigned long tlb_tag = get_tlb_tag(virtual_addr);


    tlb_list[tlb_index].tag = tlb_tag;
    tlb_list[tlb_index].paddr = physical_addr;
    
    pthread_mutex_unlock(&lock_tlb); // Unlock the TLB

    return 0;
}

/*
 * Part 2: Check TLB for a valid translation.
 * Returns the physical page address.
 * Feel free to extend this function and change the return type.
 */
void* check_TLB(void *va) {

    /* Part 2: TLB lookup code here */
    pthread_mutex_lock(&lock_tlb); // Lock the TLB

    int tlb_index = get_tlb_index(va);
    unsigned long tlb_tag = get_tlb_tag(va);

    if( tlb_list[tlb_index].tag == tlb_tag ){
        // A TLB entry was found,
        void * pa = tlb_list[tlb_index].paddr;
        pthread_mutex_unlock(&lock_tlb); // Unlock the TLB
        return pa;
    }

    pthread_mutex_unlock(&lock_tlb); // Unlock the TLB

    return NULL; // No mapping was found
}


/*
 * Part 2: Print TLB miss rate.
 * Feel free to extend the function arguments or return type.
 */
void print_TLB_missrate()
{
    double miss_rate = 0;

    /* Part 2 Code here to calculate and print the TLB miss rate */
    printf("miss rate: %lf Count trans call: %lf \n ",count_miss_tlb,count_trans_call);
    miss_rate = count_miss_tlb / count_trans_call;

    fprintf(stderr, "TLB miss rate %lf \n", miss_rate * 100);

}


/*
 * The function takes a virtual address and page directories starting address and
 * performs translation to return the physical address
 */
void *translate(pde_t pgdir, void *va) {
    count_trans_call++;
    if (pgdir == NULL || va == NULL) {
        return NULL;
    }

    // TLB Check
    void *p = check_TLB(va);
    if (p != NULL) {
        count_tlb_hit++;
        return p;
    }
    count_miss_tlb++;

    // Virtual Address Index Calculation
    int virtual_index = get_virtual_index(va);
    pthread_mutex_lock(&virtual_bitmap_lock);
    int char_index = virtual_index / 8;
    int bit_index = virtual_index % 8;
    int is_page = (virtual_bitmap[char_index] >> (7 - bit_index)) & 1;
    pthread_mutex_unlock(&virtual_bitmap_lock);

    if (is_page == 0) {
        return NULL;
    }

    // Page Directory and Table Index Calculation
    int pdi = get_pd_bits(va);
    int pti = get_pt_bits(va);
    int offset = get_offset_bits(va);

    pthread_mutex_lock(&page_directory_lock);
    page_table *p_table = pgdir[pdi];
    if (p_table == NULL) {
        pthread_mutex_unlock(&page_directory_lock);
        return NULL;
    }

    void *pte = p_table[pti].physical_address;
    pthread_mutex_unlock(&page_directory_lock);

    if (pte == NULL) {
        return NULL;
    }

    // Physical Address Calculation
    void *physical_address = (void *)((char *)pte + offset);
    add_TLB(va, physical_address);

    return physical_address;
}

/* 
 * The function takes a page directory address, virtual address, physical address
 * as an argument, and sets a page table entry. This function will walk the page
 * directory to see if there is an existing mapping for a virtual address. If the
 * virtual address is not present, then a new entry will be added
 */
int page_map(pde_t pgdir, void *va, void *pa) {
    if (!pgdir || !va || !pa) {
        // Basic validation
        return -1;
    }

    int page_dir_entry_index = get_pd_bits(va);
    int pg_table_entry_index = get_pt_bits(va);
    int num_entries = 1 << (bits_intern - 1);
    
    pthread_mutex_lock(&page_directory_lock); // Lock the page directory

    if (pgdir[page_dir_entry_index] == NULL) {  
        // If the page directory entry is null, allocate a new page table
        page_table* p_table = (page_table*)malloc(num_entries * sizeof(page_table));
        if (!p_table) {
            // Handle malloc failure
            pthread_mutex_unlock(&page_directory_lock);
            return -1;
        }

        // Initialize the page table
        memset(p_table, 0, num_entries * sizeof(page_table));
        pgdir[page_dir_entry_index] = p_table;
    }

    // Create or update the page table entry
    pgdir[page_dir_entry_index][pg_table_entry_index].physical_address = pa;
    
    pthread_mutex_unlock(&page_directory_lock); // Unlock the page directory

    return 0; // Success
}


/*
 * Function that gets the next available page in VIRTUAL space
 * Function that returns a pointer variable which stores the VIRT_INDEX of the next available virtual page.
 * if we want multiple pages the function returns a pointer that points to an array of integers where each element stores a virtual index.
 */
int *get_next_avail(int num_pages) {
    int *new_arr = (int*)malloc(num_pages * sizeof(int));
    int index = 0;

    pthread_mutex_lock(&virtual_bitmap_lock); // Lock the virtual bitmap so that no changes are done while searching for the available pages

    for (int i = 0; i < TOTAL_VIRTUAL_PAGES; i++) {
        int char_index = i/8;
        int bit_index = i%8;
        
        int is_page = (virtual_bitmap[char_index] >> (7 - bit_index)) & 1 ; //Check bit corresponding to that page

        if (is_page == 0) {
            // printf("Char Index: %d , Bit_index: %d ", char_index, bit_index);
            if (num_pages == 1) {
                virtual_bitmap[char_index] |= 1<<(7 - bit_index); // Set bit corresponding to that page
                new_arr[index] = i;
                pthread_mutex_unlock(&virtual_bitmap_lock); // Unlock the virtual bitmap
                return new_arr;
            }

            int j = i + 1;
            int continuus_pages_count = 0;

            while (j < TOTAL_VIRTUAL_PAGES) {
                char_index = j/8;
                bit_index = j%8;
                is_page = (virtual_bitmap[char_index] >> (7 - bit_index)) & 1 ; //Check bit corresponding to that page in the virtual bitmap

                // Check for contiguous pages
                if (is_page == 0) {
                    continuus_pages_count++;
                }

                if (is_page == 1) {
                    i = j;
                    break;
                }

                if (continuus_pages_count == num_pages - 1) {
                    continuus_pages_count = 0;
                    j = i;

                    while (continuus_pages_count < num_pages) {
                        char_index = j/8;
                        bit_index = j%8;
                        // printf("Char Index: %d , Bit_index: %d \n", char_index, bit_index);
                        // printf("Before: ");
                        // print_binary_char2(virtual_bitmap[char_index]);
                        virtual_bitmap[char_index] |= 1<<(7 - bit_index); // Set bit corresponding to that page
                        // printf("After: ");
                        // print_binary_char2(virtual_bitmap[char_index]);
                        // printf("\n");
                        new_arr[index] = j;
                        j++; continuus_pages_count++; index++;
                    }

                    pthread_mutex_unlock(&virtual_bitmap_lock); // Unlock the virtual bitmap
                    return new_arr;
                }
                j++;
            }
        }
    }
    pthread_mutex_unlock(&virtual_bitmap_lock); // Unlock the virual bitmap
    // No contiguous block of free pages is found
    return NULL;
}

/*
 * Function that gets the next available page in physical memory
 * Function that returns a pointer variable which stores the PHY_INDEX of the next available physical page
 * if we want multiple pages returns a pointer that points to an array of integers where each element stores a physical index.
 */
int *get_next_available_physical(int num_pages) {

    //Use physical address bitmap to find the next free page 
    // The available pages/free pages need NOT be continuous in the physical memory
    int j = 0, count_of_pages = 0;
    
    int* new_arr = (int*)malloc(num_pages*sizeof(int));
    
    pthread_mutex_lock(&physical_bitmap_lock); // Lock the physical bitmap
    
    for (int i = 0; i < TOTAL_PHYSICAL_PAGES; i++) {
        int char_index = i/8;
        int bit_index = i%8;

        int is_page = (physical_bitmap[char_index] >> (7 - bit_index)) & 1 ; //Check bit corresponding to that page in the physical bitmap
        
        // check if page is occupied or not
        if (is_page == 0) { 
            new_arr[j] = i;
            j++; count_of_pages++;
        }

        if (count_of_pages == num_pages) {
            j=0;
            while (j < num_pages) {
                char_index = new_arr[j]/8;
                bit_index = new_arr[j]%8;
                // printf("Char Index: %d , Bit_index: %d \n", char_index, bit_index);
                // printf("Before: ");
                // print_binary_char2(virtual_bitmap[char_index]);
                physical_bitmap[char_index] |= 1<<(7 - bit_index);
                // printf("After: ");
                // print_binary_char2(virtual_bitmap[char_index]);
                // printf("\n");
                j++;
            }
            pthread_mutex_unlock(&physical_bitmap_lock); // Unlock the physical bitmap
            return new_arr;
        }
    }

    pthread_mutex_unlock(&physical_bitmap_lock); // Unlock the physical bitmap
    // No contiguous block of free pages was found
    return NULL;
}


/* 
 * Function responsible for allocating pages and used by the benchmark
 */
void *t_malloc(unsigned int num_bytes) {
    /* 
     * HINT: If the physical memory is not yet initialized, then allocate and initialize.
     */

    /* 
     * HINT: If the page directory is not initialized, then initialize the
     * page directory. Next, using get_next_avail(), check if there are free pages. If
     * free pages are available, set the bitmaps and map a new page. Note, you will 
     * have to mark which physical pages are used. 
     */
    pthread_mutex_lock(&mem_lock); // Lock physical memory before allocating

    if (!is_init) {
        // Allocating and initializing physical memory
        set_physical_mem();
        is_init = 1;
    }

    pthread_mutex_unlock(&mem_lock); // Unlock physical memory after allocating
    
    // Check for the number of pages to be allocated but always
    // allocate a minimum of 1 page if physical memory is allocated
    int num_pages = ceil((double)num_bytes / PGSIZE);
    
    if (num_pages == 0) {
        num_pages = 1;
    }
    // printf("Numpages: %d \n",num_pages);
    
    // Check for the available pages in virtual memory
    int *virtual_index = get_next_avail(num_pages);
    
    if (virtual_index == NULL) {
        return NULL;
    }
    
    // Check for the available pages in physical memory
    int *physical_index = get_next_available_physical(num_pages);
    
    if (physical_index == NULL) {
        return NULL;
    }
    
    for (int i = 0; i < num_pages; i++) {
        // printf("Allocating virtual index: %d, physical index: %d \n",virtual_index[i],physical_index[i]);
        int x =  page_map(page_dir, get_virtual_address(virtual_index[i]), get_physical_address(physical_index[i]));
        if (x == -1) return NULL; // If PTE already exists
    }

    return (void*)get_virtual_address(virtual_index[0]);
}

// free the page table entry and the physical bitmap corresponding to the virtual_address

void free_page(void *va) {

        // 1. Get the physcial address corresponding to this virtual page index from the page table 
        // 2. Get the physical index (i.e page index for this physical address)
        // 3. Unset the bit of the corresponding page in the physical bitmap
        // 4. Clear the page table entry

        // mainly frees the 
        int dir_entry_idx = get_pd_bits(va);
        int tb_entry_idx = get_pt_bits(va);

        pthread_mutex_lock(&page_directory_lock); // Lock the page directory
        
        // Mapping Exist => clean the bitmap (1. and 2.)
        int physcial_idx = get_physical_index(page_dir[dir_entry_idx][tb_entry_idx].physical_address);
        
        pthread_mutex_lock(&physical_bitmap_lock); // Lock the physical bitmap
        
        int char_index = physcial_idx/8;
        int bit_index = physcial_idx%8;

        // 3. Unset the bit of the corresponding page in the physical bitmap

        physical_bitmap[char_index] &= ~(1<<(7 - bit_index)); // Unset bit corresponding to that page
        
        // printf("After: ");
        // print_binary_char2(physical_bitmap[char_index]);
        // printf("\n");
        
        pthread_mutex_unlock(&physical_bitmap_lock); // Unlock the physical bitmap
        // 4. clear the page table entry 
        page_dir[dir_entry_idx][tb_entry_idx].physical_address = NULL;

        pthread_mutex_unlock(&page_directory_lock); // Unlock the page directory

}

// Checks if the va has a mapping to a pa or not
 
int is_valid(void *va){
        // check if the va is valid or not
        int dir_entry_idx = get_pd_bits(va);
        int tb_entry_idx = get_pt_bits(va);

        pthread_mutex_lock(&page_directory_lock); // Lock the page directory
        
        if (page_dir[dir_entry_idx] == NULL) {
            // Virtual page is occupied but its not mapped => invalid
            // PDE-PTE mapping does not exist in page directory
            pthread_mutex_unlock(&page_directory_lock); // Unlock the page directory
            return 0;
        }else{
            // PDE-PTE mapping is there bit PTE-PA mapping not there still Invalid
            if (page_dir[dir_entry_idx][tb_entry_idx].physical_address == NULL) {
                pthread_mutex_unlock(&page_directory_lock); // Unlock the page directory
                return 0;
            }
        }
        pthread_mutex_unlock(&page_directory_lock); // Unlock the page directory

        return 1;

}

// Check if all the virtual pages are already occupied or not
 
int all_pages_valid(int start_index, int end_index ){
    pthread_mutex_lock(&virtual_bitmap_lock); // Lock the virtual bitmap
    for (int i = start_index; i < end_index; i++) {
        int char_index = i/8;
        int bit_index = i%8;
        int is_page = (virtual_bitmap[char_index] >> (7 - bit_index)) & 1 ; //Check bit corresponding to that page
        if (is_page == 0) {
            // virtual page is already free
            pthread_mutex_unlock(&virtual_bitmap_lock); // Immediately unlock virtual bitmap
            return 0;
        }
        // printf("Virtual index: %d,",i);
        int valid = is_valid(get_virtual_address(i));

        if (valid == 0){

            pthread_mutex_unlock(&virtual_bitmap_lock); // Immediately unlock virtual bitmap
            return 0;
        }
    }

    pthread_mutex_unlock(&virtual_bitmap_lock); // Immediately unlock virtual bitmap
    return 1;
}

/* 
 * Responsible for releasing one or more memory pages using virtual address (va)
 */
int t_free(void *va, int size) {
    // printf("----NEW CALL---- \n");

    /* Part 1: Free the page table entries starting from this virtual address
     * (va). Also mark the pages free in the bitmap. Perform free only if the 
     * memory from "va" to va+size is valid.
     *
     * Part 2: Also, remove the translation from the TLB
     */
    if (va == NULL) {
        return 0;
    }

    int virtual_idx = get_virtual_index(va); //equivalent to virtual page number
    int num_pages = ceil((double)size / PGSIZE); // Number of pages to free
    int virtual_end_idx = virtual_idx + num_pages; // equivalent to the last virual page number
    
    // printf("----Checking---- \n");
    int validity = all_pages_valid(virtual_idx, virtual_end_idx);
    
    if(validity == 0){
        // not all pages are valid 
        return 0;
    }

    // printf("----CLEAR------- \n");
    for (int i = virtual_idx; i < virtual_end_idx; i++) {
        int char_index = i/8;
        int bit_index = i%8;

        // clear physical bitmap and page table entry
        free_page(get_virtual_address(i));


        // clear virtual bitmap
        pthread_mutex_lock(&virtual_bitmap_lock); // Lock the virtual bitmap

        // printf("Virtual index: %d, Before: ",i);
        // print_binary_char2(virtual_bitmap[char_index]);
        // 3. Unset the bit of the corresponding page in the physical bitmap
        virtual_bitmap[char_index] &= ~(1<<(7 - bit_index)); // Unset bit corresponding to that page
        // printf("After: ");
        // print_binary_char2(virtual_bitmap[char_index]);
        // printf("\n");

        pthread_mutex_unlock(&virtual_bitmap_lock); // Unlock virtual bitmap
    }
    return 1; 
}

/* 
 * The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
 * The function returns 0 if the put is successfull and -1 otherwise.
 */
int put_value(void *va, void *val, int size) {
    if (!va || !val || size <= 0) {
        return -1; // Basic validation
    }

    int remaining_size = size;
    char *current_val = (char *)val;
    char *current_va = (char *)va;

    while (remaining_size > 0) {
        void *paddr = translate(page_dir, current_va);
        if (!paddr) {
            fprintf(stderr, "Translation failure at virtual address %p\n", current_va);
            return -1;
        }

        int offset = (unsigned long)current_va % PGSIZE;
        int bytes_to_copy = PGSIZE - offset;
        if (bytes_to_copy > remaining_size) {
            bytes_to_copy = remaining_size;
        }

        memcpy(paddr + offset, current_val, bytes_to_copy);

        remaining_size -= bytes_to_copy;
        current_val += bytes_to_copy;
        current_va += bytes_to_copy;
    }
    return 0;//Success
}


/*
 * Given a virtual address copy the contents of the page to val
 */

void get_value(void *va, void *val, int size) {
    if (!va || !val || size <= 0) {
        return; // Basic validation
    }

    char *current_va = (char *)va;
    char *current_val = (char *)val;

    for (int remaining_size = size; remaining_size > 0;) {
        void *paddr = translate(page_dir, current_va);
        if (!paddr) {
            // Handle translation failure
            fprintf(stderr, "Translation failure at virtual address %p\n", current_va);
            break;
        }

        int offset = (unsigned long)current_va % PGSIZE;
        int bytes_to_copy = PGSIZE - offset;
        if (bytes_to_copy > remaining_size) {
            bytes_to_copy = remaining_size;
        }

        memcpy(current_val, paddr + offset, bytes_to_copy);

        remaining_size -= bytes_to_copy;
        current_val += bytes_to_copy;
        current_va += bytes_to_copy;
    }
}


/*
 * This function receives two matrices mat1 and mat2 as an argument with size
 * argument representing the number of rows and columns. After performing matrix
 * multiplication, copy the result to answer.
 */
void mat_mult(void *mat1, void *mat2, int size, void *answer) {
    if (!mat1 || !mat2 || !answer || size <= 0) {
        return; // Basic validation
    }

    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            int sum = 0;
            for (int k = 0; k < size; k++) {
                int a, b;
                int *addr_a = (int *)mat1 + i * size + k;
                int *addr_b = (int *)mat2 + k * size + j;
                
                get_value(addr_a, &a, sizeof(int));
                get_value(addr_b, &b, sizeof(int));

                sum += a * b;
            }

            int *addr_c = (int *)answer + i * size + j;
            if (put_value(addr_c, &sum, sizeof(int)) < 0) { //Either basic validation failed or physical translation failed
                return;
            }
        }
    }
}


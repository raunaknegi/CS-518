/* ***************************************************************************
 *  Group member's names : Anmol Arora (aa2640),
 *                         Raunak Negi (rn444)
 *  Username on iLab     : aa2640 | rn444
 *  iLab Server          : ilab.cs.rutgers.edu (ilab3)
 * ***************************************************************************
 */

#ifndef MY_VM_H_INCLUDED
#define MY_VM_H_INCLUDED
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

//Assume the address space is 32 bits, so the max memory size is 4GB
//Page size is 4KB

//Add any important includes here which you may need

#include <time.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define PGSIZE 4096

// Maximum size of virtual memory
#define MAX_MEMSIZE 4ULL*1024*1024*1024

// Size of "physcial memory"
#define MEMSIZE 1024*1024*1024

// Represents a page table entry
typedef unsigned long pte_t;

// Represents a page directory entry
// typedef unsigned long pde_t;

#define TLB_ENTRIES 512

// New structure for each entry in the page table
typedef struct page_table {
	void *physical_address;
	int index; 
} page_table;

// Page table directory entry
typedef page_table **pde_t; 

// TLB Size
#define TLB_SIZE 512

// An individual TLB Node
typedef struct tlb_node {
	unsigned long tag;
	void *paddr;
}tlb_node;

typedef tlb_node* tlb;


// Structure that represents the TLB
struct tlb {
    /* 
     * Assume your TLB is a direct mapped TLB with number of entries as TLB_ENTRIES
     * Think about the size of each TLB entry that performs virtual to physical
     * address translation.
     */
};

// struct tlb *tlb_store;

void set_physical_mem();
void *translate(pde_t pgdir, void *va);
int page_map(pde_t pgdir, void *va, void* pa);
bool check_in_tlb(void *va);
void put_in_tlb(void *va, void *pa);
void *t_malloc(unsigned int num_bytes);
int t_free(void *va, int size);
int put_value(void *va, void *val, int size);
void get_value(void *va, void *val, int size);
void mat_mult(void *mat1, void *mat2, int size, void *answer);
void print_TLB_missrate();

// New functions that show up
int get_offset(void *virt_addr);
void insert_tlb_new(struct tlb_node *new_tlb);
void replace_tlb_entry(void *virtual_addr, void *physical_addr, unsigned long int vpn);

#endif
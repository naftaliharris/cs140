#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "threads/thread.h"


/* The different types of pages */
typedef enum {
    STACK_PAGE;
    FILE_PAGE;
    MMAPED_PAGE;
} page_type;


/* 
 --------------------------------------------------------------------
 DESCRIPTION: tracks the additional info we need to keep tabs on 
    for a page.
 NOTE: type indicates the type of page
 NOTE: page_id is how we identify pages. It is the equivalent of 
    calling round down on a virtual address.
 NOTE: the pinned filed is used for page eviction. If a page
    is pinned, it cannot be evicted. 
 --------------------------------------------------------------------
 */
struct spte
{
    struct hash_elem elem; /* For the per-process list                   */
    page_type type;
    void* page_id;
    
    bool is_writeable;   /*true if the page can be written to */
    bool is_loaded;      /*true if the page is currently loaded in memory */
    bool pinned;         /*pinning for page eviction */
           
    struct file* file_ptr;
    off_t offset_in_file;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    
    uint32_t swap_index; /* index into the swap sector */
    
    
};

//--------------------LP ADDED FUNCTIONS------------------------//
/*
 --------------------------------------------------------------------
 DESCRIPTION: This function creates a new spte struct, populates
    the fields with the supplied data, and then adds the created
    spte to the supplemental page table.
 --------------------------------------------------------------------
 */
void create_spte_and_add_to_table(page_type type, void* page_id, bool is_writeable, bool is_loaded, bool pinned, struct file* file_ptr, off_t offset, uint32_t read_bytes, uint32_t zero_bytes);





/*
 --------------------------------------------------------------------
 DESCRIPTION: this function frees the resources that an spte uses
    to track a page. 
 NOTE: Here we release all resources attained in the process of 
    managing a page with an spte. This involces the following:
    1. Remove the spte from the data structure it is a part of
    2. Freeing memory
    3. Releasing locks
 --------------------------------------------------------------------
 */
void free_spte(struct spte* spte);

/*
 --------------------------------------------------------------------
 DESCRIPTION: Loads a page into a physical memory frame.
 NOTE: This function gets called AFTER!! the create_spte has been 
    called. 
 NOTE: A page is loaded into memory when the user calls palloc
    or from the page fault handler. The process of loading a page
    of memory is different for the different types of pages.
    Thus, we use a switch statement based on page type to 
    load the page
 NOTE: Need to finish implimentation of the specific load page
    functions in page.c
 --------------------------------------------------------------------
 */
void load_page_into_physical_memory(struct spte* spte);

/*
 --------------------------------------------------------------------
 DESCRIPTION: Removes a page that is currently residing in a physcal
    frame, and moves it to another location. 
 NOTE: this function can only be called if the given page is currently
    in physical memory!
 NOTE: The process of moving a page is dependent on the page type.
    Thus, we use a switch statement, just as in load page. 
 --------------------------------------------------------------------
 */
void evict_page_from_physical_memory(struct spte* spte);

/*
 --------------------------------------------------------------------
 DESCRIPTION: looks up an spte entry for a given virtual address.
 NOTE: Our method of identifiying pages to spte's is by rounded 
    down virtual address. Thus, to get the spte entry for a given 
    virtual address, we pass the address to this function, we
    round the address down, and then we look up the spte entry in the
    per thread data structure based on the rounded down virtual 
    address.
 NOTE: The rounded down vitual address is the page number, given
    the settup of the virtual address. 
 --------------------------------------------------------------------
 */
struct spte* find_spte(void* virtual_address);

/*
 --------------------------------------------------------------------
 DESCRIPTION: initializes the given hash table.
 --------------------------------------------------------------------
 */
void init_spte_table(struct hash* thread_hash_table);

/*
 --------------------------------------------------------------------
 DESCRIPTION: frees the spe_table hash table, removing all
    malloc'd spte structs that reside in the hash table in 
    the process
 --------------------------------------------------------------------
 */
void free_spte_table(struct hash* thread_hash_table);














//------------------PREVIOUS DECLERATIONS--------------------//

bool map_page (struct thread*, void *, void *, bool);
void evict_page (struct thread*, void *);
struct spte *find_spte(struct thread*, void *);
void free_page (struct thread*, struct spte *);
void free_spt (struct list *);

#endif /* vm/page.h */

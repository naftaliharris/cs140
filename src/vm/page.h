#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "threads/thread.h"
#include <hash.h>
#include "filesys/off_t.h"
#include "frame.h"



//TO DO:
//1. free frame resources in free_hash_entry


/* The different types of pages */
typedef enum {
    SWAP_PAGE,
    FILE_PAGE,
    MMAPED_PAGE,
} page_location;


/* 
 --------------------------------------------------------------------
 DESCRIPTION: tracks the additional info we need to keep tabs on 
    for a page.
 NOTE: location indicates where to access page if not in physical
    memory
 NOTE: page_id is how we identify pages. It is the equivalent of 
    calling round down on a virtual address.
 NOTE: the pinned filed is used for page eviction. If a page
    is pinned, it cannot be evicted. 
 --------------------------------------------------------------------
 */
struct spte
{
    struct hash_elem elem; /* For the per-process list                   */
    page_location location;
    struct thread* owner_thread; /* needed to access pd */
    void* page_id;
    struct frame* frame; /* the physical frame of the page if it is memory */
    
    bool is_writeable;   /*true if the page can be written to */
    //bool is_loaded;      /*true if the page is currently loaded in memory */
           
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
struct spte* create_spte_and_add_to_table(page_location location, void* page_id, bool is_writeable, bool is_loaded, struct file* file_ptr, off_t offset, uint32_t read_bytes, uint32_t zero_bytes);

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
bool load_page_into_physical_memory(struct spte* spte, bool is_fresh_stack_page);

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
bool evict_page_from_physical_memory(struct spte* spte);

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

/*
 --------------------------------------------------------------------
 DESCRIPTION: Taken out of process.c, and moved here. This function
    installs the address translation from upage virtual address to
    kpage which is a physical_memory_address of a frame.
 NOTE: Adds a mapping from user virtual address UPAGE to kernel
    virtual address KPAGE to the page table.
    If WRITABLE is true, the user process may modify the page;
    otherwise, it is read-only.
    UPAGE must not already be mapped.
    KPAGE should probably be a page obtained from the user pool
    with palloc_get_page().
    Returns true on success, false if UPAGE is already mapped or
    if memory allocation fails.
 --------------------------------------------------------------------
 */
bool install_page (void *upage, void *kpage, bool writable);

/*
 --------------------------------------------------------------------
 DESCRIPTION: The opposite of install_page. Wrapper around the call
    to pagedir_clear_page to remove the address translation from
    upage to kpage.
 --------------------------------------------------------------------
 */
void clear_page(void* upage, struct thread* t);

/*
 --------------------------------------------------------------------
 DESCRIPTION: checks to ensure that a stack access is valid.
 --------------------------------------------------------------------
 */
bool is_valid_stack_access(void* esp, void* user_virtual_address);

/*
 --------------------------------------------------------------------
 DESCRIPTION: handles the last case of a stack access in which the 
    faulting address is above the stack pointer.
 --------------------------------------------------------------------
 */

/*
 --------------------------------------------------------------------
 DESCRIPTION: grows the current processes stack by creating and adding
    an spte for the given page_id, and then calling frame_handler_palloc
    to load the page into physical memory.
 NOTE: this function assumes that check_stack_growth has been called 
    beforehand ensuring that the stack access is valid. The only time
    we do not call is_valid_stack_access is when creating the initial
    stack page of a process.
 --------------------------------------------------------------------
 */
bool grow_stack(void* page_id);

/*
 --------------------------------------------------------------------
 DESCRIPTION: takes in a virtual address, finds the spte for the address
    ensures that the page the address resides on is in memory, and then
    aquires the lock on the frame the page resides in. 
 --------------------------------------------------------------------
 */
void pin_page(void* virtual_address);

/*
 --------------------------------------------------------------------
 DESCRIPTION: unpins a page by releasing the frame lock
 --------------------------------------------------------------------
 */
void un_pin_page(void* virtual_address);


#endif /* vm/page.h */

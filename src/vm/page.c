#include <stdbool.h>
#include "threads/thread.h"
#include "threads/malloc.h"  
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <stdio.h>
#include <stddef.h>
#include "filesys/file.h"
#include <string.h>


static void load_swap_page(struct spte* spte);
static void load_file_page(struct spte* spte);
static void load_mmaped_page(struct spte* spte);
static void evict_swap_page(struct spte* spte);
static void evict_file_page(struct spte* spte);
static void evict_mmaped_page(struct spte* spte);



/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
struct spte* create_spte_and_add_to_table(page_location location, void* page_id, bool is_writeable, bool is_loaded, struct file* file_ptr, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {
    struct spte* spte = malloc(sizeof(struct spte));
    if (spte == NULL) {
        PANIC("Could not allocate spte");
    }
    spte->location = location;
    spte->owner_thread = thread_current();
    spte->page_id = page_id;
    spte->is_writeable = is_writeable;
    spte->is_loaded = is_loaded;
    spte->frame = NULL;
    spte->file_ptr = file_ptr;
    spte->offset_in_file = offset;
    spte->read_bytes = read_bytes;
    spte->zero_bytes = zero_bytes;
    spte->swap_index = 0; //IS THIS CORRECT???
    struct hash* target_table = &thread_current()->spte_table;
    struct spte* outcome = hash_insert(target_table, &spte->elem);
    if (outcome != NULL) {
        PANIC("Trying to add two spte's for the same page");
    }
    return spte;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
void free_spte(struct spte* spte) {
    //HAVE TO REMOVE FROM DATA STRUCTURE
    free(spte);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: loads a page from swap to physical memory
 --------------------------------------------------------------------
 */
static void load_swap_page(struct spte* spte) {
    read_from_swap(spte->frame->physical_mem_frame_base, spte->swap_index);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: loads a page from swap to physical memory
 NOTE: this function is called by frame_handler_palloc, which
    locks the frame, so we do not need to pin here.
 --------------------------------------------------------------------
 */
static void load_file_page(struct spte* spte) {
    if (spte->zero_bytes == PGSIZE) {
        memset(spte->frame->physical_mem_frame_base, 0, PGSIZE);
        return;
    }
    uint32_t bytes_read = file_read (spte->file_ptr, spte->frame->physical_mem_frame_base, spte->read_bytes);
    if (bytes_read != spte->read_bytes) {
        //HERE WE NEED TO HANDLE THIS ERROR CONDITION!!
    }
    if (spte->read_bytes != PGSIZE) {
        memset (spte->frame->physical_mem_frame_base + spte->read_bytes, 0, spte->zero_bytes);
    }
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: loads a memory mapped page into memory
 --------------------------------------------------------------------
 */
static void load_mmaped_page(struct spte* spte) {
    return;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: Need to implement these functions. 
 NOTE: Need to add the mapping by calling page_dir_set_page
 --------------------------------------------------------------------
 */
bool load_page_into_physical_memory(struct spte* spte) {
    ASSERT(spte != NULL);
    switch (spte->location) {
        case SWAP_PAGE:
            load_swap_page(spte);
            break;
        case FILE_PAGE:
            load_file_page(spte);
            break;
        case MMAPED_PAGE:
            load_mmaped_page(spte);
            break;
        default:
            break;
    }
    return install_page(spte->page_id, spte->frame->physical_mem_frame_base, spte->is_writeable);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION:In this function, we copy a page from a physcial
    frame to a swap slot
 --------------------------------------------------------------------
 */
static void evict_swap_page(struct spte* spte) {
    uint32_t swap_index = write_to_swap(spte->frame->physical_mem_frame_base);
    spte->swap_index = swap_index;
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: moves a page containing file data to a swap slot
    if the page is dirty. Else, we do nothing.
 --------------------------------------------------------------------
 */
static void evict_file_page(struct spte* spte) {
    uint32_t* pagedir = spte->owner_thread->pagedir;
    bool dirty = pagedir_is_dirty(pagedir, spte->frame->physical_mem_frame_base);
    if (dirty) {
        spte->location = SWAP_PAGE;
        evict_swap_page(spte);
    }
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: moves a mmapped page from physical memory to another
    location
 --------------------------------------------------------------------
 */
static void evict_mmaped_page(struct spte* spte) {
    
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION:
 NOTE: need to break the prevous mapping from virtual address to
    physcial frame by calling page_dir_clear_page
 --------------------------------------------------------------------
 */
bool evict_page_from_physical_memory(struct spte* spte) {
    ASSERT(spte != NULL);
    switch (spte->location) {
        case SWAP_PAGE:
            evict_swap_page(spte);
            break;
        case FILE_PAGE:
            evict_file_page(spte);
            break;
        case MMAPED_PAGE:
            evict_mmaped_page(spte);
            break;
        default:
            break;
    }
    clear_page(spte->page_id, spte->owner_thread);
    return true;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES: declare a local spte on the stack to 
    search against. 
 NOTE: Returns null if no element could be found. 
 --------------------------------------------------------------------
 */
struct spte* find_spte(void* virtual_address) {
    void* spte_id = (void*)pg_round_down(virtual_address);
    struct spte dummy;
    dummy.page_id = spte_id;
    
    struct hash* table = &thread_current()->spte_table;
    struct hash_elem* match = hash_find(table, &dummy.elem);
    if (match) {
        return hash_entry(match, struct spte, elem);
    }
    return NULL;
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: hashes based on the spte id which is the rounded
    down virtual address, ie the page number. 
 --------------------------------------------------------------------
 */
static unsigned hash_func(const struct hash_elem* e, void* aux UNUSED) {
    struct spte* spte = hash_entry(e, struct spte, elem);
    return hash_int((uint32_t)spte->page_id);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: Compares the keys stored in elements a and b. 
    Returns true if a is less than b, false if a is greater 
    than or equal to b.
 --------------------------------------------------------------------
 */
static bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
    struct spte* A_spte = hash_entry(a, struct spte, elem);
    struct spte* B_spte = hash_entry(b, struct spte, elem);
    if ((uint32_t)A_spte->page_id < (uint32_t)B_spte->page_id) return true;
    return false;
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: frees all resources associated with a given spte
    entry. 
 NOTE: need to check if the page is currently in a frame. If it is
    we have to free those frame resources.
 --------------------------------------------------------------------
 */
static void free_hash_entry(struct hash_elem* e, void* aux UNUSED) {
    struct spte* spte = hash_entry(e, struct spte, elem);
    if (spte->is_loaded) {
        frame_handler_palloc_free(spte);
    }
    free_spte(spte);
    
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES: initializes the given hash table.
 --------------------------------------------------------------------
 */
void init_spte_table(struct hash* thread_hash_table) {
    bool success = hash_init(thread_hash_table, hash_func, less_func, NULL);
    if (!success) {
        PANIC("Could not initialize the spte_hash table");
    }
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
void free_spte_table(struct hash* thread_hash_table) {
    hash_destroy(thread_hash_table, free_hash_entry);
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: Implimentation verifies that there's not already a 
    page at that virtual address, then map our page there. 
 --------------------------------------------------------------------
 */
bool install_page(void *upage, void *kpage, bool writable) {
    struct thread *t = thread_current ();
    
    return (pagedir_get_page (t->pagedir, upage) == NULL
            && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
void clear_page(void* upage, struct thread* t) {
    ASSERT(thread_current() == t);
    pagedir_clear_page(t->pagedir, upage);
}

#define PUSHA_BYTE_DEPTH 32
#define PUSH_BYTE_DEPTH 4
#define MAX_STACK_SIZE_IN_BYTES 8392000
/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
bool is_valid_stack_access(void* esp, void* user_virtual_address) {
    uint32_t stack_bottom_limit = (uint32_t)(PHYS_BASE - MAX_STACK_SIZE_IN_BYTES);
    if ((uint32_t)user_virtual_address < stack_bottom_limit) {
        return false;
    }
    if ((uint32_t)user_virtual_address >= (uint32_t)PHYS_BASE) {
        return false;
    }
    void* acceptable_depth_pushA = (void*)((char*)esp - PUSHA_BYTE_DEPTH);
    if ((uint32_t)user_virtual_address == (uint32_t)acceptable_depth_pushA) {
        return true;
    }
    void* acceptable_depth_push = (void*)((char*)esp - PUSH_BYTE_DEPTH);
    if ((uint32_t)user_virtual_address == (uint32_t)acceptable_depth_push) {
        return true;
    }
    return false;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: we take care of freeing the palloc'd page on error within 
    frame_handler_palloc
 --------------------------------------------------------------------
 */
bool grow_stack(void* page_id) {
    struct spte* spte = create_spte_and_add_to_table(SWAP_PAGE, page_id, true, true, NULL, 0, 0, 0);
    bool outcome = frame_handler_palloc(true, spte, false);
    return outcome;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: if the page is not curently in memory, this case is easy, as 
    we simply call frame_handler_palloc(false, spte, true), as the
    last true field will pin frame. 
 NOTE: if the page is in memory, synchronization becomes an issue.
    In this case we have to repeatedly try to acquire the 
    lock for the page if it is in physical memory. If we find that
    it isn't at any time during this process, we revert to the above
    easy case.
 --------------------------------------------------------------------
 */
void pin_page(void* virtual_address) {
    struct spte* spte = find_spte(virtual_address);
    if (spte->is_loaded != true) {
        frame_handler_palloc(false, spte, true);
    } else {
        while (true) {
            bool success = aquire_frame_lock(spte->frame, spte);
            if (success) {
                break;
            } else if (spte->is_loaded != true) {
                frame_handler_palloc(false, spte, true);
                break;
            }
        }
        
    }
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
void un_pin_page(void* virtual_address) {
    struct spte* spte = find_spte(virtual_address);
    ASSERT(lock_held_by_current_thread(&spte->frame->frame_lock));
    lock_release(&spte->frame->frame_lock);
}



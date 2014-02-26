#include <stdbool.h>
#include "threads/thread.h"
#include "threads/malloc.h"  /* XXX can we malloc? */
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

//-----------------NOTES MOVED TO BOTTOM OF FILE-------------------//


/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
struct spte* create_spte_and_add_to_table(page_location location, void* page_id, bool is_writeable, bool is_loaded, bool pinned, struct file* file_ptr, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {
    struct spte* spte = malloc(sizeof(struct spte));
    if (spte == NULL) {
        PANIC("Could not allocate spte");
    }
    spte->location = location;
    spte->owner_thread = thread_current();
    spte->page_id = page_id;
    spte->is_writeable = is_writeable;
    spte->is_loaded = is_loaded;
    spte->is_pinned = pinned;
    spte->file_ptr = file_ptr;
    spte->offset_in_file = offset;
    spte->read_bytes = read_bytes;
    spte->zero_bytes = zero_bytes;
    spte->swap_index = 0; //IS THIS CORRECT???
    struct hash* target_table = &thread_current()->spte_table;
    bool outcome = hash_insert(target_table, &spte->elem);
    if (outcome == false) {
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
 IMPLIMENTATION NOTES:
 NOTE: Need to implement these functions. 
 NOTE: Need to add the mapping by calling page_dir_set_page
 --------------------------------------------------------------------
 */
bool load_page_into_physical_memory(struct spte* spte, void* physcial_mem_address) {
    ASSERT(spte != NULL);
    switch (spte->location) {
        case SWAP_PAGE:
            load_stack_page(spte, void* physcial_mem_address);
            break;
        case FILE_PAGE:
            load_file_page(spte, void* physcial_mem_address);
            break;
        case MMAPED_PAGE:
            load_mmaped_page(spte, void* physcial_mem_address);
            break;
        default:
            break;
    }
    return install_page(spte->page_id, physcial_mem_address, spte->is_writeable);
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION:
 NOTE: need to break the prevous mapping from virtual address to
    physcial frame by calling page_dir_clear_page
 --------------------------------------------------------------------
 */
bool evict_page_from_physical_memory(struct spte* spte, void* physcial_mem_address) {
    ASSERT(spte != NULL);
    switch (spte->location) {
        case SWAP_PAGE:
            evict_stack_page(spte, void* physcial_mem_address);
            break;
        case FILE_PAGE:
            evict_file_page(spte, void* physcial_mem_address);
            break;
        case MMAPED_PAGE:
            evict_mmaped_page(spte, void* physcial_mem_address);
            break;
        default:
            break;
    }
    clear_page(spte->page_id, spte->owner_thread);
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES: declare a local spte on the stack to 
    search against. 
 NOTE: Returns null if no element could be found. 
 --------------------------------------------------------------------
 */
struct spte* find_spte(void* virtual_address) {
    void* spte_id = page_round_down(virtual_address);
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
    return hash_int(spte->vaddr);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: Compares the keys stored in elements a and b. 
    Returns true if a is less than b, false if a is greater 
    than or equal to b.
 --------------------------------------------------------------------
 */
static bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
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
        //here we free any resources for the frame
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
static bool install_page(void *upage, void *kpage, bool writable) {
    struct thread *t = thread_current ();
    
    return (pagedir_get_page (t->pagedir, upage) == NULL
            && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
static void clear_page(void* upage, struct thread* t) {
    ASSERT(thread_current() == t);
    pagedir_clear_page(t->pagedir, upage);
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
bool is_valid_stack_access(void* esp, void* user_virtual_address) {
    
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: we take care of freeing the palloc'd page on error within 
    frame_handler_palloc
 --------------------------------------------------------------------
 */
bool grow_stack(void* page_id) {
    struct spte* spte = create_spte_and_add_to_table(SWAP_PAGE, page_id, true, true, false, NULL, 0, 0, 0);
    bool outcome = frame_handler_palloc(true, spte);
    return outcome;
}



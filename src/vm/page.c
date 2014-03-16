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

static void free_hash_entry(struct hash_elem* e, void* aux UNUSED);


static void
assert_spte_consistency(struct spte* spte)
{
    ASSERT (spte != NULL);
    ASSERT (spte->location == SWAP_PAGE ||
            spte->read_bytes + spte->zero_bytes == PGSIZE);

    ASSERT (spte->frame == NULL ||
            spte->frame->physical_mem_frame_base >= PHYS_BASE);

}


/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: we aquire the lock here, to handle the case where we 
    allocate a swap page, in which case we pass is_loaded
    value of true. 
 NOTE: On failure of malloc, we return null. 
 NOTE: On failure to add the spte to the spte table, 
    we exit the thread. 
 --------------------------------------------------------------------
 */
struct spte* create_spte_and_add_to_table(page_location location, void* page_id, bool is_writeable, bool is_loaded, struct file* file_ptr, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {

    struct spte* spte = malloc(sizeof(struct spte));
    if (spte == NULL) {
        return NULL;
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
    spte->swap_index = 0; 
    lock_init(&spte->page_lock);
    lock_acquire(&spte->page_lock);
    struct hash* target_table = &thread_current()->spte_table;
    struct spte* outcome = hash_entry(hash_insert(target_table, &spte->elem), struct spte, elem);
    if (outcome != NULL) {
        thread_current()->vital_info->exit_status = -1;
        if (thread_current()->is_running_user_program) {
            printf("%s: exit(%d)\n", thread_name(), -1);
        }
        thread_exit();
    }

    assert_spte_consistency(spte);
    return spte;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
void free_spte(struct spte* spte) {
    assert_spte_consistency(spte);
    free(spte);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: loads a page from swap to physical memory
 --------------------------------------------------------------------
 */
static void load_swap_page(struct spte* spte) {
    assert_spte_consistency(spte);
    read_from_swap(spte->frame->physical_mem_frame_base, spte->swap_index);
    assert_spte_consistency(spte);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: loads a page from swap to physical memory
 NOTE: this function is called by frame_handler_palloc, which
    locks the frame, so we do not need to pin here.
 --------------------------------------------------------------------
 */
static void load_file_page(struct spte* spte) {
    assert_spte_consistency(spte);

    if (spte->zero_bytes == PGSIZE) {
        memset(spte->frame->physical_mem_frame_base, 0, PGSIZE);
        return;
    }
    //lock_acquire(&file_system_lock);
    uint32_t bytes_read = file_read_at (spte->file_ptr, spte->frame->physical_mem_frame_base, spte->read_bytes, spte->offset_in_file);
    //lock_release(&file_system_lock);
    if (bytes_read != spte->read_bytes) {
        thread_current()->vital_info->exit_status = -1;
        if (thread_current()->is_running_user_program) {
            printf("%s: exit(%d)\n", thread_name(), -1);
        }
        thread_exit();
    }
    if (spte->read_bytes != PGSIZE) {
        memset (spte->frame->physical_mem_frame_base + spte->read_bytes, 0, spte->zero_bytes);
    }

    assert_spte_consistency(spte);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: loads a memory mapped page into memory
 --------------------------------------------------------------------
 */
static void load_mmaped_page(struct spte* spte) {
    /* No difference between loading mmapped pages and file pages in */
    return load_file_page(spte);
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: we only add the mapping of virtual address to frame 
    after the load has completed. 
 NOTE: This function assumes that the caller has aquired the 
    page lock, thus ensuring that eviction and loading
    cannot be done at the same time. 
 --------------------------------------------------------------------
 */
bool load_page_into_physical_memory(struct spte* spte, bool is_fresh_stack_page) {
    assert_spte_consistency(spte);
    ASSERT(lock_held_by_current_thread(&spte->frame->frame_lock));
    if (is_fresh_stack_page == false) {
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
                thread_current()->vital_info->exit_status = -1;
                if (thread_current()->is_running_user_program) {
                    printf("%s: exit(%d)\n", thread_name(), -1);
                }
                thread_exit();
                break;
        }
    }
    bool success = install_page(spte->page_id, spte->frame->physical_mem_frame_base, spte->is_writeable);
    assert_spte_consistency(spte);
    return success;
}

/*
 --------------------------------------------------------------------
 DESCRIPTION:In this function, we copy a page from a physcial
    frame to a swap slot
 --------------------------------------------------------------------
 */
static void evict_swap_page(struct spte* spte) {
    assert_spte_consistency(spte);
    uint32_t swap_index = write_to_swap(spte->frame->physical_mem_frame_base);
    spte->swap_index = swap_index;
    assert_spte_consistency(spte);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: moves a page containing file data to a swap slot
    if the page is dirty. Else, we do nothing.
 NOTE: Once the page is dirty once, as discussed in OH, we treat
    it as always dirty, which means it will forever more be a swap. 
 --------------------------------------------------------------------
 */
static void evict_file_page(struct spte* spte) {
    assert_spte_consistency(spte);
    lock_acquire(&spte->owner_thread->pagedir_lock);
    uint32_t* pagedir = spte->owner_thread->pagedir;
    bool dirty = pagedir_is_dirty(pagedir, spte->frame->resident_page->page_id);
    lock_release(&spte->owner_thread->pagedir_lock);
    if (dirty) {
        spte->location = SWAP_PAGE;
        evict_swap_page(spte);
    }
    assert_spte_consistency(spte);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: moves a mmapped page from physical memory to another
    location
 NOTE: Reset's the dirty value if the page is currently dirty so
    that subsequent checks will only write if dirty again. 
 --------------------------------------------------------------------
 */
static void evict_mmaped_page(struct spte* spte) {
    assert_spte_consistency(spte);

    uint32_t* pagedir = spte->owner_thread->pagedir;
    lock_acquire(&spte->owner_thread->pagedir_lock);
    void *page_id = spte->page_id;
    bool dirty = pagedir_is_dirty(pagedir, page_id);
    if (dirty) {
        pagedir_set_dirty(pagedir, page_id, false);
        lock_release(&spte->owner_thread->pagedir_lock);
        //lock_acquire(&file_system_lock);
        file_write_at (spte->file_ptr, spte->frame->physical_mem_frame_base, spte->read_bytes,
                       spte->offset_in_file);
        //lock_release(&file_system_lock);
    } else {
        lock_release(&spte->owner_thread->pagedir_lock);
    }

    assert_spte_consistency(spte);
}


/*
 --------------------------------------------------------------------
 DESCRIPTION: frees the resources aquired to mmap.
 --------------------------------------------------------------------
 */
struct list_elem *
munmap_state(struct mmap_state *mmap_s, struct thread *t)
{
    void *page;
    //lock_acquire(&file_system_lock);
    int size = file_length(mmap_s->fp);
    //lock_release(&file_system_lock);
    
    /* Write back dirty pages, and free all pages in use */
    for (page = mmap_s->vaddr; page < mmap_s->vaddr + size; page += PGSIZE)
    {
        struct spte *entry = find_spte(page, t);
        ASSERT (entry != NULL);
        lock_acquire(&entry->page_lock);
        if (entry->is_loaded == true) {
            if (lock_held_by_current_thread(&entry->frame->frame_lock) == false) {
                lock_acquire(&entry->frame->frame_lock);
            }
            clear_page(entry->page_id, entry->owner_thread);
            evict_mmaped_page(entry);
            entry->is_loaded = false;
            palloc_free_page(entry->frame->physical_mem_frame_base);
            lock_release(&entry->frame->frame_lock);
            entry->frame->resident_page = NULL;
            entry->frame = NULL;
        }
        lock_release(&entry->page_lock);
        hash_delete(&t->spte_table, &entry->elem);
        free(entry);
    }

    //lock_acquire(&file_system_lock);
    file_close(mmap_s->fp);
    //lock_release(&file_system_lock);
    
    struct list_elem *next = list_remove(&mmap_s->elem);
    free(mmap_s);
    return next;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION:
 NOTE: need to break the prevous mapping from virtual address to
    physcial frame by calling page_dir_clear_page
 --------------------------------------------------------------------
 */
bool evict_page_from_physical_memory(struct spte* spte) {
    assert_spte_consistency(spte);
    clear_page(spte->page_id, spte->owner_thread);
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
            thread_current()->vital_info->exit_status = -1;
            if (thread_current()->is_running_user_program) {
                printf("%s: exit(%d)\n", thread_name(), -1);
            }
            thread_exit();
            break;
    }
    assert_spte_consistency(spte);
    return true;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES: declare a local spte on the stack to 
    search against. 
 NOTE: Returns null if no element could be found. 
 --------------------------------------------------------------------
 */
struct spte* find_spte(void* virtual_address, struct thread *t) {
    void* spte_id = (void*)pg_round_down(virtual_address);
    struct spte dummy;
    dummy.page_id = spte_id;
    
    struct hash* table = &t->spte_table;
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
    lock_acquire(&spte->page_lock);
    if (spte->is_loaded) {
        clear_page(spte->page_id, spte->owner_thread);
        frame_handler_palloc_free(spte);
        spte->is_loaded = false;
    }
    lock_release(&spte->page_lock);
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
        thread_current()->vital_info->exit_status = -1;
        if (thread_current()->is_running_user_program) {
            printf("%s: exit(%d)\n", thread_name(), -1);
        }
        thread_exit();
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
    
    lock_acquire(&t->pagedir_lock);
    bool result = (pagedir_get_page (t->pagedir, upage) == NULL
            && pagedir_set_page (t->pagedir, upage, kpage, writable));
    lock_release(&t->pagedir_lock);
    return result;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES: note, pagedir may be null of the thread
    is exiting and has set its pagedir to null. Thus, we 
    must check for this case.
 --------------------------------------------------------------------
 */
void clear_page(void* upage, struct thread* t) {
     lock_acquire(&t->pagedir_lock);
    if (t->pagedir != NULL) {
        pagedir_clear_page(t->pagedir, upage);
    }
    lock_release(&t->pagedir_lock);
}

#define PUSHA_BYTE_DEPTH 32
#define PUSH_BYTE_DEPTH 4
#define MAX_STACK_SIZE_IN_BYTES 8392000
/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: we need to check if the stack pointer is below the faulting 
    address.
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
    if ((uint32_t)user_virtual_address >= (uint32_t)esp) {
        return true;
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
 NOTE: Create_spte_and_add aquires the page lock for us, to handle
    this race between creation and access to swap memory before 
    frame gets allocated.
 --------------------------------------------------------------------
 */
bool grow_stack(void* page_id) {
    struct spte* spte = create_spte_and_add_to_table(SWAP_PAGE, page_id, true, true, NULL, 0, 0, 0);
    if (spte == NULL) {
        return false;
    }
    bool outcome = frame_handler_palloc(true, spte, false, true);
    return outcome;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: if the page is not curently in memory, this case is easy, as 
    we simply call frame_handler_palloc(false, spte, true), as the
    last true field will pin frame. 
 NOTE: if the page is in memory, synchronization becomes an issue.
    In this case we have to try to acquire the 
    lock for the page if it is in physical memory. If we aquire the 
    lock, and the page is still in the frame, we are good, else,
    we have to hunt down the new frame.
 --------------------------------------------------------------------
 */
void pin_page(void* virtual_address) {
    struct spte* spte = find_spte(virtual_address, thread_current());
    lock_acquire(&spte->page_lock);
    if (spte->is_loaded != true) {
        frame_handler_palloc(false, spte, true, false);
    } else {
        lock_release(&spte->page_lock);
        lock_acquire(&spte->page_lock);
        bool success = aquire_frame_lock(spte->frame, spte);
        if (success) {
            lock_release(&spte->page_lock);
            return;
        } else {
            if (spte->is_loaded != true) {
                frame_handler_palloc(false, spte, true, false);
                return;
            } else {
                if (spte->is_loaded == true) {
                    frame_handler_palloc_free(spte);
                    frame_handler_palloc(false, spte, true, false);
                    return;
                }
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
    struct spte* spte = find_spte(virtual_address, thread_current());
    lock_acquire(&spte->page_lock);
    ASSERT(lock_held_by_current_thread(&spte->frame->frame_lock));
    lock_release(&spte->frame->frame_lock);
    lock_release(&spte->page_lock);
}



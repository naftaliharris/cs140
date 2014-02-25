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
struct spte* create_spte(uint32_t paddr, uint32_t vaddr, page_loc loc, page_type type, struct file* file_ptr) {
    struct spte* new_spte = malloc(sizeof(struct spte));
    if (new_spte == NULL) {
        PANIC("Malloc returned null in create_spte");
    }
    new_spte->paddr = paddr;
    new_spte->vaddr = vaddr;
    new_spte->loc = loc;
    new_spte->type = type;
    new_spte->file_ptr = file_ptr;
    //HAVE TO ADD TO THREAD SPECIFIC DATA STRUCTURE
    return new_spte;
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
 --------------------------------------------------------------------
 */
void load_page_into_physical_memory(struct spte* spte) {
    ASSERT(spte != NULL);
    switch (spte->type) {
        case STACK_PAGE:
            load_stack_page(spte);
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
}














bool
map_page (struct thread *t, void *upage, void *kpage, bool writable)
{
    // TODO: make sure that upage is page aligned

    /* Verify that there's not already a page at that virtual
     address, then map our page there. */
    bool success = (pagedir_get_page (t->pagedir, upage) == NULL
                 && pagedir_set_page (t->pagedir, upage, kpage, writable));

    if (success)
    {
        struct spte *entry = malloc(sizeof(struct spte));  /* XXX: Is this kosher? */
        if (entry == NULL)
            return false;
        entry->paddr = (uint32_t)kpage;
        entry->vaddr = (uint32_t)upage;
        entry->loc = LOC_MEMORY;
        list_push_back (&t->spt, &entry->elem);
    }
    return success;
}

/* Currently just swaps the page out to the swap partition. */
void evict_page (struct thread *t, void *page)
{
    struct spte *entry = find_spte(t, page);
    ASSERT (entry != NULL);
    ASSERT (entry->loc == LOC_MEMORY);
    pagedir_clear_page(t->pagedir, page);

    /* If the page is writeable, place into swap */
    if (pagedir_is_writeable(t->pagedir, page))
    {
        entry->paddr = write_to_swap(page);
        entry->loc = LOC_SWAPPED;
    } else {
        /* TODO: Figure out where on disk this page resides */
        entry->paddr = (uint32_t)NULL;
        entry->loc = LOC_DISK;
    }
}

struct spte *
find_spte (struct thread *t, void *page)
{
    struct list_elem *e;

    for (e = list_begin(&t->spt); e != list_end(&t->spt); e = list_next (e))
    {
      struct spte *entry = list_entry (e, struct spte, elem);
      if (entry->paddr == (uint32_t)page)
          return entry;
    }

    return NULL;
}

void
free_page (struct thread *t, struct spte *entry)
{
  while(true)
  {
    switch (entry->loc)
    {
        case LOC_MEMORY:
            if(frame_handler_free_page(entry->paddr, entry->vaddr, t))
              return;
        case LOC_SWAPPED:
            free_slot (entry->paddr);
            return;
        case LOC_DISK:
            /* Don't need to do anything */
            break;
    }
  }
}

void
free_spt (struct list *spt)
{
    struct list_elem *e;
    for (e = list_begin(spt); e != list_end(spt); e = list_next (e))
        free_page(thread_current(), list_entry (e, struct spte, elem));

    /* TODO: Free the malloc'd memory */
}





/* Notes from Naftali and Connor's meeting, (to be deleted shortly). */

/* Initialize the SPT in process.c when the user progam's pagedir is initialized.
 *
 * Perhaps write function wrapping pagedir_set_page and pagedir_clear_page that also modify the SPT.
 *
 * Perhaps write nice little evict_page functions and similar that will swap to and from disk.
 *
 * Write these API calls:
 *
 * 1)
 * map_page function does two things: one, calls pagedir_set_page, (which puts into the PT), and
 * two, it adds it to the supplementary page table, (marking it as in memory). Model off of
 * install_page function in process.c. Return just like install_page, (true for success,
 * false for failure).
 *
 * 2)
 * evict_page function: Get's rid of the referred page, putting it swapping it to disk or just
 * removing the page (as necessary). (Does NOT touch the frame table, which will be taken care of
 * by the frame table code that will call this function).
 *
 * 3)
 * remove_page function: For when we free pages. Will interact with pagedir_destroy.
 *
 * Take care of freeing pages, and use frame_handler_free_page. Note that may need to be careful
 * of race condition where try to free and evict page at the same time.
 */

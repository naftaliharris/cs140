#include <stdbool.h>
#include "threads/thread.h"
#include "threads/malloc.h"  /* XXX can we malloc? */
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

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


bool
map_page (void *upage, void *kpage, bool writable)
{
    struct thread *t = thread_current ();

    /* Verify that there's not already a page at that virtual
     address, then map our page there. */
    bool success = (pagedir_get_page (t->pagedir, upage) == NULL
                 && pagedir_set_page (t->pagedir, upage, kpage, writable));

    if (success)
    {
        struct spte *entry = malloc(sizeof(struct spte));  /* XXX: Is this kosher? */
        if (entry == NULL)
            return false;
        entry->addr = (uint32_t)kpage;
        entry->loc = LOC_MEMORY;
        list_push_back (&t->spt, &entry->elem);
    }
    return true;
}

/* Currently just swaps the page out to the swap partition. */
void evict_page (void *page)
{
    struct spte *entry = find_spte(page);
    ASSERT (entry != NULL);
    ASSERT (entry->loc == LOC_MEMORY);

    struct thread *t = thread_current ();

    /* If the page is writeable, place into swap */
    if (pagedir_is_writeable(t->pagedir, page))
    {
        entry->addr = write_to_swap(page);
        entry->loc = LOC_SWAPPED;
    } else {
        /* TODO: Figure out where on disk this page resides */
        entry->addr = (uint32_t)NULL;
        entry->loc = LOC_DISK;
    }
}

struct spte *
find_spte (void *page)
{
    struct thread *t = thread_current ();
    struct list_elem *e;

    for (e = list_begin(&t->spt); e != list_end(&t->spt); e = list_next (e))
    {
      struct spte *entry = list_entry (e, struct spte, elem);
      if (entry->addr == (uint32_t)page)
          return entry;
    }

    return NULL;
}

void
free_page (struct spte *entry)
{
    switch (entry->loc)
    {
        case LOC_MEMORY:
            break;
        case LOC_SWAPPED:
            free_slot (entry->addr);
            break;
        case LOC_DISK:
            /* Don't need to do anything */
            break;
    }
}

void
free_spt (struct list *spt)
{
    struct list_elem *e;
    for (e = list_begin(spt); e != list_end(spt); e = list_next (e))
        free_page(list_entry (e, struct spte, elem));

    /* TODO: Free the malloc'd memory */
}

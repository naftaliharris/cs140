#include <stdbool.h>
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

/* Notes */

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



/* 
 ----------------------------------------------------------------
 Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. 
 ----------------------------------------------------------------
 */

bool
map_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  bool success = (pagedir_get_page (t->pagedir, upage) == NULL
                 && pagedir_set_page (t->pagedir, upage, kpage, writable));

  //t->spt 
}

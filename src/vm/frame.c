#include "vm/frame.h"
#include "userprog/pagedir.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <debug.h>
#include "threads/pte.h"
#include "threads/malloc.h"
#include "threads/palloc.h"

static struct list frame_table = LIST_INITIALIZER(frame_table);
static struct lock frame_table_lock = LOCK_INITIALIZER(frame_table_lock);

static struct frame* evict_frame(void);

bool
frame_handler_create_user_page(void* vaddr, bool writeable, bool zeroed, *create_page_func func, void* aux)
{
  struct frame* frame = NULL;
  void* kaddr = palloc_get_page (PAL_USER | (zeroed ? PAL_ZERO : 0));
  if (kaddr != NULL)
  {
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
   /* Verify that there's not already a page at that virtual
     address, then map our page there. */
    struct thread *t = thread_current ();
    if(pagedir_get_page (t->pagedir, vaddr) == NULL)
    {
      struct frame new_frame;
      new_frame.owner_thread = t;
      new_frame.kaddr = kaddr;
      new_frame.vaddr = vaddr;
      lock_init(&(new_frame.lock));        
      
      frame = malloc(sizeof(struct frame));
      if(frame == NULL)
      {
        // malloc failed
        // Terminate gracefully?
        debug_panic();
      }
      memcpy(frame, &new_frame, sizeof(struct frame));
      
      lock_acquire(&(frame->lock));
      lock_acquire(&frame_table_lock);
      list_push_back(&frame_table, &(((struct frame*)f)->elem));
      lock_release(&frame_table_lock);
      if(!((func == NULL || func(kaddr, aux)) && pagedir_set_page (t->pagedir, vaddr, kaddr, writable)))
      {
        lock_acquire(&frame_table_lock);
        list_remove(&(frame->elem));
        lock_release(&frame_table_lock);
        palloc_free_page(frame->kaddr);
        lock_release(&(frame->lock));
        free(frame);
        return false;
      }
      lock_release(&(frame->lock));
    }
    else
    {
      // Page was already mapped
      // Shouldn't happen?
      debug_panic();
    }
  }
  else
  {
    debug_panic();
    frame = evict_frame(); // acquires frame->lock
    kaddr = frame->kaddr;
    if(!((func == NULL || func(kaddr, aux)) && pagedir_set_page (t->pagedir, vaddr, kaddr, writable)))
    {
      lock_acquire(&frame_table_lock);
      list_remove(&(frame->elem));
      lock_release(&frame_table_lock);
      palloc_free_page(frame->kaddr);
      lock_release(&(frame->lock));
      free(frame);
      return false;
    }
    lock_release(&(frame->lock));
  }
  return true;
}

bool
frame_handler_free_page(struct frame* frame)
{
  debug_panic();
  // More thought needs to be put into this in regards to synchronization
  // Freeing a page that is in a frame
  // while(page_table_free_page() == false); page_table_free_page can free from swap or call this func
  // this func makes sure that the correct frame is being freed
  lock_acquire(&(frame->lock));
  lock_acquire(&frame_table_lock);
  list_remove(&(frame->elem));
  lock_release(&frame_table_lock);
  palloc_free_page(frame->kaddr);
  lock_release(&(frame->lock));
  free(frame);
}

static struct frame*
evict_frame(void)
{
  // set page to 0s
  return NULL;
}
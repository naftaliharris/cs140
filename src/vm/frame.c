#include "vm/frame.h"
#include "userprog/pagedir.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <debug.h>
#include "threads/pte.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"

struct frame {
  struct thread* owner_thread; // if NULL, no page mapped
  void* vaddr; // user virtual address, for use when referencing page table, and perhaps for when converting between kaddr/uaddr
  struct lock lock;
};

static struct frame* frame_table;
static size_t total_frames;
static void* first_frame;
static struct lock frame_table_lock;

static struct frame* evict_frame(void);

static inline uint32_t
get_frame_index(void* kaddr)
{
  ASSERT (first_frame != NULL);
  ASSERT ((uint32_t)first_frame <= (uint32_t)kaddr);
  return ((uint32_t)kaddr - (uint32_t)first_frame) >> 12;
}

bool
frame_handler_init(size_t num_frames, uint8_t* frame_base)
{
  total_frames = num_frames;
  first_frame = frame_base;
  lock_init(&frame_table_lock);
  frame_table = malloc(sizeof(struct frame) * num_frames);
  if(frame_table == NULL)
  {
    return false;
  }
  struct frame basic_frame;
  lock_init(&(basic_frame.lock));
  int i;
  for(i = 0; i < num_frames; i++)
  {
    memcpy((frame_table + i), &basic_frame, sizeof(struct frame));
  }
  return true;
}

bool
frame_handler_create_user_page(void* vaddr, bool writeable, bool zeroed, create_page_func* func, void* aux)
{
  struct frame* frame = NULL;
  bool success = false;
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
    lock_acquire(&frame_table_lock);
    struct frame* frame = frame_table + get_frame_index(kaddr);
    ASSERT (frame->owner_thread == NULL);
    lock_acquire(&(frame->lock));
    lock_release(&frame_table_lock);
    frame->owner_thread = t;
    frame->vaddr = vaddr;
    if(!((func == NULL || func(kaddr, aux)) && pagedir_set_page (t->pagedir, vaddr, kaddr, writeable)))
    {
      palloc_free_page(kaddr);
      frame->owner_thread = NULL;
    }
    else
    {
      success = true;
    }
    lock_release(&(frame->lock));
  }
  else
  {
    PANIC("Out Of Frames");
    frame = evict_frame(); // acquires frame->lock
  }
  return success;
}

bool
frame_handler_free_page(void* kaddr, void* uaddr, struct thread* owner)
{
  PANIC("Cannot free pages");
  // More thought needs to be put into this in regards to synchronization
  // Freeing a page that is in a frame
  // while(page_table_free_page() == false); page_table_free_page can free from swap or call this func
  // this func makes sure that the correct frame is being freed
  /*lock_acquire(&(frame->lock));
  lock_acquire(&frame_table_lock);
  list_remove(&(frame->elem));
  lock_release(&frame_table_lock);
  palloc_free_page(frame->kaddr);
  lock_release(&(frame->lock));
  free(frame);*/
  return false;
}

static struct frame*
evict_frame(void)
{
  // set page to 0s
  return NULL;
}
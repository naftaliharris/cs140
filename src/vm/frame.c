#include "vm/frame.h"
#include "userprog/pagedir.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <debug.h>
#include <stdio.h>
#include "threads/pte.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "vm/page.h"


/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
void init_frame_table(size_t num_frames, uint8_t* frame_base) {
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: evict frame. This is a private function of the frame 
    file. In this function, we check the list of frames, and when
    we find one suitable for eviction, we write the contents of the
    frame to associated memory location, and then remove the frame
    from the list, and then return the frame.
 --------------------------------------------------------------------
 */
static struct frame* evict_frame() {
    
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES: 
 NOTE: we aquire the frame table lock and then add the frame
    to the table.
 NOTE: eviction in the case of the list is very nice. We always
    begin our eviction check from the beginning of the list, and 
    work our way to the back. We also insert frames at the end
    of the list.
 --------------------------------------------------------------------
 */
void* allocate_user_page(bool zeros, struct spte* spte) {
    void* physical_memory_addr = palloc_get_page (PAL_USER | (zeros ? PAL_ZERO : 0));
    if (physical_memory_addr == NULL) {
        struct frame* evicted_frame = evict_frame();
    } else {
        struct frame* new_frame = malloc(sizeof(struct frame));
        if (new_frame == NULL) {
            PANIC("Could not malloc a frame struct.");
        }
        new_frame->resident_page = spte;
        new_frame->physical_mem_frame_base = physical_memory_addr;
        lock_init(&frame_lock);
        lock_aquire(&frame_list_lock);
        list_push_back(&list_of_frames, &new_frame->elem);
        lock_release(&frame_list_lock);
        return physical_memory_addr;
    }
    
}






//==============================
static struct frame* frame_table;
static size_t total_frames;
static void* first_frame;
struct lock frame_evict_lock;
static uint32_t frame_table_iterator = 0;

static struct frame* evict_frame(void);

static inline uint32_t
get_frame_index(void* kaddr)
{
    ASSERT (first_frame != NULL);
    ASSERT ((uint32_t)first_frame <= (uint32_t)kaddr);
    uint32_t index = ((uint32_t)kaddr - (uint32_t)first_frame) >> 12;
    ASSERT (index < total_frames);
    return index;
}

bool
frame_handler_init(size_t num_frames, uint8_t* frame_base)
{
    total_frames = num_frames;
    first_frame = frame_base;
    lock_init(&frame_evict_lock);
    frame_table = malloc(sizeof(struct frame) * num_frames);
    if(frame_table == NULL)
    {
        return false;
    }
    struct frame basic_frame;
    basic_frame.owner_thread = NULL;
    basic_frame.vaddr = NULL;
    uint32_t i;
    for(i = 0; i < num_frames; i++)
    {
        memcpy((frame_table + i), &basic_frame, sizeof(struct frame));
        lock_init(&(frame_table[i].lock));
        frame_table[i].kaddr = (void*)((uint32_t)first_frame + (i << 12));
    }
    return true;
}

bool
frame_handler_create_user_page(void* vaddr, bool writeable, bool zeroed, create_page_func* func, void* aux)
{
    bool success = false;
    struct thread *t = thread_current ();
    lock_acquire(&frame_evict_lock);
    void* kaddr = palloc_get_page (PAL_USER | (zeroed ? PAL_ZERO : 0));
    if (kaddr != NULL)
    {
        struct frame* frame = frame_table + get_frame_index(kaddr);
        lock_acquire(&(frame->lock));
        lock_release(&frame_evict_lock);
        ASSERT (frame->owner_thread == NULL);
        
        success = map_page (t, vaddr, kaddr, writeable) && (func == NULL || func(kaddr, aux));
        if(!success)
        {
            frame->owner_thread = NULL;
            barrier();
            palloc_free_page(kaddr);
        }
        else
        {
            frame->owner_thread = t;
            frame->vaddr = vaddr;
        }
        lock_release(&(frame->lock));
    }
    else
    {
        struct frame* frame = evict_frame(); // acquires frame->lock
        if(zeroed)
        {
            memset (frame->kaddr, 0, PGSIZE);
        }
        
        success = map_page (t, vaddr, kaddr, writeable) && (func == NULL || func(kaddr, aux));
        if(!success)
        {
            frame->owner_thread = NULL;
            barrier();
            palloc_free_page(frame->kaddr);
        }
        else
        {
            frame->owner_thread = t;
            frame->vaddr = vaddr;
        }
        lock_release(&(frame->lock));
    }
    return success;
}

bool
frame_handler_free_page(void* kaddr, void* uaddr, struct thread* owner)
{
    struct frame *frame = frame_table + get_frame_index(kaddr);
    lock_acquire(&(frame->lock));
    bool success = (frame->vaddr == uaddr && frame->owner_thread == owner);
    if(success)
    {
        frame->owner_thread = NULL;
        barrier();
        palloc_free_page(kaddr);
        memset(kaddr, 0, PGSIZE);
    }
    lock_release(&(frame->lock));
    return false;
}

// UPDATE FOR EXTRA CREDIT - CHECK ALL MAPPED PAGES
/*  iterate through list with clock pointer
 try to acquire a lock
 if fails, continue
 if succeeds, check accessed bit
 if accessed == 1, set it to 0 and continue
 else, return this frame
 Check the accessed bits for user AND kernel virtual address*/
static struct frame*
evict_frame(void)
{
    ASSERT (lock_held_by_current_thread(&frame_evict_lock));
    struct frame* frame;
    while(true)
    {
        frame = &(frame_table[frame_table_iterator]);
        if(lock_try_acquire(&(frame->lock)))
        {
            lock_release(&frame_evict_lock);
            if(frame->owner_thread == NULL)
            {
                PANIC("I don't think we're supposed to reach here");
                break;
            }
            uint32_t* pagedir = frame->owner_thread->pagedir;
            bool accessed = pagedir_is_accessed(pagedir, frame->kaddr) || pagedir_is_accessed(pagedir, frame->vaddr);
            if(accessed)
            {
                pagedir_set_accessed(pagedir, frame->kaddr, false);
                pagedir_set_accessed(pagedir, frame->vaddr, false);
                lock_release(&(frame->lock));
            }
            else
            {
                break;
            }
            lock_acquire(&frame_evict_lock);
        }
        frame_table_iterator++;
        if(frame_table_iterator >= total_frames)
        {
            frame_table_iterator = 0;
        }
    }
    evict_page(frame->owner_thread, frame->vaddr);
    return frame;
}

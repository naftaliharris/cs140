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

/* Globals */
static struct frame* frame_table;
static size_t total_frames;
static void* first_frame;
struct lock frame_evict_lock;
static uint32_t clock_hand = 0;

static void advance_clock_hand(void);


/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
void init_frame_table(size_t num_frames, uint8_t* frame_base) {
    total_frames = num_frames;
    first_frame = frame_base;
    lock_init(&frame_evict_lock);
    frame_table = malloc(sizeof(struct frame) * num_frames);
    if(frame_table == NULL)
    {
        PANIC("could not allocate frame table");
    }
    struct frame basic_frame;
    basic_frame.resident_page = NULL;
    basic_frame.physical_mem_frame_base = NULL;
    uint32_t i;
    for(i = 0; i < num_frames; i++)
    {
        memcpy((frame_table + i), &basic_frame, sizeof(struct frame));
        lock_init(&(frame_table[i].frame_lock));
        frame_table[i].physical_mem_frame_base = (void*)((uint32_t)first_frame + (i << 12));
    }
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: simply advances the clock_hand one frame forward.
 NOTE: the frame_evict_lock must be held by the current thread.
 --------------------------------------------------------------------
 */
static void advance_clock_hand() {
    ASSERT(lock_held_by_current_thread(&frame_evict_lock));
    clock_hand++;
    if (clock_hand >= total_frames) {
        clock_hand = 0;
    }
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: evict frame. This is a private function of the frame 
    file. In this function, we check the table of frames, and when
    we find one suitable for eviction, we write the contents of the
    frame to associated memory location, and then return the frame.
 NOTE: when checking the access bits, we need to make sure that if
    multiple virtual addresses refer to same frame, that they all
    see the update. Curently do so by only checking the kernel address.
 NOTE: we have to call advance clock_hand at the beginning to ensure 
    we move on from the prevous frame we evicted last. This will cause
    first sweep of eviction to begin at frame index 1, but that is
    ok, as subsequent frame sweeps will be in proper cycle.
 --------------------------------------------------------------------
 */
static struct frame* evict_frame(void) {
    ASSERT(lock_held_by_current_thread(&frame_evict_lock));
    advance_clock_hand();
    struct frame* frame = NULL;
    while (true) {
        frame = &(frame_table[clock_hand]);
        bool aquired = lock_try_acquire(&frame->frame_lock);
        if (aquired) {
            lock_release(&frame_evict_lock);
            if (frame->resident_page == NULL || frame->resident_page->owner_thread->pagedir == NULL) {
                lock_acquire(&frame_evict_lock);
                lock_release(&frame->frame_lock);
                advance_clock_hand();
                continue;
            }
            uint32_t* pagedir = frame->resident_page->owner_thread->pagedir;
            bool accessed = pagedir_is_accessed(pagedir, frame->resident_page->page_id);
            if (accessed) {
                pagedir_set_accessed(pagedir, frame->resident_page->page_id, false);
                lock_release(&frame->frame_lock);
            } else {
                evict_page_from_physical_memory(frame->resident_page);
                return frame;
            }
            lock_acquire(&frame_evict_lock);
        }
        advance_clock_hand();
    }
    return NULL;
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: given a physical memory address that is returned
    by palloc, returns the index into the frame table for the 
    corresponding frame struct.
 --------------------------------------------------------------------
 */
static inline uint32_t get_frame_index(void* physical_memory_addr) {
    ASSERT (first_frame != NULL);
    ASSERT ((uint32_t)first_frame <= (uint32_t)physical_memory_addr);
    uint32_t index = ((uint32_t)physical_memory_addr - (uint32_t)first_frame) >> 12;
    ASSERT (index < total_frames);
    return index;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES: 
 NOTE: Update so that this takes a boolean and releases the lock
    only if boolean is true
 NOTE: if is_fresh_stack_page is true, then we do not load from
    swao, because it is our first swap page.
 --------------------------------------------------------------------
 */
bool frame_handler_palloc(bool zeros, struct spte* spte, bool should_pin, bool is_fresh_stack_page) {
    lock_acquire(&frame_evict_lock);
    void* physical_memory_addr = palloc_get_page (PAL_USER | (zeros ? PAL_ZERO : 0));
    
    struct frame* frame;
    if (physical_memory_addr != NULL) {
        frame = frame_table + get_frame_index(physical_memory_addr);
        lock_acquire(&frame->frame_lock);
        lock_release(&frame_evict_lock);
        ASSERT(frame->resident_page == NULL)
    } else {
        frame = evict_frame();
    }
    
    if (zeros) memset(frame->physical_mem_frame_base, 0, PGSIZE);
    spte->frame = frame;
    
    bool success = load_page_into_physical_memory(spte, is_fresh_stack_page);
    
    if (!success) {
        barrier();
        spte->frame = NULL;
        spte->is_loaded = false;
        palloc_free_page(physical_memory_addr);
    } else {
        frame->resident_page = spte;
        spte->is_loaded = true;
    }
    if (should_pin == false) {
        lock_release(&frame->frame_lock);
    }
    return success;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: Not sure why we are returning a boolean here?
 --------------------------------------------------------------------
 */
bool frame_handler_palloc_free(struct spte* spte) {
    struct frame* frame = frame_table + get_frame_index(spte->frame->physical_mem_frame_base);
    lock_acquire(&frame->frame_lock);
    if (frame->resident_page == spte) {
        //in this case, we are still the owner of the frame, so we can free the page
        palloc_free_page(frame->physical_mem_frame_base);
        frame->resident_page = NULL;
    }
    //If we are not the current owner, than some other thread swooped in and is using
    //the page, so we do not want to free it, thus do nothing.
    lock_release(&frame->frame_lock);
    return true;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
bool aquire_frame_lock(struct frame* frame, struct spte* page_trying_to_pin) {
    
    lock_acquire(&frame->frame_lock);
    if (frame->resident_page == page_trying_to_pin) {
        return true;
    }
    lock_release(&frame->frame_lock);
    return false;
}



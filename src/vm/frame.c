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
void init_frame_table() {
    list_init(&list_of_frames);
    lock_init(&frame_list_lock);
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

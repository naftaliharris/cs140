#ifndef __VM_FRAME_H
#define __VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include "threads/palloc.h"
#include <threads/synch.h>

bool frame_handler_init(size_t num_frames, uint8_t* frame_base);

typedef bool create_page_func (void* kaddr, void* aux);
bool frame_handler_create_user_page(void* virtaddr, bool writeable, bool zeroed, create_page_func* func, void* aux);

bool frame_handler_free_page(void* kaddr, void* uaddr, struct thread* owner);

#endif /* __VM_FRAME_H */
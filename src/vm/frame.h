#ifndef __VM_FRAME_H
#define __VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include "threads/thread.h"

bool frame_handler_init(size_t num_frames, uint8_t* frame_base);

void* frame_handler_create_user_page(void* virtaddr, bool writeable, bool zeroed);

bool frame_handler_free_page(void* kaddr, void* uaddr, struct thread* owner);

#endif /* __VM_FRAME_H */
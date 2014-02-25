#ifndef __VM_FRAME_H
#define __VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include "threads/thread.h"

struct frame {
  struct thread* owner_thread; // if NULL, no page mapped
  void* vaddr; // user virtual address, for use when referencing page table, and perhaps for when converting between kaddr/uaddr
  void* kaddr;
  struct lock lock;
};

bool frame_handler_init(size_t num_frames, uint8_t* frame_base);

typedef bool create_page_func (void* kaddr, void* aux);
bool frame_handler_create_user_page(void* virtaddr, bool writeable, bool zeroed, create_page_func* func, void* aux);

bool frame_handler_free_page(void* kaddr, void* uaddr, struct thread* owner);

#endif /* __VM_FRAME_H */
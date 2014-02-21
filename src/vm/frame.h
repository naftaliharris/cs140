#ifndef __VM_FRAME_H
#define __VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include "threads/palloc.h"
#include <threads/synch.h>

struct {
  struct list_elem elem;
  struct thread* owner_thread;
  void* kaddr; // kernel virtual address
  void* vaddr; // user virtual address, for use when referencing page table, and perhaps for when converting between kaddr/uaddr
  struct lock frame_lock;
} frame;

typedef bool create_page_func (void* kaddr, void* aux);
bool frame_handler_create_user_page(void* virtaddr, bool writeable, bool zeroed, *create_page_func func, void* aux);

bool frame_handler_free_page(struct frame* frame);

#endif /* __VM_FRAME_H */
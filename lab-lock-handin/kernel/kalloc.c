// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

int get_cpuid()
{
  push_off();
  int id = cpuid();
  pop_off();
  return id;
}

void
kinit()
{
  for(int i=0; i<NCPU; i++)
  {
    char name[10] = "kmem";
    name[4] = i - '0';
    name[5] = '\0';
    initlock(&kmems[i].lock, name);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  int id = get_cpuid();

  acquire(&kmems[id].lock);
  r->next = kmems[id].freelist;
  kmems[id].freelist = r;
  release(&kmems[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
//kalloc
  int id = get_cpuid();

  acquire(&kmems[id].lock);
  r = kmems[id].freelist;
  if(r)
    kmems[id].freelist = r->next;
  release(&kmems[id].lock);

  int flag = 0;

  if(!r)//freelist of current cpu is empty, need to STEAL!
  {
    for(int i=0; i<NCPU; i++)
    {
      if(i == id)
        continue;
      if(kmems[i].freelist)
      {
        acquire(&kmems[i].lock);
        r = kmems[i].freelist;
        kmems[i].freelist = r->next;
        release(&kmems[i].lock);

        acquire(&kmems[id].lock);
        r->next = kmems[id].freelist;
        kmems[id].freelist = r;
        release(&kmems[id].lock);
        flag = 1;
        break;
      }
    }
  }

  if(flag)//push front
  {
    acquire(&kmems[id].lock);
    r = kmems[id].freelist;
    if(r)
      kmems[id].freelist = r->next;
    release(&kmems[id].lock);
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

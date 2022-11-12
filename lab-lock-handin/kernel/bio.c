// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

struct {
  //struct spinlock lock;
  struct buf buf[NBUF];

  struct buf bucket[NBUCKET];
  struct spinlock bucket_lock[NBUCKET];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
} bcache;

int getHash(uint i)
{
  return (i % NBUCKET);
}

void
binit(void)
{
  //printf("binit\n");
  struct buf *b;

  //initlock(&bcache.lock, "bcache");
  for(int i=0; i<NBUCKET; i++)
  {
    char name[10] = "bcache";
    name[6] = i - '0';
    name[7] = '\0';
    initlock(&bcache.bucket_lock[i], name);
    bcache.bucket[i].prev = &bcache.bucket[i];
    bcache.bucket[i].next = &bcache.bucket[i];
  }

  // Create linked list of buffers
  //bcache.head.prev = &bcache.head;
  //bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int hash = getHash(b->blockno);
    b->next = bcache.bucket[hash].next;
    b->prev = &bcache.bucket[hash];
    initsleeplock(&b->lock, "buffer");
    bcache.bucket[hash].next->prev = b;
    bcache.bucket[hash].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  //printf("bget\n");
  struct buf *b;
  int hash = getHash(blockno);
  acquire(&bcache.bucket_lock[hash]);

  // Is the block already cached?
  for(b = bcache.bucket[hash].next; b != &bcache.bucket[hash]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      //b->time_stamp = ticks;
      b->refcnt++;
      release(&bcache.bucket_lock[hash]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //int flag = 0;
  for(int i=getHash(blockno+1); i != hash; i = getHash(i+1))
  {
    if(i == hash)
      continue;
    acquire(&bcache.bucket_lock[i]);
    for(b = bcache.bucket[i].prev; b != &bcache.bucket[i]; b = b->prev)//search from back to front
    {
      if(b->refcnt == 0){ //not currently used
        //b->time_stamp = ticks;
        b->dev = dev;
        b->blockno = blockno;
        b->refcnt++;
        b->valid = 0; //for bread to check

        b->prev->next = b->next;
        b->next->prev = b->prev;

        b->next = bcache.bucket[hash].next;
        b->prev = &bcache.bucket[hash];
        bcache.bucket[hash].next->prev = b;
        bcache.bucket[hash].next = b;

        release(&bcache.bucket_lock[hash]);
        release(&bcache.bucket_lock[i]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.bucket_lock[i]);
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  //printf("bread\n");
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  //printf("bwrite\n");
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  //printf("brelse\n");
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int hash = getHash(b->blockno);
  acquire(&bcache.bucket_lock[hash]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.bucket[hash].next;
    b->prev = &bcache.bucket[hash];
    bcache.bucket[hash].next->prev = b;
    bcache.bucket[hash].next = b;
  }
  
  release(&bcache.bucket_lock[hash]);
}

void
bpin(struct buf *b) {
  //printf("bpin\n");
  int hash = getHash(b->blockno);
  acquire(&bcache.bucket_lock[hash]);
  b->refcnt++;
  release(&bcache.bucket_lock[hash]);
}

void
bunpin(struct buf *b) {
  //printf("bunpin\n");
  int hash = getHash(b->blockno);
  acquire(&bcache.bucket_lock[hash]);
  b->refcnt--;
  release(&bcache.bucket_lock[hash]);
}



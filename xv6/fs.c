// File system implementation.  Four layers:
//   + Blocks: allocator for raw disk blocks.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// Disk layout is: superblock, inodes, block in-use bitmap, data blocks.
//
// This file contains the low-level file system manipulation 
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "buf.h"
#include "fs.h"
#include "fsvar.h"
#include "dev.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);

struct buflist {
  struct buf* blist[NBUF];
};

struct cylinderstats{
  uint usedinodes;
  uint dircount;
};

struct cylinderstats cylstats[32]; //ugly, but can be done away with later.....
uint   cgcount = 0;


//returns count of bytes needed to allocate a buffer.
uint getallocsize(uint buffersize)
{
  uint pagesneeded = buffersize / PAGE; //in param.h
  if ((buffersize % PAGE) > 0)  pagesneeded++;
  return pagesneeded * PAGE;
}

void printsector(unsigned char *data)
{
    int i = 0;
    for (i = 0; i < 16; i++)
    {
		int j = 0;
		for ( j = 0; j < 32; j++)
		{
			cprintf("%x ", data[i*32+j]);
		}
		cprintf("\n");
    }

};

void fsbread(uint dev, uint block, uchar *buffer, uint blocksize, struct buflist *lockedlist)
{
  //sectors per block. May be 1 to 1. blocksize should always be power of 2
  //cprintf("cp:%d read start block: %d\n", cp->pid, block);
  uint secperblock = blocksize / DISK_SECTOR_SIZE;
  uint firstsector = block * secperblock;
  //loop and bread it.
 // cprintf("Reading block: %d, which starts at sector: %d and is %d sectors big\n", block, firstsector, secperblock);
  int i = 0;
  for (i = 0; i < secperblock; i++)
  {
    struct buf *currentbuf= bread(dev, firstsector + i);
    lockedlist->blist[i] = currentbuf;
    memmove(buffer+(i*DISK_SECTOR_SIZE), currentbuf->data, DISK_SECTOR_SIZE);
  };
//  cprintf("\t\tcp:%d read stop block: %d\n", cp->pid, block);
}

void
fsbwrite(uchar *buffer, uint blocksize, struct buflist *buffers)
{
  //cprintf("cp:%d write start block: %d\n", cp->pid, blocksize/buffers->blist[0]->sector);
  int secperblock = blocksize / DISK_SECTOR_SIZE;
  int i = 0;
  for (i = 0; i < secperblock; i++)
  {
     memmove(buffers->blist[i]->data, buffer+(i*DISK_SECTOR_SIZE), DISK_SECTOR_SIZE);
     bwrite(buffers->blist[i]);
  }
 //cprintf("\t\tcp:%d write stop\n", cp->pid);
}

void fsbrelease(uchar *buffer, struct buflist *buffers, uint blocksize)
{
  //cprintf("\t\tcp:%d letgo start sector: %d\n", cp->pid, buffers->blist[0]->sector);  
  int secperblock = blocksize / DISK_SECTOR_SIZE;
  int i = 0;
  //release in reverse order, in case of deadlock
  for (i = secperblock - 1; i >= 0; i--)
  {
    brelse(buffers->blist[i]);
  }
//  cprintf("\t\tcp:%d letgo end\n", cp->pid);
}

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
 /* struct buf *bp;
  
  bp = bread(dev, 1);*/  
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;
  fsbread(dev, 1, buffer, BSIZE, &bl);
  //memmove(sb, bp->data, sizeof(*sb));
  memmove(sb, buffer, sizeof(struct superblock));
  //brelse(bp);
  fsbrelease(buffer, &bl, BSIZE);
  kfree((char*)buffer, getallocsize(BSIZE));  
}


void fs_init()
{
  int inum;
  struct dinode *ip;
  struct superblock sb;
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;

  readsb(ROOTDEV, &sb);
  cgcount = sb.nblocks / CGSIZE + (((sb.nblocks % CGSIZE) > 0) ? 1 : 0);

  for(inum = 0; inum < sb.ninodes; inum += IPB){
    fsbread(ROOTDEV, IBLOCK(inum), buffer, BSIZE, &bl);
    int inumblock;
    for(inumblock = 0; inumblock < IPB; inumblock++)
    {
      ip = (struct dinode*)buffer + inumblock;
      if(ip->type != 0){  // a free inode
        int cg = inum / IPCG;
        cylstats[cg].usedinodes++;
        if (ip->type == T_DIR)
          cylstats[cg].dircount++;
      };    
    }
    fsbrelease(buffer, &bl, BSIZE);
  }
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  /*struct buf *bp;
  
  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  bwrite(bp);
  brelse(bp);*/

  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;
  fsbread(dev, bno, buffer, BSIZE, &bl);
  memset(buffer, 0, BSIZE);
  fsbwrite(buffer, BSIZE, &bl);
  fsbrelease(buffer, &bl, BSIZE);
  kfree((char*)buffer, getallocsize(BSIZE));
}

// Blocks. 

// Allocate a disk block.
static uint
balloc(uint dev)
{
/*  int b, bi, m;
  struct buf *bp;
  struct superblock sb;

  bp = 0;
  readsb(dev, &sb);
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb.ninodes));
    for(bi = 0; bi < BPB; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use on disk.
        bwrite(bp);
        brelse(bp);
        return b + bi;
      }
    }
    brelse(bp);
  }*/
  int b, bi, m;
  struct superblock sb;
  struct buflist bl;
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));

  readsb(dev, &sb);
  for( b = 0; b < sb.size; b+= BPB)
  {
    fsbread(dev, BBLOCK(b), buffer, BSIZE, &bl);
    for( bi = 0; bi < BPB; bi++)
    {
      m = 1 << (bi % 8);          //calculate offset bit for free map
      if ((buffer[bi/8] & m) == 0) //Is block free?
      {
        buffer[bi/8] |= m; //Mark block in use on disk.
        fsbwrite(buffer, BSIZE, &bl);
        fsbrelease(buffer, &bl, BSIZE);
        kfree((char*)buffer, getallocsize(BSIZE));
        return b + bi;
      }
    }
    fsbrelease(buffer, &bl, BSIZE);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
 /* struct buf *bp;
  struct superblock sb;
  int bi, m;

  bzero(dev, b);

  readsb(dev, &sb);
  bp = bread(dev, BBLOCK(b, sb.ninodes));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;  // Mark block free on disk.
  bwrite(bp);
  brelse(bp);
*/
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;
  struct superblock sb;
  int bi, m;
  bzero(dev, b);
  readsb(dev, &sb);
  fsbread(dev, BBLOCK(b), buffer, BSIZE, &bl);
  bi = b % BPB;
  m = 1 << (bi % 8);
  if ((buffer[bi/8] & m) == 0)
    panic("freeing free block");
  buffer[bi/8] &= ~m;
  fsbwrite(buffer, BSIZE, &bl);
  fsbrelease(buffer, &bl, BSIZE);
  kfree((char*)buffer, getallocsize(BSIZE));
}

// Inodes.
//
// An inode is a single, unnamed file in the file system.
// The inode disk structure holds metadata (the type, device numbers,
// and data size) along with a list of blocks where the associated
// data can be found.
//
// The inodes are laid out sequentially on disk immediately after
// the superblock.  The kernel keeps a cache of the in-use
// on-disk structures to provide a place for synchronizing access
// to inodes shared between multiple processes.
// 
// ip->ref counts the number of pointer references to this cached
// inode; references are typically kept in struct file and in cp->cwd.
// When ip->ref falls to zero, the inode is no longer cached.
// It is an error to use an inode without holding a reference to it.
//
// Processes are only allowed to read and write inode
// metadata and contents when holding the inode's lock,
// represented by the I_BUSY flag in the in-memory copy.
// Because inode locks are held during disk accesses, 
// they are implemented using a flag rather than with
// spin locks.  Callers are responsible for locking
// inodes before passing them to routines in this file; leaving
// this responsibility with the caller makes it possible for them
// to create arbitrarily-sized atomic operations.
//
// To give maximum control over locking to the callers, 
// the routines in this file that return inode pointers 
// return pointers to *unlocked* inodes.  It is the callers'
// responsibility to lock them before using them.  A non-zero
// ip->ref keeps these unlocked inodes in the cache.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit(void)
{
  initlock(&icache.lock, "icache.lock");
}

// Find the inode with number inum on device dev
// and return the in-memory copy.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Try for cached inode.
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Allocate fresh inode.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->flags = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
void
ilock(struct inode *ip)
{
  //struct buf *bp;
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;
  struct dinode *dip;
  
  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquire(&icache.lock);
  while(ip->flags & I_BUSY)
    sleep(ip, &icache.lock);
  ip->flags |= I_BUSY;
  release(&icache.lock);

  if(!(ip->flags & I_VALID)){
    //bp = bread(ip->dev, IBLOCK(ip->inum));
    //dip = (struct dinode*)bp->data + ip->inum%IPB;
    fsbread(ip->dev, IBLOCK(ip->inum), buffer, BSIZE, &bl);    

    dip = (struct dinode*)buffer + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    //brelse(bp);
    fsbrelease(buffer, &bl, BSIZE);
    ip->flags |= I_VALID;
    if(ip->type == 0)
    {
      panic("ilock: no type");
    }
  }
  kfree((char*)buffer, getallocsize(BSIZE));
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !(ip->flags & I_BUSY) || ip->ref < 1)
    panic("iunlock");

  acquire(&icache.lock);
  ip->flags &= ~I_BUSY;
  wakeup(ip);
  release(&icache.lock);
}

// Caller holds reference to unlocked ip.  Drop reference.
void
iput(struct inode *ip)
{
  acquire(&icache.lock);
  if(ip->ref == 1 && (ip->flags & I_VALID) && ip->nlink == 0){
    // inode is no longer used: truncate and free inode.
    if(ip->flags & I_BUSY)
      panic("iput busy");
    ip->flags |= I_BUSY;
    release(&icache.lock);
    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    acquire(&icache.lock);
    ip->flags &= ~I_BUSY;
    wakeup(ip);
  }
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Allocate a new inode with the given type on device dev.
struct inode*
ialloc(uint dev, short type, uint parent) //TOM - UPDATE here.
{
  int inum;
  if (cgcount == 0) fs_init();
  //struct buf *bp;
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;
  struct dinode *dip;
  struct superblock sb;

  readsb(dev, &sb);
  // cprintf("Superblock inodes: %d\n", sb.ninodes); //DEBUG
  uint bestgroup = 0;
  if (type == T_DIR)
  {
   //select an inode from the "best cylinder group" based on average number of free inodes
   int i = 0;
   int totalused = 0;
   int lowestdir = -1;
   for (i = 0; i < cgcount; i++)
   {
      totalused += cylstats[i].usedinodes;
   };
   float averageusedinodes = totalused / cgcount;
   for (i = 0; i < cgcount; i++)
   {
      if (cylstats[i].usedinodes < averageusedinodes)
      {         
         if ((lowestdir == -1) || (cylstats[i].dircount < cylstats[lowestdir].dircount)) lowestdir = i;
      }
   };
   if (lowestdir == -1) lowestdir = 0;
   bestgroup = lowestdir;
  }
  else //type is not T_DIR
  {
     bestgroup = parent/IPCG;
  }
  int start = bestgroup * IPCG;
  if (start == 0) start++;
  for(inum = start; inum < sb.ninodes; inum++){  // loop over inode, prefering our local group.
    //bp = bread(dev, IBLOCK(inum));
    //dip = (struct dinode*)bp->data + inum%IPB;
    fsbread(dev, IBLOCK(inum), buffer, BSIZE, &bl);
    dip = (struct dinode*)buffer + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;      
      //bwrite(bp);   // mark it allocated on the disk
      //brelse(bp);
      fsbwrite(buffer, BSIZE, &bl);
      fsbrelease(buffer, &bl, BSIZE);
      kfree((char*)buffer, getallocsize(BSIZE));
      if (type == T_DIR) //update stats
      { 
          cylstats[inum/IPCG].dircount++;
      }
      cylstats[inum/IPCG].usedinodes++;
      return iget(dev, inum);
    }
    //brelse(bp);
    fsbrelease(buffer, &bl, BSIZE);
  }
  //backup plan. check everything else.
  for(inum = 1; inum < start; inum++){  
    //bp = bread(dev, IBLOCK(inum));
    //dip = (struct dinode*)bp->data + inum%IPB;
    fsbread(dev, IBLOCK(inum), buffer, BSIZE, &bl);
    dip = (struct dinode*)buffer + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      //bwrite(bp);   // mark it allocated on the disk
      //brelse(bp);
      fsbwrite(buffer, BSIZE, &bl);
      fsbrelease(buffer, &bl, BSIZE);
      kfree((char*)buffer, getallocsize(BSIZE));
      if (type == T_DIR)
      {
          cylstats[inum/IPCG].dircount++;
      }
      cylstats[inum/IPCG].usedinodes++;
      return iget(dev, inum);
    }
    //brelse(bp);
    fsbrelease(buffer, &bl, BSIZE);
  }
  
  panic("ialloc: no inodes");
}

// Copy inode, which has changed, from memory to disk.
void
iupdate(struct inode *ip)
{
  //struct buf *bp;
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;
  struct dinode *dip;

  //bp = bread(ip->dev, IBLOCK(ip->inum));
  fsbread(ip->dev, IBLOCK(ip->inum), buffer, BSIZE, &bl);
  //dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip = (struct dinode*)buffer+ ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  //bwrite(bp);
  //brelse(bp);
  fsbwrite(buffer, BSIZE, &bl);
  fsbrelease(buffer, &bl, BSIZE);
  kfree((char*)buffer, getallocsize(BSIZE));
}

// Inode contents
//
// The contents (data) associated with each inode is stored
// in a sequence of blocks on the disk.  The first NDIRECT blocks
// are listed in ip->addrs[].  The next NINDIRECT blocks are 
// listed in the block ip->addrs[INDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, alloc controls whether one is allocated.
static uint
bmap(struct inode *ip, uint bn, int alloc)
{
  uint addr, *a;
  //struct buf *bp;
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      if(!alloc){
        kfree((char*)buffer, getallocsize(BSIZE));
        return -1;
      }
      ip->addrs[bn] = addr = balloc(ip->dev);
    }
    kfree((char*)buffer, getallocsize(BSIZE));
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[INDIRECT]) == 0){
      if(!alloc){
        kfree((char*)buffer, getallocsize(BSIZE));
        return -1;
      }
      ip->addrs[INDIRECT] = addr = balloc(ip->dev);
    }
    //bp = bread(ip->dev, addr);
    fsbread(ip->dev, addr, buffer, BSIZE, &bl);
    //a = (uint*)bp->data;
    a = (uint*)buffer;
  
    if((addr = a[bn]) == 0){
      if(!alloc){
        //brelse(bp);
        fsbrelease(buffer, &bl, BSIZE);
        kfree((char*)buffer, getallocsize(BSIZE));
        return -1;
      }
      a[bn] = addr = balloc(ip->dev);
      //bwrite(bp);
      fsbwrite(buffer, BSIZE, &bl);
    }
    //brelse(bp);
    fsbrelease(buffer, &bl, BSIZE);
    kfree((char*)buffer, getallocsize(BSIZE));
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
static void
itrunc(struct inode *ip)
{
  int i, j;
  //struct buf *bp;
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }
  
  if(ip->addrs[INDIRECT]){
    fsbread(ip->dev, ip->addrs[INDIRECT], buffer, BSIZE, &bl);
    //bp = bread(ip->dev, ip->addrs[INDIRECT]);
    //a = (uint*)bp->data;
    a = (uint*)buffer;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    //brelse(bp);
    fsbrelease(buffer, &bl, BSIZE);
    ip->addrs[INDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
  kfree((char*)buffer, getallocsize(BSIZE));
}

// Copy stat information from inode.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// Read data from inode.
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read){
      kfree((char*)buffer, getallocsize(BSIZE));
      return -1;
    }
    kfree((char*)buffer, getallocsize(BSIZE));
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off){
    kfree((char*)buffer, getallocsize(BSIZE));
    return -1;
  }
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    //bp = bread(ip->dev, bmap(ip, off/BSIZE, 0));
    fsbread(ip->dev, bmap(ip, off/BSIZE, 0), buffer, BSIZE, &bl);
    m = min(n - tot, BSIZE - off%BSIZE);
    //memmove(dst, bp->data + off%BSIZE, m);
    memmove(dst, buffer + off%BSIZE, m);
    //brelse(bp);
    fsbrelease(buffer, &bl, BSIZE);
  }
  kfree((char*)buffer, getallocsize(BSIZE));
  return n;
}

// Write data to inode.
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  //struct buf *bp;
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write){
      kfree((char*)buffer, getallocsize(BSIZE));
      return -1;
    }
    kfree((char*)buffer, getallocsize(BSIZE));
    return devsw[ip->major].write(ip, src, n);
  }

  if(off + n < off){
    kfree((char*)buffer, getallocsize(BSIZE));
    return -1;
  }
  if(off + n > MAXFILE*BSIZE)
    n = MAXFILE*BSIZE - off;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    //bp = bread(ip->dev, bmap(ip, off/BSIZE, 1));
    fsbread(ip->dev, bmap(ip, off/BSIZE, 1), buffer, BSIZE, &bl);
    m = min(n - tot, BSIZE - off%BSIZE);
   // memmove(bp->data + off%BSIZE, src, m);
    memmove(buffer + off%BSIZE, src, m);
    //bwrite(bp);
    //brelse(bp);
    fsbwrite(buffer, BSIZE, &bl);
    fsbrelease(buffer, &bl, BSIZE);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  kfree((char*)buffer, getallocsize(BSIZE));
  return n;
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
// Caller must have already locked dp.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  //struct buf *bp;
  uchar *buffer = (uchar*)kalloc(getallocsize(BSIZE));
  struct buflist bl;

  struct dirent *de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += BSIZE){
    //bp = bread(dp->dev, bmap(dp, off / BSIZE, 0));
    fsbread(dp->dev, bmap(dp, off/BSIZE, 0), buffer, BSIZE, &bl);
    //for(de = (struct dirent*)bp->data;
    //    de < (struct dirent*)(bp->data + BSIZE);
    //    de++){
    for(de = (struct dirent*)buffer;
        de < (struct dirent*)(buffer+BSIZE);
        de++){
      if(de->inum == 0)
        continue;
      if(namecmp(name, de->name) == 0){
        // entry matches path element
        if(poff)
          *poff = off + (uchar*)de - buffer; //bp->data;
        inum = de->inum;
        //brelse(bp);
        fsbrelease(buffer, &bl, BSIZE);
        kfree((char*)buffer, getallocsize(BSIZE));
        return iget(dp->dev, inum);
      }
    }
    //brelse(bp);
    fsbrelease(buffer, &bl, BSIZE);
  }
  kfree((char*)buffer, getallocsize(BSIZE));
  return 0;
}

// Write a new directory entry (name, ino) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint ino)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = ino;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");
  
  return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
static struct inode*
_namei(char *path, int parent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, 1);
  else
    ip = idup(cp->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(parent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(parent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return _namei(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return _namei(path, 1, name);
}

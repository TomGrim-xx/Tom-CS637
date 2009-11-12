#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include "types.h"
#include "fs.h"

int nblocks = 995;   //number of data blocks
int ninodes = 200;   //number of inodes
int size = 1024;  //number of blocks on disk
int blocksize = 512; //bytes per block

int fsfd;
struct superblock sb;   // superblock
char * zeroes;
uint freeblock;   //next available block for data
uint usedblocks;  //total used blocks
uint bitblocks;   //number of bitmap blocks
uint freeinode = 1;  

void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);

// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*) &y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*) &y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[blocksize];
  struct dinode din;

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img blocks files...\n");
    exit(1);
  }

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[1]);
    exit(1);
  }

  blocksize = atoi(argv[2]);

  zeroes = malloc(blocksize);

  //A block is evenly divided into inodes or directory entries.
  assert((blocksize % sizeof(struct dinode)) == 0);
  assert((blocksize % sizeof(struct dirent)) == 0);

  size = atoi(argv[3]);
  
  bitblocks = size / BPB + 1; 
  usedblocks = ninodes / IPB + 3 + bitblocks;
  freeblock = usedblocks;

  //remaining blocks are data blocks
  nblocks = size - usedblocks;

  sb.size = xint(size);
  sb.nblocks = xint(nblocks); // so whole disk is size sectors
  sb.ninodes = xint(ninodes);

    printf("used %d (bit %d ninode %lu) free %u total %d\n", usedblocks,
         bitblocks, ninodes/IPB + 1, freeblock, nblocks+usedblocks);

  //data + metadata = whole disk
  assert(nblocks + usedblocks == size);

  //fill drive with zeros
  for(i = 0; i < nblocks + usedblocks; i++)
    wsect(i, zeroes);
 
  //write superblock to block 1
  char sbbuffer[blocksize];
  for (i = 0; i < blocksize; i++) sbbuffer[i] = 0;
  memcpy(&sbbuffer, &sb, sizeof(uint)*3);

  wsect(1, &sbbuffer);
  
  //write root directory inode
  rootino = ialloc(T_DIR);
  assert(rootino == 1);

  //add . and .. entries
  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  for(i = 4; i < argc; i++){
    assert(index(argv[i], '/') == 0);

    if((fd = open(argv[i], 0)) < 0){
      perror(argv[i]);
      exit(1);
    }
    
    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(argv[i][0] == '_')
      ++argv[i];

    //allocate a new inode for the file
    inum = ialloc(T_FILE);

    //add an entry to the root directory for the file
    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, argv[i], DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    //write file data to disk
    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/blocksize) + 1) * blocksize;
  din.size = xint(off);
  winode(rootino, &din);

  balloc(usedblocks);

  exit(0);
}

void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * (long)blocksize, 0) != sec * (long)blocksize){
    perror("lseek");
    exit(1);
  }
  uint byteswritten = write(fsfd, buf, blocksize);

  //if(write(fsfd, buf, blocksize) != blocksize){
//  printf("Bytes written: %d to sector %d\n", byteswritten, sec);
  if (byteswritten != blocksize){
    perror("write");
    exit(1);
  }
}

uint
i2b(uint inum)
{
  return (inum / IPB) + 2;
}

//write inode
void
winode(uint inum, struct dinode *ip)
{
  char buf[blocksize];
  uint bn;
  struct dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((struct dinode*) buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

//read inode
void
rinode(uint inum, struct dinode *ip)
{
  char buf[blocksize];
  uint bn;
  struct dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((struct dinode*) buf) + (inum % IPB);
  *ip = *dip;
}

//read block
void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * (long)blocksize, 0) != sec * (long)blocksize){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, blocksize) != blocksize){
    perror("read");
    exit(1);
  }
}

//initialize and write next available inode, return inode number
uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

//Write bitmap block
void
balloc(int used)
{
  uchar buf[blocksize];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < bitblocks * BPB);   //do not exceed space for bitmap
  bzero(buf, blocksize);
  for(i = 0; i < used; i++) {
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %lu\n", ninodes/IPB + 3);
  wsect(ninodes / IPB + 3, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

//append data to data blocks for inode inum
void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*) xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[blocksize];
  uint indirect[NINDIRECT];
  uint x;

  rinode(inum, &din);

  off = xint(din.size); //byte offset within file (start at the end)
  while(n > 0){
    fbn = off / blocksize;  //block offset within file
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT) {
      if(xint(din.addrs[fbn]) == 0) {
        din.addrs[fbn] = xint(freeblock++);
        usedblocks++;
      }
      x = xint(din.addrs[fbn]);
    } else {
      if(xint(din.addrs[INDIRECT]) == 0) {
        // printf("allocate indirect block\n");
        din.addrs[INDIRECT] = xint(freeblock++);
        usedblocks++;
      }
      // printf("read indirect block\n");
      rsect(xint(din.addrs[INDIRECT]), (char*) indirect);
      if(indirect[fbn - NDIRECT] == 0) {
        indirect[fbn - NDIRECT] = xint(freeblock++);
        usedblocks++;
        wsect(xint(din.addrs[INDIRECT]), (char*) indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * blocksize - off);
    assert(x < size);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * blocksize), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}

// On-disk file system format. 
// Both the kernel and user programs use this header file.

// Block 0 is unused.
// Block 1 is super block.
// Inodes start at block 2.

#define BSIZE 1024  // block size

// File system super block
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint blocksize;    // Bytes per block.
};

#define NDIRECT 12
#define NADDRS (NDIRECT+1)
#define INDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT  + NINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NADDRS];   // Data block addresses
};

#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Special device


// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Bitmap bits per block
#define BPB           (BSIZE*8)

//Cylinder Group Size (in blocks)
#define CGSIZE (BPB)

//Inodes per Cylinder Group
#define IPCG (CGSIZE * BSIZE / 2048)

//Inode blocks per Cylinder Group
#define IBPCG (IPCG/IPB)


// Block containing inode i - find the correct cylinder group and the offset into it.
#define IBLOCK(i) ( (i/IPCG)*CGSIZE + 2 + (i % IPCG)/IPB)

// Block containing bit for block b
//#define BBLOCK(b, ninodes) (b/BPB + (ninodes)/IPB + 3)
//Get the correct cylinder group, then offset in it.
#define BBLOCK(b) ((b/CGSIZE)*CGSIZE + IBPCG+2) //superblock offset, plus the first


// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};


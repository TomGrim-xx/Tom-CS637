#include "types.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"
#include "memory.h"

int
exec(char *path, char **argv)
{
  char *mem, *s, *last;
  int i, argc, arglen, len, off;
  uint sz, sp, argp;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;

  if((ip = namei(path)) == 0)
    return -1;
  ilock(ip);

  // Compute memory size of new process.
  mem = 0;
  sz = 0;

  // Program segments.
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) < sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    sz += ph.memsz;
  }
  
  // Arguments.
  arglen = 0;
  for(argc=0; argv[argc]; argc++)
    arglen += strlen(argv[argc]) + 1;
  arglen = (arglen+3) & ~3;
  sz += arglen + 4*(argc+1);

  // Stack.
  sz += PAGE;
  
  // Allocate program memory.
  sz = (sz+PAGE-1) & ~(PAGE-1);
  mem = kalloc(sz);
  if(mem == 0)
    goto bad;
  memset(mem, 0, sz);

  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.va + ph.memsz > sz)
      goto bad;
    if(readi(ip, mem + ph.va, ph.offset, ph.filesz) != ph.filesz)
      goto bad;
    memset(mem + ph.va + ph.filesz, 0, ph.memsz - ph.filesz);
  }
  iunlockput(ip);
  
  // Initialize stack.
  sp = sz;
  argp = sz - arglen - 4*(argc+1);

  // Copy argv strings and pointers to stack.
  *(uint*)(mem+argp + 4*argc) = 0;  // argv[argc]
  for(i=argc-1; i>=0; i--){
    len = strlen(argv[i]) + 1;
    sp -= len;
    memmove(mem+sp, argv[i], len);
    *(uint*)(mem+argp + 4*i) = sp;  // argv[i]
  }

  // Stack frame for main(argc, argv), below arguments.
  sp = argp;
  sp -= 4;
  *(uint*)(mem+sp) = argp;
  sp -= 4;
  *(uint*)(mem+sp) = argc;
  sp -= 4;
  *(uint*)(mem+sp) = 0xffffffff;   // fake return pc

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(cp->name, last, sizeof(cp->name));

  // Commit to the new image.
  kfree(cp->mem, cp->sz);
  cp->mem = mem;
  cp->sz = sz;
  cp->tf->eip = elf.entry;  // main
  cp->tf->esp = sp;
  cp->page_dir = BAD_PAGE_DIR;
  setupsegs(cp);
  return 0;

 bad:
  if(mem)
    kfree(mem, sz);
  iunlockput(ip);
  return -1;
}


int
exec_page(char *path, char **argv)
{
  char *mem, *s, *last;
  struct page_directory *page_dir;
  struct page_table *page_tab;
  int i, argc, arglen, len, off;
  uint sz, sp, argp;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;

  if((ip = namei(path)) == 0)
    return -1;
  ilock(ip);

  // Compute memory size of new process.
  mem = 0;
  sz = 0;

  // Program segments.
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) < sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    sz += ph.memsz;
  }

  // Arguments.
  arglen = 0;
  for(argc=0; argv[argc]; argc++)
    arglen += strlen(argv[argc]) + 1;
  arglen = (arglen+3) & ~3;
  sz += arglen + 4*(argc+1);

  // Stack.
  sz += PAGE;

  // Allocate program memory.
  sz = (sz+PAGE-1) & ~(PAGE-1);
  mem = kalloc(sz);
  if(mem == 0)
    goto bad;
  memset(mem, 0, sz);

  //allocate page dir entry.
  page_dir = (struct page_directory*) kalloc(PAGE); 
  for (i = 0; i < 1024; i++)
  {     
     page_dir->page_tables[i].present = 0;
  };


  // this should fill the page table with 1-1 virtual to physical addresses
  // up to 4 MB (1024 pages)
  
  uint page_dir_index = 0;

  page_dir->page_tables[0].present = 1;
  page_dir->page_tables[0].readwenable = 1;
  page_dir->page_tables[0].user = 1;
  page_dir->page_tables[0].page_table_ptr = (uint)kernel_memory >> 12;
  cprintf("Kernel page table: %d\n", page_dir->page_tables[0].page_table_ptr);
  /*
  struct page_table * ptable = (struct page_table *) kalloc(PAGE);
  for (i=0; i<1024; i++) {
    ptable->pages[i].physical_page_addr = (page_dir_index << 10) + i;
    //cprintf("page addr %d \n", ptable->pages[i].physical_page_addr);
    ptable->pages[i].present = 1;
    ptable->pages[i].readwenable = 1;
    ptable->pages[i].user = 0;
  }
*/



  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.va + ph.memsz > sz)
      goto bad;
    if(readi(ip, mem + ph.va, ph.offset, ph.filesz) != ph.filesz)
      goto bad;
    memset(mem + ph.va + ph.filesz, 0, ph.memsz - ph.filesz);
  }
  iunlockput(ip);

  // Initialize stack.
  sp = sz;
  argp = sz - arglen - 4*(argc+1);

  // Copy argv strings and pointers to stack.
  *(uint*)(mem+argp + 4*argc) = 0;  // argv[argc]
  for(i=argc-1; i>=0; i--){
    len = strlen(argv[i]) + 1;
    sp -= len;
    memmove(mem+sp, argv[i], len);
    *(uint*)(mem+argp + 4*i) = sp;  // argv[i]
  }

  // Stack frame for main(argc, argv), below arguments.
  sp = argp;
  sp -= 4;
  *(uint*)(mem+sp) = argp;
  sp -= 4;
  *(uint*)(mem+sp) = argc;
  sp -= 4;
  *(uint*)(mem+sp) = 0xffffffff;   // fake return pc

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(cp->name, last, sizeof(cp->name));

  // Commit to the new image.
  kfree(cp->mem, cp->sz);
  cp->mem = mem;
  page_tab =(struct page_table*)kalloc(PAGE);
  for (i = 0; i < 1024; i++)
  {
    if (i < (sz / PAGE))
    {
      page_tab->page[i].present = 1;
      page_tab->page[i].readwenable = 1;
      page_tab->page[i].user = 1;
      page_tab->page[i].physical_page_addr = ((uint)mem + (i * PAGE)) >> 12;
      cprintf("Page Address = %d\n", mem);
      cprintf("Page # = %d\n", page_tab->page[i].physical_page_addr);
    }
    else page_tab->page[i].present = 0;
  }

  page_dir->page_tables[1].page_table_ptr = (uint) page_tab >> 12;
  

  cp->sz = sz;
  cp->tf->eip = elf.entry;  // main
  cp->tf->esp = sp;
  cp->page_dir = page_dir;
  //setupsegs(cp);
 // setuppages(cp);
  return 0;

 bad:
  if(mem)
    kfree(mem, sz);
  if(page_dir)
    kfree(page_dir, PAGE);
  if(page_tab)
    kfree(page_tab, PAGE);
  iunlockput(ip);
  return -1;




}

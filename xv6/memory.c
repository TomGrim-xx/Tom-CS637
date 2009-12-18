#include "param.h"
#include "types.h"
#include "defs.h"
#include "mmu.h"
#include "memory.h"


//first 4 MB of virtual address space reserved for kernel
//setup 1 pagetable for this
void
meminit() {
   // this should fill the page table with 1-1 virtual to physical addresses
   // up kernel size
   uint page_dir_index = 0;

   kernel_memory = (struct page_table *) kalloc(PAGE);

   kernel_memory_size = ((uint)kernel_memory / PAGE);

   int i;
   for (i=0; i<kernel_memory_size; i++) {
      kernel_memory->page[i].physical_page_addr = (page_dir_index << 10) + i;
      //cprintf("page addr %d \n", ptable->pages[i].physical_page_addr);
      kernel_memory->page[i].present = 1;
      kernel_memory->page[i].readwenable = 1;
      kernel_memory->page[i].user = 0;
   }
}


void
free_page_dir(struct page_directory *page_dir){
  //This will clear a page directory, as well as its page tables, freeing memory.
  int i;
  uint *addr;
  for(i= 1; i < 1024; i++) //0 is kernel. Don't nuke that.
  {
    addr = 0;
    if (page_dir->page_tables[i].present == 1)
    {
    addr = page_dir->page_tables[i].page_table_ptr << 12;
    kfree(addr, PAGE);
    }
  }
  addr = page_dir;
  kfree(addr, PAGE);
};
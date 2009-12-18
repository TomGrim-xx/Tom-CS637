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

   struct page_table * ptable = (struct page_table *) kalloc(PAGE);

   kernel_memory_size = ((uint)ptable / PAGE);

   int i;
   for (i=0; i<kernel_memory_size; i++) {
      kernel_memory->pages[i].physical_page_addr = (page_dir_index << 10) + i;
      //cprintf("page addr %d \n", ptable->pages[i].physical_page_addr);
      kernel_memory->pages[i].present = 1;
      kernel_memory->pages[i].readwenable = 1;
      kernel_memory->pages[i].user = 0;
   }
}

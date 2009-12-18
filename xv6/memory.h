
struct page_table * kernel_memory;

uint kernel_memory_size;  //in pages

//Start user memory spaces at virtual address starting at 4MB.
#define USER_MEM_START 0x00400000


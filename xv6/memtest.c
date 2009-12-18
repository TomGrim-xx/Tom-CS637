#include "types.h"
#include "user.h"
#define PAGE 4096

void*
writemem(int pages) {
   void* addr = malloc(pages * PAGE);
   int i;
   for(i=0; i<pages; i++) {
      *(int*)(addr+i*PAGE) = i;
      printf(1, "*");
   }
   return addr;
}


int
readmem(void* addr, int pages) {
   int i;
   for(i=0; i<pages; i++) {
      int v = *(int*)(addr+i*PAGE);
      if (v==i) {
         printf(0, "\x80");
      } else {
         printf(1, "%d %d\n", i, v);
      }
   }
}

int
main(int argc, char *argv[]) {
   

   int pages = 20;
   void* addr = writemem(pages);
   readmem(addr, pages);
   free(addr);

   printf(1, "\n");
   exit();
}

#include "types.h"
#include "stat.h"
#include "user.h"

#define PAGE 4096


int one=1, two=2, three=3, four=4, five=5;

int
print_numbers() {
   printf(1, "pid %d: %d, %d, %d, %d, %d \n", getpid(), &one, &two, &three, &four, &five);
   printf(1, "pid %d: %d, %d, %d, %d, %d \n", getpid(), one, two, three, four, five);
}

int
function() {
   printf(1, "hello threads!\n");
   print_numbers();
   one=10;
   printf(1, "one is now 10\n");
   print_numbers();
}


int
main(int argc, char *argv[])
{
   print_numbers();
   
   printf(1, "allocate stack space\n");
   void* child_stack = malloc(PAGE);
   if (child_stack == 0) {
      printf(1, "fail\n");
      exit();
   }

   printf(1, "child_stack=%x\n", child_stack);

   printf(1, "clone process\n");
   int child = clone(function, child_stack);
   if (child == 0) {
      printf(1, "in child!\n");
      function();
      exit();
   }

   printf(1, "hello parent process!\n");
   print_numbers();

   printf(1, "child is %d\n", child);

   wait();

   print_numbers();

   exit();
}

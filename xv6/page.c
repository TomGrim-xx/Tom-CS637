#include "types.h"
#include "user.h"

int
main(int argc, char *argv[]) {
   int r = exec_page(argv[1], argv+1);
   printf(1, "exec_page failed\n", r);
   exit();
}


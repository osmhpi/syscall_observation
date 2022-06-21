#include <stdio.h>

int main(void) {
  FILE *f = fopen("out.txt", "w");
  fputs("hello, world\n", f);
  fclose(f);
}

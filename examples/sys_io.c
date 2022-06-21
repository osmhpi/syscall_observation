#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(void) {
  int fd = open("out.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
  const char *s = "hello, world\n";
  write(fd, s, strlen(s));
  close(fd);
}

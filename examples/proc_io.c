#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/wait.h>

int main (void) {
  int fd = open("out.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
  const char *s = "hello, world\n";
  write(fd, s, strlen(s) + 1);

  printf("%u\n", getpid());

  pid_t pid = fork();
  if (pid == 0) // child
    execl("/usr/bin/bash", "bash", NULL);
  else // parent
    wait(NULL);
}

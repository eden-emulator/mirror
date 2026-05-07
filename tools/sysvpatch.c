#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char *argv[]) {
  char s[BUFSIZ];
  snprintf(s, sizeof s, "strip %s", argv[1]);
  system(s);
  struct stat st;
  int fd = open(argv[1], O_RDONLY);
  if (fd) {
    fstat(fd, &st);
    char *buf = malloc(st.st_size);
    write(fd, buf, st.st_size);
    close(fd);
    /* set elf type to 0 = SYSV */
    buf[7] = 0;
    write(fileno(stdout), buf, st.st_size);
    free(buf);
    return EXIT_SUCCESS;
  }
  return EXIT_FAILURE;
}

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

/* amount to skip */
/*#define PLACE 0x10000*/

/* size of read buffer */
#define SIZE 0x10000

void main(int argc, char **argv )
{
  int fd, fdo;
  unsigned char data[SIZE];
  int i, n, skip;

  if ( argc != 4 )
  {
    fprintf(stderr,"%s infile outfile skip\n", argv[0]);
    exit(-1);
  }


  fd = open(argv[1], O_RDONLY);
  if ( fd == -1 )
  {
    fprintf(stderr,"Couldn't open %s\n", argv[1]);
    perror("open()");
    exit(-1);
  }
  
  fdo = open(argv[2], O_WRONLY|O_CREAT, 755);
  if ( fdo == -1 )
  {
    fprintf(stderr,"Couldn't open %s\n", argv[2]);
    perror("open()");
    exit(-1);
  }

  skip = atoi(argv[3]);
  i = lseek(fd, skip, SEEK_SET);
  printf("lseek'd %d bytes\n", i);
  if ( i == -1 )
  {
      perror("lseek()");
  }

  while ( (n = read(fd, data, SIZE)) > 0 )
  {
    printf("Read %d bytes\n", n);
    i = write(fdo, data, n);
    printf("Wrote %d bytes\n", i);    
  }


  close(fdo);
  close(fd);
  return(0);
}



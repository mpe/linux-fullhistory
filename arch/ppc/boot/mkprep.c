/*
 * This program will make a type 0x41 load image from an
 * executable file.  Note:  assumes that the executable has
 * already been "flattened" by 'mkboot'.
 *
 * usage: mk_type41 flat-file image
 */

#include <stdio.h>
#include <errno.h>
#ifdef linux
#include <asm/stat.h>
#else
#include <sys/stat.h>
#endif
#include <asm/byteorder.h> /* use same as kernel -- Cort */

typedef unsigned long dword_t;
typedef unsigned short word_t;
typedef unsigned char byte_t;
typedef byte_t block_t[512];
typedef byte_t page_t[4096];

_LE(long val, unsigned char *le)
{
	le[0] = val;
	le[1] = val >> 8;
	le[2] = val >> 16;
	le[3] = val >> 24;
}

/*
 * Partition table entry
 *  - from the PReP spec
 */
typedef struct partition_entry {
    byte_t	boot_indicator;
    byte_t	starting_head;
    byte_t	starting_sector;
    byte_t	starting_cylinder;

    byte_t	system_indicator;
    byte_t	ending_head;
    byte_t	ending_sector;
    byte_t	ending_cylinder;

    dword_t	beginning_sector;
    dword_t	number_of_sectors;
} partition_entry_t;

#define BootActive	0x80
#define SystemPrep	0x41


int main(int argc, char *argv[])
{
  struct stat info;
  char buf[8192];
  int in_fd, out_fd,len;
  unsigned char block[512];
  partition_entry_t *pe = (partition_entry_t *)&block[0x1BE];
  dword_t *entry  = (dword_t *)&block[0x50];
  dword_t *length = (dword_t *)&block[0x51];
  
  if (argc != 3)
    {
      fprintf(stderr, "usage: mk_type41 <boot-file> <image>\n");
      exit(1);
    }
  if ((in_fd = open(argv[1], 0)) < 0)
    {
      exit(2);
    }
  if ((out_fd = creat(argv[2], 0755)) < 0)
    {
      exit(2);
    }

  bzero( &block, sizeof block );
    

  /*
   * Magic marker
   */
  block[510] = 0x55;
  block[511] = 0xAA;
  
  pe->boot_indicator   = BootActive;
  pe->system_indicator = SystemPrep;
    pe->starting_head     = 0;		/* zero-based			     */
    pe->starting_sector   = 2;		/* one-based			     */
    pe->starting_cylinder = 0;		/* zero-based			     */

    pe->ending_head       = 1;		/* assumes two heads		     */
    pe->ending_sector     = 18;		/* assumes 18 sectors/track	     */
    pe->ending_cylinder   = 79;		/* assumes 80 cylinders/diskette     */

#if 0
#if 0    
  pe->beginning_sector  = LeDword(1);
#else
  /* This has to be 0 on the PowerStack? */
  pe->beginning_sector  = LeDword(0);
#endif
  pe->number_of_sectors = LeDword(2*18*80-1);
#endif
  
  if (fstat(in_fd, &info) < 0)
    {
      exit(4);
    }
  /* begin execution at 0x400 */
  _LE(0x400,(unsigned char *)entry);
  _LE(info.st_size+0x400,length);
  
  lseek( out_fd, 0, 0 );
  /* write out 1st block */
  write( out_fd, block, sizeof block );
  
  /* copy image */
#if 1 
	lseek(out_fd, 0x400, 0);
	while ((len = read(in_fd, buf, sizeof(buf))) > 0)
	{
		if (write(out_fd, buf, len) != len)
		{
			exit(5);
		}
	}
	if (len < 0)
	{
		exit(6);
	}
	close(in_fd);
	close(out_fd);
#endif	
  return 0;
}


/*
 * This program will make a type 0x41 load image from an
 * executable file.  Note:  assumes that the executable has
 * already been "flattened" by 'mkboot'.
 *
 * usage: mk_type41 flat-file image
 */

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

_LE(long val, unsigned char *le)
{
	le[0] = val;
	le[1] = val >> 8;
	le[2] = val >> 16;
	le[3] = val >> 24;
}

main(int argc, char *argv[])
{
	int in_fd, out_fd, len, size;
	struct stat info;
	char buf[8192];
	struct hdr
	{
		unsigned long entry_point;
		unsigned long image_length;
	} hdr;
	if (argc != 3)
	{
		fprintf(stderr, "usage: mk_type41 <boot-file> <image>\n");
		exit(1);
	}
	if ((in_fd = open(argv[1], 0)) < 0)
	{
		fprintf(stderr, "Can't open input file: '%s': %s\n", argv[1], strerror(errno));
		exit(2);
	}
	if ((out_fd = creat(argv[2], 0666)) < 0)
	{
		fprintf(stderr, "Can't create output file: '%s': %s\n", argv[2], strerror(errno));
		exit(2);
	}
	if (fstat(in_fd, &info) < 0)
	{
		fprintf(stderr, "Can't get info on input file: %s\n", strerror(errno));
		exit(4);
	}
	write_prep_boot_partition(out_fd);
	_LE(0x400, &hdr.entry_point);
	_LE(info.st_size+0x400, &hdr.image_length);
	lseek(out_fd, 0x200, 0);
	if (write(out_fd, &hdr, sizeof(hdr)) != sizeof(hdr))
	{
		fprintf(stderr, "Can't write output file: %s\n", strerror(errno));
		exit(5);
	}
	lseek(out_fd, 0x400, 0);
	while ((len = read(in_fd, buf, sizeof(buf))) > 0)
	{
		if (write(out_fd, buf, len) != len)
		{
			fprintf(stderr, "Can't write output file: %s\n", strerror(errno));
			exit(5);
		}
	}
	if (len < 0)
	{
		fprintf(stderr, "Can't read input file: %s\n", strerror(errno));
		exit(6);
	}
	close(in_fd);
	close(out_fd);
}

/* Adapted from IBM Naked Application Package (NAP) */

#define Align(value,boundary)						\
	(((value) + (boundary) - 1) & ~((boundary) - 1))

#define HiByte(word)		((word_t)(word) >> 8)
#define LoByte(word)		((word_t)(word) & 0xFF)

#define HiWord(dword)		((dword_t)(dword) >> 16)
#define LoWord(dword)		((dword_t)(dword) & 0xFFFF)

/*
 * Little-endian stuff
 */
#define LeWord(word)							\
	(((word_t)(word) >> 8) | ((word_t)(word) << 8))

#define LeDword(dword)							\
	(LeWord(LoWord(dword)) << 16) | LeWord(HiWord(dword))

#define PcDword(dword)							\
	(LeWord(LoWord(dword)) << 16) | LeWord(HiWord(dword))


typedef unsigned long dword_t;
typedef unsigned short word_t;
typedef unsigned char byte_t;
typedef byte_t block_t[512];
typedef byte_t page_t[4096];

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


/*
 * Writes the "boot record", which contains the partition table, to the
 * diskette, followed by the dummy PC boot block and load image descriptor
 * block.  It returns the number of bytes it has written to the load
 * image.
 *
 * The boot record is the first block of the diskette and identifies the
 * "PReP" partition.  The "PReP" partition contains the "load image" starting
 * at offset zero within the partition.  The first block of the load image is
 * a dummy PC boot block.  The second block is the "load image descriptor"
 * which contains the size of the load image and the entry point into the
 * image.  The actual boot image starts at offset 1024 bytes (third sector)
 * in the partition.
 */
void
write_prep_boot_partition(int out_fd)
{
    block_t block;
    partition_entry_t *pe = (partition_entry_t *)&block[0x1BE];
    dword_t *entry  = (dword_t *)&block[0];
    dword_t *length = (dword_t *)&block[4];

    bzero( &block, sizeof block );

    /*
     * Magic marker
     */
    block[510] = 0x55;
    block[511] = 0xAA;

    /*
     * Build a "PReP" partition table entry in the boot record
     *  - "PReP" may only look at the system_indicator
     */
    pe->boot_indicator   = BootActive;
    pe->system_indicator = SystemPrep;

    /*
     * The first block of the diskette is used by this "boot record" which
     * actually contains the partition table. (The first block of the
     * partition contains the boot image, but I digress...)  We'll set up
     * one partition on the diskette and it shall contain the rest of the
     * diskette.
     */
    pe->starting_head     = 0;		/* zero-based			     */
    pe->starting_sector   = 2;		/* one-based			     */
    pe->starting_cylinder = 0;		/* zero-based			     */

    pe->ending_head       = 1;		/* assumes two heads		     */
    pe->ending_sector     = 18;		/* assumes 18 sectors/track	     */
    pe->ending_cylinder   = 79;		/* assumes 80 cylinders/diskette     */

    /*
     * The "PReP" software ignores the above fields and just looks at
     * the next two.
     *   - size of the diskette is (assumed to be)
     *     (2 tracks/cylinder)(18 sectors/tracks)(80 cylinders/diskette)
     *   - unlike the above sector numbers, the beginning sector is zero-based!
     */
#if 0     
    pe->beginning_sector  = LeDword(1);
#else
    /* This has to be 0 on the PowerStack? */   
    pe->beginning_sector  = LeDword(0);
#endif    
    pe->number_of_sectors = LeDword(2*18*80-1);

    /*
     * Write the partition table
     */
    lseek( out_fd, 0, 0 );
    write( out_fd, block, sizeof block );
}


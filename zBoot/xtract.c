/*
 *  linux/tools/build.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This file builds a disk-image from three different files:
 *
 * - bootsect: max 510 bytes of 8086 machine code, loads the rest
 * - setup: max 4 sectors of 8086 machine code, sets up system parm
 * - system: 80386 code for actual system
 *
 * It does some checking that all files are of the correct type, and
 * just writes the result to stdout, removing headers and padding to
 * the right amount. It also writes some system data to stderr.
 */

/*
 * Changes by tytso to allow root device specification
 */

#include <stdio.h>	/* fprintf */
#include <string.h>
#include <stdlib.h>	/* contains exit */
#include <sys/types.h>	/* unistd.h needs this */
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>	/* contains read/write */
#include <fcntl.h>
#include <a.out.h>
#include <linux/config.h>

#define GCC_HEADER 1024

#define STRINGIFY(x) #x

void die(char * str)
{
	fprintf(stderr,"%s\n",str);
	exit(1);
}

void usage(void)
{
	die("Usage: xtract system [ | gzip | piggyback > piggy.s]");
}

int main(int argc, char ** argv)
{
	int i,c,id, sz;
	char buf[1024];
	char major_root, minor_root;
	struct stat sb;

	struct exec *ex = (struct exec *)buf;

	if (argc  != 2)
		usage();
	
	if ((id=open(argv[1],O_RDONLY,0))<0)
		die("Unable to open 'system'");
	if (read(id,buf,GCC_HEADER) != GCC_HEADER)
		die("Unable to read header of 'system'");
	if (N_MAGIC(*ex) != ZMAGIC)
		die("Non-GCC header of 'system'");

	sz = N_SYMOFF(*ex) - GCC_HEADER + 4;	/* +4 to get the same result than tools/build */

	fprintf(stderr, "System size is %d\n", sz);

	while (sz)
	{
		int l, n;

		l = sz;
		if (l > sizeof(buf)) l = sizeof(buf);

		if ((n=read(id, buf, l)) !=l)
		{
			if (n == -1) 
			   perror(argv[1]);
			else
			   fprintf(stderr, "Unexpected EOF\n");

			die("Can't read system");
		}

		write(1, buf, l);
		sz -= l;
	}

	close(id);
	return(0);
}

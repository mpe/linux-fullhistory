/*
 * arch/alpha/boot/tools/build.c
 *
 * Build a bootable image from the vmlinux binary
 */
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <a.out.h>

#include <asm/system.h>

#define MAXSECT 10
#define MAXBUF 8192

int verbose = 0;
int pad = 0;
char * program = "tools/build";
char buffer[MAXBUF];
unsigned long bootblock[64];
struct filehdr fhdr;
struct aouthdr ahdr;
struct scnhdr  shdr[MAXSECT];

char * usage = "'build [-b] system > secondary' or 'build > primary'";

static void die(char * str)
{
	fprintf(stderr,"%s: %s\n", program, str);
	exit(1);
}

static int comp(struct scnhdr * a, struct scnhdr * b)
{
	return a->s_vaddr - b->s_vaddr;
}

int main(int argc, char ** argv)
{
	int fd, i;
	unsigned long tmp, start;
	unsigned long system_start, system_size;
	char * infile = NULL;

	system_start = START_ADDR;
	system_size = START_SIZE;
	if (argc) {
		program = *(argv++);
		argc--;
	}
	while (argc > 0) {
		if (**argv == '-') {
			while (*++*argv) {
				switch (**argv) {
					case 'b':
						system_start = BOOT_ADDR;
						system_size = BOOT_SIZE;
						pad = 1;
						break;
					case 'v':
						verbose++;
						break;
					default:
						die(usage);
				}
			}
		} else if (infile)
			die(usage);
		else
			infile = *argv;
		argv++;
		argc--;
	}
	if (!infile) {
		memcpy(bootblock, "Linux Test", 10);
		bootblock[60] = BOOT_SIZE / 512;	/* count */
		bootblock[61] = 1;			/* starting LBM */
		bootblock[62] = 0;			/* flags */
		tmp = 0;
		for (i = 0 ; i < 63 ; i++)
			tmp += bootblock[i];
		bootblock[63] = tmp;
		if (write(1, (char *) bootblock, 512) != 512) {
			perror("bbwrite");
			exit(1);
		}
		return 0;
	}
	fd = open(infile, O_RDONLY);
	if (fd < 0) {
		perror(infile);
		exit(1);
	}
	if (read(fd, &fhdr, sizeof(struct filehdr)) != sizeof(struct filehdr))
		die("unable to read file header");
	if (fhdr.f_nscns > MAXSECT)
		die("Too many sections");
	if (fhdr.f_opthdr != AOUTHSZ)
		die("optional header doesn't look like a.out");
	if (read(fd, &ahdr, sizeof(struct aouthdr)) != sizeof(struct aouthdr))
		die("unable to read a.out header");
	for (i = 0 ; i < fhdr.f_nscns ; i++) {
		if (read(fd, i+shdr, sizeof(struct scnhdr)) != sizeof(struct scnhdr))
			die("unable to read section header");
		if (shdr[i].s_paddr != shdr[i].s_vaddr)
			die("unable to handle different phys/virt addresses");
		if (shdr[i].s_relptr)
			die("Unable to handle relocation info");
		if (verbose) {
			fprintf(stderr, "section %d (%.8s):\t%lx - %lx (at %x)\n",
				i, shdr[i].s_name,
				shdr[i].s_vaddr,
				shdr[i].s_vaddr + shdr[i].s_size,
				shdr[i].s_scnptr);
		}
	}
	qsort(shdr, fhdr.f_nscns, sizeof(shdr[1]), comp);
	start = system_start;
	for (i = 0 ; i < fhdr.f_nscns ; i++) {
		unsigned long size, offset;
		memset(buffer, 0, MAXBUF);
		if (!strcmp(shdr[i].s_name, ".comment"))
			continue;
		if (shdr[i].s_vaddr != start)
			die("Unordered or badly placed segments");
		size = shdr[i].s_size;
		start += size;
		offset = shdr[i].s_scnptr;
		if (lseek(fd, offset, SEEK_SET) != offset)
			die("Unable to seek in in-file");
		while (size > 0) {
			unsigned long num = size;
			if (num > MAXBUF)
				num = MAXBUF;
			if (offset)
				if (read(fd, buffer, num) != num)
					die("partial read");
			if (write(1, buffer, num) != num)
				die("partial write");
			size -= num;
		}
		if (verbose) {
			fprintf(stderr, "section %d (%.8s):\t%lx - %lx (at %x)\n",
				i, shdr[i].s_name,
				shdr[i].s_vaddr,
				shdr[i].s_vaddr + shdr[i].s_size,
				shdr[i].s_scnptr);
		}
	}
	if (start > system_start + system_size) {
		fprintf(stderr, "Boot image too large\n");
		exit(1);
	}
	if (pad) {
		unsigned long count = (system_start + system_size) - start;
		memset(buffer, 0, MAXBUF);
		while (count > 0) {
			int i = MAXBUF;
			if (i > count)
				i = count;
			i = write(1, buffer, i);
			if (i <= 0) {
				perror("pad write");
				exit(1);
			}
			count -= i;
		}
	}
		
	return 0;
}

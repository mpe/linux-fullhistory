#include <sys/types.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

/* zlib required.. */
#include <zlib.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#include "cramfs.h"

#define PAGE_CACHE_SIZE (4096)

static const char *progname = "mkcramfs";

void usage(void)
{
	fprintf(stderr, "Usage: '%s dirname outfile'\n"
		" where <dirname> is the root of the\n"
		" filesystem to be compressed.\n", progname);
	exit(1);
}

struct entry {
	/* stats */
	char *name;
	unsigned int mode, size, uid, gid;

	/* FS data */
	void *uncompressed;
	unsigned int dir_offset;	/* Where in the archive is the directory entry? */
	unsigned int data_offset;	/* Where in the archive is the start of the data? */

	/* organization */
	struct entry *child;
	struct entry *next;
};

/*
 * We should mind about memory leaks and
 * checking for out-of-memory.
 *
 * We don't.
 */
static unsigned int parse_directory(const char *name, struct entry **prev)
{
	DIR *dir;
	int count = 0, totalsize = 0;
	struct dirent *dirent;
	char *path, *endpath;
	int len = strlen(name);

	dir = opendir(name);
	if (!dir) {
		perror(name);
		exit(2);
	}
	/* Set up the path.. */
	path = malloc(4096);
	memcpy(path, name, len);
	endpath = path + len;
	*endpath = '/';
	endpath++;

	while ((dirent = readdir(dir)) != NULL) {
		struct entry *entry;
		struct stat st;
		int fd, size;

		/* Ignore "." and ".." - we won't be adding them to the archive */
		if (dirent->d_name[0] == '.') {
			if (dirent->d_name[1] == '\0')
				continue;
			if (dirent->d_name[1] == '.') {
				if (dirent->d_name[2] == '\0')
					continue;
			}
		}
		strcpy(endpath, dirent->d_name);

		if (lstat(path, &st) < 0) {
			perror(endpath);
			continue;
		}
		entry = calloc(1, sizeof(struct entry));
		entry->name = strdup(dirent->d_name);
		entry->mode = st.st_mode;
		entry->size = st.st_size;
		entry->uid = st.st_uid;
		entry->gid = st.st_gid;
		size = sizeof(struct cramfs_inode) + (~3 & (strlen(entry->name) + 3));
		if (S_ISDIR(st.st_mode)) {
			entry->size = parse_directory(path, &entry->child);
		} else if (S_ISREG(st.st_mode)) {
			int fd = open(path, O_RDONLY);
			if (fd < 0) {
				perror(path);
				continue;
			}
			if (entry->size)
				entry->uncompressed = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
			if (-1 == (int) (long) entry->uncompressed) {
				perror("mmap");
				exit(5);
			}
			close(fd);
		} else if (S_ISLNK(st.st_mode)) {
			entry->uncompressed = malloc(st.st_size);
			if (readlink(path, entry->uncompressed, st.st_size) < 0) {
				perror(path);
				continue;
			}
		} else {
			entry->size = st.st_rdev;
		}

		/* Link it into the list */
		*prev = entry;
		prev = &entry->next;
		count++;
		totalsize += size;
	}
	closedir(dir);
	free(path);
	return totalsize;
}

static void set_random(void *area, int size)
{
	int fd = open("/dev/random", O_RDONLY);

	if (fd >= 0) {
		if (read(fd, area, size) == size)
			return;
	}
	memset(area, 0x00, size);
}

static unsigned int write_superblock(struct entry *root, char *base)
{
	struct cramfs_super *super = (struct cramfs_super *) base;
	unsigned int offset = sizeof(struct cramfs_super);

	super->magic = CRAMFS_MAGIC;
	super->flags = 0;
	super->size = 0x10000;
	memcpy(super->signature, CRAMFS_SIGNATURE, sizeof(super->signature));
	set_random(super->fsid, sizeof(super->fsid));
	strncpy(super->name, "Compressed", sizeof(super->name));

	super->root.mode = root->mode;
	super->root.uid = root->uid;
	super->root.gid = root->gid;
	super->root.size = root->size;
	super->root.offset = offset >> 2;

	return offset;
}

static void set_data_offset(struct entry *entry, char *base, unsigned long offset)
{
	struct cramfs_inode *inode = (struct cramfs_inode *) (base + entry->dir_offset);
	inode->offset = (offset >> 2);
}


/*
 * We do a width-first printout of the directory
 * entries, using a stack to remember the directories
 * we've seen.
 */
#define MAXENTRIES (100)
static int stack_entries = 0;
static struct entry *entry_stack[MAXENTRIES];

static unsigned int write_directory_structure(struct entry *entry, char *base, unsigned int offset)
{
	for (;;) {
		while (entry) {
			struct cramfs_inode *inode = (struct cramfs_inode *) (base + offset);
			int len = strlen(entry->name);

			entry->dir_offset = offset;
			offset += sizeof(struct cramfs_inode);

			inode->mode = entry->mode;
			inode->uid = entry->uid;
			inode->gid = entry->gid;
			inode->size = entry->size;
			inode->offset = 0;	/* Fill in later */

			memcpy(base + offset, entry->name, len);
			/* Pad up the name to a 4-byte boundary */
			while (len & 3) {
				*(base + offset + len) = '\0';
				len++;
			}
			inode->namelen = len >> 2;
			offset += len;

			printf("  %s\n", entry->name);

			if (entry->child) {
				entry_stack[stack_entries] = entry;
				stack_entries++;
			}
			entry = entry->next;
		}
		if (!stack_entries)
			break;
		stack_entries--;
		entry = entry_stack[stack_entries];

		set_data_offset(entry, base, offset);
		printf("'%s':\n", entry->name);
		entry = entry->child;
	}
	return offset;
}

/*
 * One 4-byte pointer per block and then the actual blocked
 * output. The first block does not need an offset pointer,
 * as it will start immediately after the pointer block.
 *
 * Note that size > 0, as a zero-sized file wouldn't ever
 * have gotten here in the first place.
 */
static unsigned int do_compress(char *base, unsigned int offset, char *uncompressed, unsigned int size)
{
	unsigned long original_size = size;
	unsigned long original_offset = offset;
	unsigned long new_size;
	unsigned long blocks = (size - 1) / PAGE_CACHE_SIZE + 1;
	unsigned long curr = offset + 4 * blocks;
	int change;

	do {
		unsigned int input = size;
		unsigned long len = 8192;
		if (input > PAGE_CACHE_SIZE)
			input = PAGE_CACHE_SIZE;
		compress(base + curr, &len, uncompressed, input);
		uncompressed += input;
		size -= input;
		curr += len;

		if (len > PAGE_CACHE_SIZE*2) {
			printf("AIEEE: block expanded to > 2*blocklength (%d)\n", len);
			exit(1);
		}

		*(u32 *) (base + offset) = curr;
		offset += 4;
	} while (size);

	new_size = curr - original_offset;
	change = new_size - original_size;
	printf("%4.2f %% (%d bytes)\n", (change * 100) / (double) original_size, change);

	return (curr + 3) & ~3;
}

static unsigned int write_data(struct entry *entry, char *base, unsigned int offset)
{
	do {
		if (entry->uncompressed) {
			set_data_offset(entry, base, offset);
			offset = do_compress(base, offset, entry->uncompressed, entry->size);
		}
		if (entry->child)
			offset = write_data(entry->child, base, offset);
		entry = entry->next;
	} while (entry);
	return offset;
}

/* This is the maximum rom-image you can create */
#define MAXROM (64*1024*1024)

/*
 * Usage:
 *
 *      mkcramfs directory-name
 *
 * where "directory-name" is simply the root of the directory
 * tree that we want to generate a compressed filesystem out
 * of..
 */
int main(int argc, char **argv)
{
	struct stat st;
	struct entry *root_entry;
	char *rom_image;
	unsigned int offset, written;
	int fd;

	if (argc)
		progname = argv[0];
	if (argc != 3)
		usage();

	if (stat(argv[1], &st) < 0) {
		perror(argv[1]);
		exit(1);
	}
	fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0666);

	root_entry = calloc(1, sizeof(struct entry));
	root_entry->mode = st.st_mode;
	root_entry->uid = st.st_uid;
	root_entry->gid = st.st_gid;
	root_entry->name = "";

	root_entry->size = parse_directory(argv[1], &root_entry->child);

	rom_image = mmap(NULL, MAXROM, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (-1 == (int) (long) rom_image) {
		perror("ROM image map");
		exit(1);
	}
	offset = write_superblock(root_entry, rom_image);
	printf("Super block: %d bytes\n", offset);

	offset = write_directory_structure(root_entry->child, rom_image, offset);
	printf("Directory data: %d bytes\n", offset);

	offset = write_data(root_entry, rom_image, offset);
	printf("Everything: %d bytes\n", offset);

	written = write(fd, rom_image, offset);
	if (written < 0) {
		perror("rom image");
		exit(1);
	}
	if (offset != written) {
		fprintf(stderr, "ROM image write failed (%d %d)\n", written, offset);
		exit(1);
	}
	return 0;
}

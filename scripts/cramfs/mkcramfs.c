/*
 * mkcramfs - make a cramfs file system
 *
 * Copyright (C) 1999-2001 Transmeta Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <sys/types.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <linux/cramfs_fs.h>
#include <zlib.h>

#define PAD_SIZE 512		/* only 0 and 512 supported by kernel */

static const char *progname = "mkcramfs";

/* N.B. If you change the disk format of cramfs, please update fs/cramfs/README. */

/* Input status of 0 to print help and exit without an error. */
static void usage(int status)
{
	FILE *stream = status ? stderr : stdout;

	fprintf(stream, "usage: %s [-h] [-e edition] [-i file] [-n name] dirname outfile\n"
		" -h         print this help\n"
		" -E         make all warnings errors (non-zero exit status)\n"
		" -e edition set edition number (part of fsid)\n"
		" -i file    insert a file image into the filesystem (requires >= 2.4.0)\n"
		" -n name    set name of cramfs filesystem\n"
		" -p         pad by %d bytes for boot code\n"
		" -s         sort directory entries (old option, ignored)\n"
		" -z         make explicit holes (requires >= 2.3.39)\n"
		" dirname    root of the filesystem to be compressed\n"
		" outfile    output file\n", progname, PAD_SIZE);

	exit(status);
}

#define PAGE_CACHE_SIZE (4096)
/* The kernel assumes PAGE_CACHE_SIZE as block size. */
static unsigned int blksize = PAGE_CACHE_SIZE;
static long total_blocks = 0, total_nodes = 1; /* pre-count the root node */
static int image_length = 0;

/*
 * If opt_holes is set, then mkcramfs can create explicit holes in the
 * data, which saves 26 bytes per hole (which is a lot smaller a
 * saving than most most filesystems).
 *
 * Note that kernels up to at least 2.3.39 don't support cramfs holes,
 * which is why this is turned off by default.
 */
static int opt_edition = 0;
static int opt_errors = 0;
static int opt_holes = 0;
static int opt_pad = 0;
static char *opt_image = NULL;
static char *opt_name = NULL;

static int warn_dev, warn_gid, warn_namelen, warn_skip, warn_size, warn_uid;

#ifndef MIN
# define MIN(_a,_b) ((_a) < (_b) ? (_a) : (_b))
#endif

/* In-core version of inode / directory entry. */
struct entry {
	/* stats */
	char *name;
	unsigned int mode, size, uid, gid;

	/* FS data */
	void *uncompressed;
        /* points to other identical file */
        struct entry *same;
        unsigned int offset;            /* pointer to compressed data in archive */
	unsigned int dir_offset;	/* Where in the archive is the directory entry? */

	/* organization */
	struct entry *child; /* null for non-directories and empty directories */
	struct entry *next;
};

/*
 * The longest file name component to allow for in the input directory tree.
 * Ext2fs (and many others) allow up to 255 bytes.  A couple of filesystems
 * allow longer (e.g. smbfs 1024), but there isn't much use in supporting
 * >255-byte names in the input directory tree given that such names get
 * truncated to 255 bytes when written to cramfs.
 */
#define MAX_INPUT_NAMELEN 255

static int find_identical_file(struct entry *orig,struct entry *newfile)
{
        if(orig==newfile) return 1;
        if(!orig) return 0;
        if(orig->size==newfile->size && orig->uncompressed && !memcmp(orig->uncompressed,newfile->uncompressed,orig->size)) {
                newfile->same=orig;
                return 1;
        }
        return find_identical_file(orig->child,newfile) ||
                   find_identical_file(orig->next,newfile);
}

static void eliminate_doubles(struct entry *root,struct entry *orig) {
        if(orig) {
                if(orig->size && orig->uncompressed)
			find_identical_file(root,orig);
                eliminate_doubles(root,orig->child);
                eliminate_doubles(root,orig->next);
        }
}

/*
 * We define our own sorting function instead of using alphasort which
 * uses strcoll and changes ordering based on locale information.
 */
static int cramsort (const void *a, const void *b)
{
	return strcmp ((*(const struct dirent **) a)->d_name,
		       (*(const struct dirent **) b)->d_name);
}

static unsigned int parse_directory(struct entry *root_entry, const char *name, struct entry **prev, loff_t *fslen_ub)
{
	struct dirent **dirlist;
	int totalsize = 0, dircount, dirindex;
	char *path, *endpath;
	size_t len = strlen(name);

	/* Set up the path. */
	/* TODO: Reuse the parent's buffer to save memcpy'ing and duplication. */
	path = malloc(len + 1 + MAX_INPUT_NAMELEN + 1);
	if (!path) {
		perror(NULL);
		exit(8);
	}
	memcpy(path, name, len);
	endpath = path + len;
	*endpath = '/';
	endpath++;

        /* read in the directory and sort */
        dircount = scandir(name, &dirlist, 0, cramsort);

	if (dircount < 0) {
		perror(name);
		exit(8);
	}

	/* process directory */
	for (dirindex = 0; dirindex < dircount; dirindex++) {
		struct dirent *dirent;
		struct entry *entry;
		struct stat st;
		int size;
		size_t namelen;

		dirent = dirlist[dirindex];

		/* Ignore "." and ".." - we won't be adding them to the archive */
		if (dirent->d_name[0] == '.') {
			if (dirent->d_name[1] == '\0')
				continue;
			if (dirent->d_name[1] == '.') {
				if (dirent->d_name[2] == '\0')
					continue;
			}
		}
		namelen = strlen(dirent->d_name);
		if (namelen > MAX_INPUT_NAMELEN) {
			fprintf(stderr,
				"Very long (%u bytes) filename `%s' found.\n"
				" Please increase MAX_INPUT_NAMELEN in mkcramfs.c and recompile.  Exiting.\n",
				namelen, dirent->d_name);
			exit(8);
		}
		memcpy(endpath, dirent->d_name, namelen + 1);

		if (lstat(path, &st) < 0) {
			perror(endpath);
			warn_skip = 1;
			continue;
		}
		entry = calloc(1, sizeof(struct entry));
		if (!entry) {
			perror(NULL);
			exit(8);
		}
		entry->name = strdup(dirent->d_name);
		if (!entry->name) {
			perror(NULL);
			exit(8);
		}
		if (namelen > 255) {
			/* Can't happen when reading from ext2fs. */

			/* TODO: we ought to avoid chopping in half
			   multi-byte UTF8 characters. */
			entry->name[namelen = 255] = '\0';
			warn_namelen = 1;
		}
		entry->mode = st.st_mode;
		entry->size = st.st_size;
		entry->uid = st.st_uid;
		if (entry->uid >= 1 << CRAMFS_UID_WIDTH)
			warn_uid = 1;
		entry->gid = st.st_gid;
		if (entry->gid >= 1 << CRAMFS_GID_WIDTH)
			/* TODO: We ought to replace with a default
                           gid instead of truncating; otherwise there
                           are security problems.  Maybe mode should
                           be &= ~070.  Same goes for uid once Linux
                           supports >16-bit uids. */
			warn_gid = 1;
		size = sizeof(struct cramfs_inode) + ((namelen + 3) & ~3);
		*fslen_ub += size;
		if (S_ISDIR(st.st_mode)) {
			entry->size = parse_directory(root_entry, path, &entry->child, fslen_ub);
		} else if (S_ISREG(st.st_mode)) {
			/* TODO: We ought to open files in do_compress, one
			   at a time, instead of amassing all these memory
			   maps during parse_directory (which don't get used
			   until do_compress anyway).  As it is, we tend to
			   get EMFILE errors (especially if mkcramfs is run
			   by non-root).

			   While we're at it, do analagously for symlinks
			   (which would just save a little memory). */
			int fd = open(path, O_RDONLY);
			if (fd < 0) {
				perror(path);
				warn_skip = 1;
				continue;
			}
			if (entry->size) {
				if ((entry->size >= 1 << CRAMFS_SIZE_WIDTH)) {
					warn_size = 1;
					entry->size = (1 << CRAMFS_SIZE_WIDTH) - 1;
				}

				entry->uncompressed = mmap(NULL, entry->size, PROT_READ, MAP_PRIVATE, fd, 0);
				if (-1 == (int) (long) entry->uncompressed) {
					perror("mmap");
					exit(8);
				}
			}
			close(fd);
		} else if (S_ISLNK(st.st_mode)) {
			entry->uncompressed = malloc(entry->size);
			if (!entry->uncompressed) {
				perror(NULL);
				exit(8);
			}
			if (readlink(path, entry->uncompressed, entry->size) < 0) {
				perror(path);
				warn_skip = 1;
				continue;
			}
		} else if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode)) {
			/* maybe we should skip sockets */
			entry->size = 0;
		} else {
			entry->size = st.st_rdev;
			if (entry->size & -(1<<CRAMFS_SIZE_WIDTH))
				warn_dev = 1;
		}

		if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
			int blocks = ((entry->size - 1) / blksize + 1);

			/* block pointers & data expansion allowance + data */
			if(entry->size)
				*fslen_ub += (4+26)*blocks + entry->size + 3;
                }

		/* Link it into the list */
		*prev = entry;
		prev = &entry->next;
		totalsize += size;
	}
	free(path);
	free(dirlist);		/* allocated by scandir() with malloc() */
	return totalsize;
}

/* Returns sizeof(struct cramfs_super), which includes the root inode. */
static unsigned int write_superblock(struct entry *root, char *base, int size)
{
	struct cramfs_super *super = (struct cramfs_super *) base;
	unsigned int offset = sizeof(struct cramfs_super) + image_length;

	if (opt_pad) {
		offset += opt_pad;
	}

	super->magic = CRAMFS_MAGIC;
	super->flags = CRAMFS_FLAG_FSID_VERSION_2 | CRAMFS_FLAG_SORTED_DIRS;
	if (opt_holes)
		super->flags |= CRAMFS_FLAG_HOLES;
	if (image_length > 0)
		super->flags |= CRAMFS_FLAG_SHIFTED_ROOT_OFFSET;
	super->size = size;
	memcpy(super->signature, CRAMFS_SIGNATURE, sizeof(super->signature));

	super->fsid.crc = crc32(0L, Z_NULL, 0);
	super->fsid.edition = opt_edition;
	super->fsid.blocks = total_blocks;
	super->fsid.files = total_nodes;

	memset(super->name, 0x00, sizeof(super->name));
	if (opt_name)
		strncpy(super->name, opt_name, sizeof(super->name));
	else
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
#ifdef DEBUG
	assert ((offset & 3) == 0);
#endif /* DEBUG */
	if (offset >= (1 << (2 + CRAMFS_OFFSET_WIDTH))) {
		fprintf(stderr, "filesystem too big.  Exiting.\n");
		exit(8);
	}
	inode->offset = (offset >> 2);
}


/*
 * We do a width-first printout of the directory
 * entries, using a stack to remember the directories
 * we've seen.
 */
#define MAXENTRIES (100)
static unsigned int write_directory_structure(struct entry *entry, char *base, unsigned int offset)
{
	int stack_entries = 0;
	struct entry *entry_stack[MAXENTRIES];

	for (;;) {
		int dir_start = stack_entries;
		while (entry) {
			struct cramfs_inode *inode = (struct cramfs_inode *) (base + offset);
			size_t len = strlen(entry->name);

			entry->dir_offset = offset;

			inode->mode = entry->mode;
			inode->uid = entry->uid;
			inode->gid = entry->gid;
			inode->size = entry->size;
			inode->offset = 0;
			/* Non-empty directories, regfiles and symlinks will
			   write over inode->offset later. */

			offset += sizeof(struct cramfs_inode);
			total_nodes++;	/* another node */
			memcpy(base + offset, entry->name, len);
			/* Pad up the name to a 4-byte boundary */
			while (len & 3) {
				*(base + offset + len) = '\0';
				len++;
			}
			inode->namelen = len >> 2;
			offset += len;

			/* TODO: this may get it wrong for chars >= 0x80.
			   Most filesystems use UTF8 encoding for filenames,
			   whereas the console is a single-byte character
			   set like iso-latin-1. */
			printf("  %s\n", entry->name);
			if (entry->child) {
				if (stack_entries >= MAXENTRIES) {
					fprintf(stderr, "Exceeded MAXENTRIES.  Raise this value in mkcramfs.c and recompile.  Exiting.\n");
					exit(8);
				}
				entry_stack[stack_entries] = entry;
				stack_entries++;
			}
			entry = entry->next;
		}

		/*
		 * Reverse the order the stack entries pushed during
                 * this directory, for a small optimization of disk
                 * access in the created fs.  This change makes things
                 * `ls -UR' order.
		 */
		{
			struct entry **lo = entry_stack + dir_start;
			struct entry **hi = entry_stack + stack_entries;
			struct entry *tmp;

			while (lo < --hi) {
				tmp = *lo;
				*lo++ = *hi;
				*hi = tmp;
			}
		}

		/* Pop a subdirectory entry from the stack, and recurse. */
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

static int is_zero(char const *begin, unsigned len)
{
	if (opt_holes)
		/* Returns non-zero iff the first LEN bytes from BEGIN are
		   all NULs. */
		return (len-- == 0 ||
			(begin[0] == '\0' &&
			 (len-- == 0 ||
			  (begin[1] == '\0' &&
			   (len-- == 0 ||
			    (begin[2] == '\0' &&
			     (len-- == 0 ||
			      (begin[3] == '\0' &&
			       memcmp(begin, begin + 4, len) == 0))))))));
	else
		/* Never create holes. */
		return 0;
}

/*
 * One 4-byte pointer per block and then the actual blocked
 * output. The first block does not need an offset pointer,
 * as it will start immediately after the pointer block;
 * so the i'th pointer points to the end of the i'th block
 * (i.e. the start of the (i+1)'th block or past EOF).
 *
 * Note that size > 0, as a zero-sized file wouldn't ever
 * have gotten here in the first place.
 */
static unsigned int do_compress(char *base, unsigned int offset, char const *name, char *uncompressed, unsigned int size)
{
	unsigned long original_size = size;
	unsigned long original_offset = offset;
	unsigned long new_size;
	unsigned long blocks = (size - 1) / blksize + 1;
	unsigned long curr = offset + 4 * blocks;
	int change;

	total_blocks += blocks;

	do {
		unsigned long len = 2 * blksize;
		unsigned int input = size;
		if (input > blksize)
			input = blksize;
		size -= input;
		if (!is_zero (uncompressed, input)) {
			compress(base + curr, &len, uncompressed, input);
			curr += len;
		}
		uncompressed += input;

		if (len > blksize*2) {
			/* (I don't think this can happen with zlib.) */
			printf("AIEEE: block \"compressed\" to > 2*blocklength (%ld)\n", len);
			exit(8);
		}

		*(u32 *) (base + offset) = curr;
		offset += 4;
	} while (size);

	curr = (curr + 3) & ~3;
	new_size = curr - original_offset;
	/* TODO: Arguably, original_size in these 2 lines should be
	   st_blocks * 512.  But if you say that then perhaps
	   administrative data should also be included in both. */
	change = new_size - original_size;
	printf("%6.2f%% (%+d bytes)\t%s\n",
	       (change * 100) / (double) original_size, change, name);

	return curr;
}


/*
 * Traverse the entry tree, writing data for every item that has
 * non-null entry->compressed (i.e. every symlink and non-empty
 * regfile).
 */
static unsigned int write_data(struct entry *entry, char *base, unsigned int offset)
{
	do {
		if (entry->uncompressed) {
                        if(entry->same) {
                                set_data_offset(entry, base, entry->same->offset);
                                entry->offset=entry->same->offset;
                        } else {
                                set_data_offset(entry, base, offset);
                                entry->offset=offset;
                                offset = do_compress(base, offset, entry->name, entry->uncompressed, entry->size);
                        }
		}
		else if (entry->child)
			offset = write_data(entry->child, base, offset);
                entry=entry->next;
	} while (entry);
	return offset;
}

static unsigned int write_file(char *file, char *base, unsigned int offset)
{
	int fd;
	char *buf;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		perror(file);
		exit(8);
	}
	buf = mmap(NULL, image_length, PROT_READ, MAP_PRIVATE, fd, 0);
	memcpy(base + offset, buf, image_length);
	munmap(buf, image_length);
	close (fd);
	/* Pad up the image_length to a 4-byte boundary */
	while (image_length & 3) {
		*(base + offset + image_length) = '\0';
		image_length++;
	}
	return (offset + image_length);
}

/*
 * Maximum size fs you can create is roughly 256MB.  (The last file's
 * data must begin within 256MB boundary but can extend beyond that.)
 *
 * Note that if you want it to fit in a ROM then you're limited to what the
 * hardware and kernel can support (64MB?).
 */
#define MAXFSLEN ((((1 << CRAMFS_OFFSET_WIDTH) - 1) << 2) /* offset */ \
		  + (1 << CRAMFS_SIZE_WIDTH) - 1 /* filesize */ \
		  + (1 << CRAMFS_SIZE_WIDTH) * 4 / PAGE_CACHE_SIZE /* block pointers */ )


/*
 * Usage:
 *
 *      mkcramfs directory-name outfile
 *
 * where "directory-name" is simply the root of the directory
 * tree that we want to generate a compressed filesystem out
 * of.
 */
int main(int argc, char **argv)
{
	struct stat st;		/* used twice... */
	struct entry *root_entry;
	char *rom_image;
	ssize_t offset, written;
	int fd;
	/* initial guess (upper-bound) of required filesystem size */
	loff_t fslen_ub = sizeof(struct cramfs_super);
	char const *dirname, *outfile;
	u32 crc = crc32(0L, Z_NULL, 0);
	int c;			/* for getopt */

	total_blocks = 0;

	if (argc)
		progname = argv[0];

	/* command line options */
	while ((c = getopt(argc, argv, "hEe:i:n:psz")) != EOF) {
		switch (c) {
		case 'h':
			usage(0);
		case 'E':
			opt_errors = 1;
			break;
		case 'e':
			opt_edition = atoi(optarg);
			break;
		case 'i':
			opt_image = optarg;
			if (lstat(opt_image, &st) < 0) {
				perror(opt_image);
				exit(16);
			}
			image_length = st.st_size; /* may be padded later */
			fslen_ub += (image_length + 3); /* 3 is for padding */
			break;
		case 'n':
			opt_name = optarg;
			break;
		case 'p':
			opt_pad = PAD_SIZE;
			fslen_ub += PAD_SIZE;
			break;
		case 's':
			/* old option, ignored */
			break;
		case 'z':
			opt_holes = 1;
			break;
		}
	}

	if ((argc - optind) != 2)
		usage(16);
	dirname = argv[optind];
	outfile = argv[optind + 1];

	if (stat(dirname, &st) < 0) {
		perror(dirname);
		exit(16);
	}
	fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);

	root_entry = calloc(1, sizeof(struct entry));
	if (!root_entry) {
		perror(NULL);
		exit(8);
	}
	root_entry->mode = st.st_mode;
	root_entry->uid = st.st_uid;
	root_entry->gid = st.st_gid;

	root_entry->size = parse_directory(root_entry, dirname, &root_entry->child, &fslen_ub);

	/* always allocate a multiple of blksize bytes because that's
           what we're going to write later on */
	fslen_ub = ((fslen_ub - 1) | (blksize - 1)) + 1;

	if (fslen_ub > MAXFSLEN) {
		fprintf(stderr,
			"warning: guestimate of required size (upper bound) is %LdMB, but maximum image size is %uMB.  We might die prematurely.\n",
			fslen_ub >> 20,
			MAXFSLEN >> 20);
		fslen_ub = MAXFSLEN;
	}

        /* find duplicate files. TODO: uses the most inefficient algorithm
           possible. */
        eliminate_doubles(root_entry,root_entry);

	/* TODO: Why do we use a private/anonymous mapping here
           followed by a write below, instead of just a shared mapping
           and a couple of ftruncate calls?  Is it just to save us
           having to deal with removing the file afterwards?  If we
           really need this huge anonymous mapping, we ought to mmap
           in smaller chunks, so that the user doesn't need nn MB of
           RAM free.  If the reason is to be able to write to
           un-mmappable block devices, then we could try shared mmap
           and revert to anonymous mmap if the shared mmap fails. */
	rom_image = mmap(NULL, fslen_ub?fslen_ub:1, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (-1 == (int) (long) rom_image) {
		perror("ROM image map");
		exit(8);
	}

	/* Skip the first opt_pad bytes for boot loader code */
	offset = opt_pad;
	memset(rom_image, 0x00, opt_pad);

	/* Skip the superblock and come back to write it later. */
	offset += sizeof(struct cramfs_super);

	/* Insert a file image. */
	if (opt_image) {
		printf("Including: %s\n", opt_image);
		offset = write_file(opt_image, rom_image, offset);
	}

	offset = write_directory_structure(root_entry->child, rom_image, offset);
	printf("Directory data: %d bytes\n", offset);

	offset = write_data(root_entry, rom_image, offset);

	/* We always write a multiple of blksize bytes, so that
           losetup works. */
	offset = ((offset - 1) | (blksize - 1)) + 1;
	printf("Everything: %d kilobytes\n", offset >> 10);

	/* Write the superblock now that we can fill in all of the fields. */
	write_superblock(root_entry, rom_image+opt_pad, offset);
	printf("Super block: %d bytes\n", sizeof(struct cramfs_super));

	/* Put the checksum in. */
	crc = crc32(crc, (rom_image+opt_pad), (offset-opt_pad));
	((struct cramfs_super *) (rom_image+opt_pad))->fsid.crc = crc;
	printf("CRC: %x\n", crc);

	/* Check to make sure we allocated enough space. */
	if (fslen_ub < offset) {
		fprintf(stderr, "not enough space allocated for ROM image (%Ld allocated, %d used)\n",
			fslen_ub, offset);
		exit(8);
	}

	written = write(fd, rom_image, offset);
	if (written < 0) {
		perror("ROM image");
		exit(8);
	}
	if (offset != written) {
		fprintf(stderr, "ROM image write failed (%d %d)\n", written, offset);
		exit(8);
	}

	/* (These warnings used to come at the start, but they scroll off the
           screen too quickly.) */
	if (warn_namelen) /* (can't happen when reading from ext2fs) */
		fprintf(stderr, /* bytes, not chars: think UTF8. */
			"warning: filenames truncated to 255 bytes.\n");
	if (warn_skip)
		fprintf(stderr, "warning: files were skipped due to errors.\n");
	if (warn_size)
		fprintf(stderr,
			"warning: file sizes truncated to %luMB (minus 1 byte).\n",
			1L << (CRAMFS_SIZE_WIDTH - 20));
	if (warn_uid) /* (not possible with current Linux versions) */
		fprintf(stderr,
			"warning: uids truncated to %u bits.  (This may be a security concern.)\n",
			CRAMFS_UID_WIDTH);
	if (warn_gid)
		fprintf(stderr,
			"warning: gids truncated to %u bits.  (This may be a security concern.)\n",
			CRAMFS_GID_WIDTH);
	if (warn_dev)
		fprintf(stderr,
			"WARNING: device numbers truncated to %u bits.  This almost certainly means\n"
			"that some device files will be wrong.\n",
			CRAMFS_OFFSET_WIDTH);
	if (opt_errors &&
	    (warn_namelen||warn_skip||warn_size||warn_uid||warn_gid||warn_dev))
		exit(8);
	return 0;
}

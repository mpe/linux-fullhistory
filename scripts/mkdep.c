#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/mman.h>

char *filename, *command, __depname[256] = "\n\t@touch ";
int needsconfig, hasconfig, hasmodules, hasdep;

#define depname (__depname+9)

struct path_struct {
	int len;
	char buffer[256-sizeof(int)];
} path_array[2] = {
	{ 23, "/usr/src/linux/include/" },
	{  0, "" }
};

static void handle_include(int type, char *name, int len)
{
	int plen;
	struct path_struct *path = path_array+type;

	if (len == 14)
		if (!memcmp(name, "linux/config.h", len))
			hasconfig = 1;
		else if (!memcmp(name, "linux/module.h", len))
			hasmodules = 1;

	plen = path->len;
	memcpy(path->buffer+plen, name, len);
	len += plen;
	path->buffer[len] = '\0';
	if (access(path->buffer, F_OK))
		return;

	if (!hasdep) {
		hasdep = 1;
		printf("%s:", depname);
	}
	printf(" \\\n   %s", path->buffer);
}

static void handle_config(void)
{
	needsconfig = 1;
	if (!hasconfig)
		fprintf(stderr,
			"%s needs config but has not included config file\n",
			filename);
}

#if defined(__alpha__) || defined(__i386__)
#define LE_MACHINE
#endif

#ifdef LE_MACHINE
#define next_byte(x) (x >>= 8)
#define current ((unsigned char) __buf)
#else
#define next_byte(x) (x <<= 8)
#define current (__buf >> 8*(sizeof(unsigned long)-1))
#endif

#define GETNEXT { \
next_byte(__buf); \
if (!__nrbuf) { \
	__buf = *(unsigned long *) next; \
	__nrbuf = sizeof(unsigned long); \
	if (!__buf) \
		break; \
} next++; __nrbuf--; }
#define CASE(c,label) if (current == c) goto label
#define NOTCASE(c,label) if (current != c) goto label

static void state_machine(register char *next)
{
	for(;;) {
	register unsigned long __buf = 0;
	register unsigned long __nrbuf = 0;

normal:
	GETNEXT
__normal:
	CASE('/',slash);
	CASE('"',string);
	CASE('\'',char_const);
	CASE('#',preproc);
	goto normal;

slash:
	GETNEXT
	CASE('*',comment);
	goto __normal;

string:
	GETNEXT
	CASE('"',normal);
	NOTCASE('\\',string);
	GETNEXT
	goto string;

char_const:
	GETNEXT
	CASE('\'',normal);
	NOTCASE('\\',char_const);
	GETNEXT
	goto char_const;

comment:
	GETNEXT
__comment:
	NOTCASE('*',comment);
	GETNEXT
	CASE('/',normal);
	goto __comment;

preproc:
	GETNEXT
	CASE('\n',normal);
	CASE(' ',preproc);
	CASE('\t',preproc);
	CASE('i',i_preproc);
	CASE('e',e_preproc);
	GETNEXT

skippreproc:
	CASE('\n',normal);
	CASE('\\',skippreprocslash);
	GETNEXT
	goto skippreproc;

skippreprocslash:
	GETNEXT;
	GETNEXT;
	goto skippreproc;

e_preproc:
	GETNEXT
	NOTCASE('l',skippreproc);
	GETNEXT
	NOTCASE('i',skippreproc);
	GETNEXT
	CASE('f',if_line);
	goto skippreproc;

i_preproc:
	GETNEXT
	CASE('f',if_line);
	NOTCASE('n',skippreproc);
	GETNEXT
	NOTCASE('c',skippreproc);
	GETNEXT
	NOTCASE('l',skippreproc);
	GETNEXT
	NOTCASE('u',skippreproc);
	GETNEXT
	NOTCASE('d',skippreproc);
	GETNEXT
	NOTCASE('e',skippreproc);

/* "# include" found */
include_line:
	GETNEXT
	CASE('\n',normal);
	CASE('<', std_include_file);
	NOTCASE('"', include_line);

/* "local" include file */
{
	char *incname = next;
local_include_name:
	GETNEXT
	CASE('\n',normal);
	NOTCASE('"', local_include_name);
	handle_include(1, incname, next-incname-1);
	goto skippreproc;
}

/* <std> include file */
std_include_file:
{
	char *incname = next;
std_include_name:
	GETNEXT
	CASE('\n',normal);
	NOTCASE('>', std_include_name);
	handle_include(0, incname, next-incname-1);
	goto skippreproc;
}

if_line:
	if (needsconfig)
		goto skippreproc;
if_start:
	GETNEXT
	CASE('C', config);
	CASE('\n', normal);
	CASE('_', if_middle);
	if (current >= 'a' && current <= 'z')
		goto if_middle;
	if (current < 'A' || current > 'Z')
		goto if_start;
config:
	GETNEXT
	NOTCASE('O', __if_middle);
	GETNEXT
	NOTCASE('N', __if_middle);
	GETNEXT
	NOTCASE('F', __if_middle);
	GETNEXT
	NOTCASE('I', __if_middle);
	GETNEXT
	NOTCASE('G', __if_middle);
	GETNEXT
	NOTCASE('_', __if_middle);
	handle_config();
	goto skippreproc;

if_middle:
	GETNEXT
__if_middle:
	CASE('\n', normal);
	CASE('_', if_middle);
	if (current >= 'a' && current <= 'z')
		goto if_middle;
	if (current < 'A' || current > 'Z')
		goto if_start;
	goto if_middle;
	}
}

static void do_depend(void)
{
	char *map;
	int mapsize;
	int pagesizem1 = getpagesize()-1;
	int fd = open(filename, O_RDONLY);
	struct stat st;

	if (fd < 0) {
		if (errno != ENOENT)
			perror(filename);
		return;
	}
	fstat(fd, &st);
	if (st.st_size == 0) {
		fprintf(stderr,"%s is empty\n",filename);
		return;
	}
	mapsize = st.st_size + 2*sizeof(unsigned long);
	mapsize = (mapsize+pagesizem1) & ~pagesizem1;
	map = mmap(NULL, mapsize, PROT_READ, MAP_PRIVATE, fd, 0);
	if (-1 == (long)map) {
		perror("mkdep: mmap");
		close(fd);
		return;
	}
	close(fd);
	state_machine(map);
	munmap(map, mapsize);
	if (hasdep)
		puts(command);
}

int main(int argc, char **argv)
{
	int len;
	char * hpath;

	hpath = getenv("HPATH");
	if (!hpath)
		hpath = "/usr/src/linux/include";
	len = strlen(hpath);
	memcpy(path_array[0].buffer, hpath, len);
	if (len && hpath[len-1] != '/') {
		path_array[0].buffer[len] = '/';
		len++;
	}
	path_array[0].buffer[len] = '\0';
	path_array[0].len = len;

	while (--argc > 0) {
		int len;
		char *name = *++argv;

		filename = name;
		len = strlen(name);
		memcpy(depname, name, len+1);
		command = __depname;
		if (len > 2 && name[len-2] == '.') {
			switch (name[len-1]) {
				case 'c':
				case 'S':
					depname[len-1] = 'o';
					command = "";
			}
		}
		needsconfig = hasconfig = hasmodules = hasdep = 0;
		do_depend();
		if (hasconfig && !hasmodules && !needsconfig)
			fprintf(stderr, "%s doesn't need config\n", filename);
	}
	return 0;
}

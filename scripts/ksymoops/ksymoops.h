/*
	ksymoops.h.

	Copyright Keith Owens <kaos@ocs.com.au>.
	Released under the GNU Public Licence, Version 2.

	Tue Nov  3 02:31:01 EST 1998
	Version 0.6
	Read lsmod (/proc/modules).
	Convert from a.out to bfd, using same format as ksymoops.
	PPC trace addresses are not bracketed, add new re.

	Wed Oct 28 13:47:23 EST 1998
	Version 0.4
	Split into separate sources.
*/

#include <sys/types.h>
#include <regex.h>
#include <stdio.h>


/* Pity this is not externalised, see binfmt_elf.c */
#define elf_addr_t unsigned long

extern char *prefix;
extern char *path_nm;		/* env KSYMOOPS_NM */
extern char *path_find;		/* env KSYMOOPS_FIND */
extern char *path_objdump;	/* env KSYMOOPS_OBJDUMP */
extern int debug;
extern int errors;
extern int warnings;

typedef struct symbol SYMBOL;

struct symbol {
	char *name;		/* name of symbol */
	char type;		/* type of symbol from nm/System.map */
	char keep;		/* keep this symbol in merged map? */
	elf_addr_t address;	/* address in kernel */
};

/* Header for symbols from one particular source */

typedef struct symbol_set SYMBOL_SET;

struct symbol_set {
	char *source;			/* where the symbols came from */
	int used;			/* number of symbols used */
	int alloc;			/* number of symbols allocated */
	SYMBOL *symbol;			/* dynamic array of symbols */
	SYMBOL_SET *related;		/* any related symbol set */
};

extern SYMBOL_SET  ss_vmlinux;
extern SYMBOL_SET  ss_ksyms_base;
extern SYMBOL_SET *ss_ksyms_module;
extern int         ss_ksyms_modules;
extern SYMBOL_SET  ss_lsmod;
extern SYMBOL_SET *ss_object;
extern int         ss_objects;
extern SYMBOL_SET  ss_system_map;

extern SYMBOL_SET  ss_merged;	/* merged map with info from all sources */
extern SYMBOL_SET  ss_Version;	/* Version_ numbers where available */

/* Regular expression stuff */

extern regex_t     re_nm;
extern regmatch_t *re_nm_pmatch;
extern regex_t     re_bracketed_address;
extern regmatch_t *re_bracketed_address_pmatch;
extern regex_t     re_unbracketed_address;
extern regmatch_t *re_unbracketed_address_pmatch;

/* Bracketed address: optional '[', required '<', at least 4 hex characters,
 * required '>', optional ']', optional white space.
 */
#define BRACKETED_ADDRESS	"\\[*<([0-9a-fA-F]{4,})>\\]*[ \t]*"

#define UNBRACKETED_ADDRESS	"([0-9a-fA-F]{4,})[ \t]*"

/* io.c */
extern int regular_file(const char *file, const char *msg);
extern FILE *fopen_local(const char *file, const char *mode, const char *msg);
extern void fclose_local(FILE *f, const char *msg);
extern char *fgets_local(char **line, int *size, FILE *f, const char *msg);
extern int fwrite_local(void const *ptr, size_t size, size_t nmemb,
			FILE *stream, const char *msg);
extern FILE *popen_local(const char *cmd, const char *msg);
extern void pclose_local(FILE *f, const char *msg);

/* ksyms.c */
extern void read_ksyms(const char *ksyms);
extern void map_ksyms_to_modules(void);
extern void read_lsmod(const char *lsmod);
extern void compare_ksyms_lsmod(void);

/* misc.c */
extern void malloc_error(const char *msg);
extern const char *format_address(elf_addr_t address);
extern char *find_fullpath(const char *program);

/* map.c */
extern void read_system_map(const char *system_map);
extern void merge_maps(const char *save_system_map);
extern void compare_maps(const SYMBOL_SET *ss1, const SYMBOL_SET *ss2,
			 int precedence);


/* object.c */
extern SYMBOL_SET *adjust_object_offsets(SYMBOL_SET *ss);
extern void read_vmlinux(const char *vmlinux);
extern void expand_objects(char * const *object, int objects);
extern void read_object(const char *object, int i);

/* oops.c */
extern int Oops_read(int filecount, char * const *filename, int code_bytes,
		     int one_shot);

/* re.c */
extern void re_compile(regex_t *preg, const char *regex, int cflags,
		       regmatch_t **pmatch);
extern void re_compile_common(void);
extern void re_strings(regex_t *preg, const char *text, regmatch_t *pmatch,
		       char ***string);
extern void re_strings_free(const regex_t *preg, char ***string);
extern void re_string_check(int need, int available, const char *msg);

/* symbol.c */
extern void ss_init(SYMBOL_SET *ss, const char *msg);
extern void ss_free(SYMBOL_SET *ss);
extern void ss_init_common(void);
extern SYMBOL *find_symbol_name(const SYMBOL_SET *ss, const char *symbol,
				int *start);
extern void add_symbol_n(SYMBOL_SET *ss, const elf_addr_t address,
			 const char type, const char keep, const char *symbol);
extern void add_symbol(SYMBOL_SET *ss, const char *address, const char type,
		       const char keep, const char *symbol);
extern char *map_address(const SYMBOL_SET *ss, const elf_addr_t address);
extern void ss_sort_atn(SYMBOL_SET *ss);
extern void ss_sort_na(SYMBOL_SET *ss);
extern SYMBOL_SET *ss_copy(const SYMBOL_SET *ss);
extern void add_Version(const char *version, const char *source);
extern void extract_Version(SYMBOL_SET *ss);
extern void compare_Version(void);

/*
 * mkboot - Make a 'boot' image from a PowerPC (ELF) binary
 *
 * usage: mkboot <ELF-file> <Boot-file>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <elf/common.h>
#include <elf/external.h>

#define MAX_SECTIONS 64
int fd;
Elf32_External_Ehdr hdr;
Elf32_External_Shdr sections[MAX_SECTIONS];
char *symtab;

/* Boolean type definitions */

#define FALSE     0
#define TRUE      1
#define bool  short

extern char *strerror();
extern int errno;

#define ADDR unsigned long

/* Definitions that control the shape of the output */
#define MAX_ITEMS  32  /* Max # bytes per line */

FILE *in_file;  /* Input (binary image) file */
FILE *out_file; /* Output (boot image) file */

int org = -1;

#ifdef linux
long
_LONG(unsigned long *p)
{
  unsigned char *xp = (unsigned char *)p;
  return ((xp[0]<<24) | (xp[1]<<16) | (xp[2]<<8) | xp[3]);
}

unsigned short
_USHORT(unsigned short *p)
{
  unsigned char *xp = (unsigned char *)p;
  return ((xp[0]<<8) | xp[1]);
}
#else
#define _LONG *
#define _USHORT *
#endif

Elf32_External_Shdr *
find_section(char *section_name)
{
  Elf32_External_Shdr *shdr = sections;
  int i;
  for (i = 0;  i < MAX_SECTIONS;  i++, shdr++)
  {
    if (strcmp(section_name, &symtab[_LONG((int *)shdr->sh_name)]) == 0)
    {
      return (shdr);
    }
  }
  return ((Elf32_External_Shdr *)0);
}

main (argc, argv)
  int argc;
  char *argv[];
{
  if ((argc == 3) || (argc == 4))
  {
    if (argc == 4)
    {
      org = strtol(argv[3], NULL, 0);
    }
    if (init(argc, argv))
    {
      process(argv[2]);
    }
    else exit(255);
  } else
  { /* Illegal command line */
    fprintf(stderr, "Syntax: mkboot <bin_file> <out_file>\n");
    exit(255);
  }
  exit(0);
}

init(argc, argv)
  int argc;
  char *argv[];
{
  int sizeof_sections, size;
  char *fn = argv[1];
  Elf32_External_Shdr *shdr;
  if ((out_file = fopen(argv[2], "w")) == (FILE *)NULL)
  {
    io_err("creating output file");
    return (FALSE);
  }
  if ((in_file = fopen(fn, "r")) == (FILE *)NULL)
  {
    fprintf(stderr, "Can't open '%s': %s\n", fn, strerror(errno));
    return (FALSE);
  }
  if (fread(&hdr, sizeof(hdr), 1, in_file) != 1)
  {
    fprintf(stderr, "Can't read ELF header: %s\n", strerror(errno));
    return (FALSE);
  }
  /* Make sure this is a file we like */
  if ((hdr.e_ident[EI_MAG0] != ELFMAG0) || (hdr.e_ident[EI_MAG1] != ELFMAG1) ||
      (hdr.e_ident[EI_MAG2] != ELFMAG2) || (hdr.e_ident[EI_MAG3] != ELFMAG3))
  {
    fprintf(stderr, "Invalid binary file (not ELF)\n");
    return (FALSE);
  }
  if (hdr.e_ident[EI_CLASS] != ELFCLASS32)
  {
    fprintf(stderr, "Invalid binary file (not ELF32)\n");
    return (FALSE);
  }
  if ((_USHORT((unsigned short *)hdr.e_machine) != EM_CYGNUS_POWERPC) &&
      (_USHORT((unsigned short *)hdr.e_machine) != EM_PPC))
  {
    fprintf(stderr, "Invalid binary file (not PowerPC)\n");
    return (FALSE);
  }
  if (_USHORT((unsigned short *)hdr.e_shnum) > MAX_SECTIONS)
  {
    fprintf(stderr, "Invalid binary file (too many sections)\n");
    return (FALSE);
  }
  fseek(in_file, _LONG((int *)hdr.e_shoff), 0);
  sizeof_sections = _USHORT((unsigned short *)hdr.e_shnum) * sizeof(sections[0]);
  if (fread(sections, sizeof_sections, 1, in_file) != 1)
  {
    fprintf(stderr, "Can't read sections: %s\n", strerror(errno));
    return (FALSE);
  }
  /* Read in symbol table */
  shdr = &sections[_USHORT((unsigned short *)hdr.e_shstrndx)];
  size = _LONG((int *)shdr->sh_size);
  if (!(symtab = malloc(256)))
  {
    fprintf(stderr, "Can't allocate memory for symbol table.\n");
    return (FALSE);
  }
  fseek(in_file, _LONG((int *)shdr->sh_offset), 0);
  if (fread(symtab, size, 1, in_file) != 1)
  {
    fprintf(stderr, "Can't read symbol table: %s\n", strerror(errno));
    return (FALSE);
  }
  return (TRUE);
}

process(out_name)
  char *out_name;
{
  Elf32_External_Shdr *shdr_text, *shdr_data;
  long relocated_text_base, text_base, text_offset, text_size;
  long relocated_data_base, data_base, data_offset, data_size;
  shdr_text = find_section(".text");
  shdr_data = find_section(".data");
  text_base = relocated_text_base = _LONG((int *)shdr_text->sh_addr);
  text_size = _LONG((int *)shdr_text->sh_size);
  text_offset = _LONG((int *)shdr_text->sh_offset);
  data_base = relocated_data_base = _LONG((int *)shdr_data->sh_addr);
  data_size = _LONG((int *)shdr_data->sh_size);
  data_offset = _LONG((int *)shdr_data->sh_offset);
  if (org >= 0)
  {
  	relocated_text_base = org;
  	relocated_data_base = data_base - text_base + org;
  }
fprintf(stderr, "TEXT %x bytes at %x[%x]\n", text_size, text_base, relocated_text_base);
fprintf(stderr, "DATA %x bytes at %x[%x]\n", data_size, data_base, relocated_data_base);
  if (dump_segment(text_offset, relocated_text_base, text_size) &&
      dump_segment(data_offset, relocated_data_base, data_size))
  {
  } else
  {
    unlink(out_name);
  }
}

dump_segment(off, base, size)
  long off;
  ADDR base;
  long size;
{
  char buf[4096];
  int len;
  bool ok = TRUE;
fprintf(stderr, "Reading %x bytes at %x.%x\n", size, off, base);
  fseek(in_file, 0, 0);
  fseek(in_file, off, 0);
#if 0  
  if (org >= 0)
  {
     fseek(out_file, base+org, 0);
  } else
  {
     fseek(out_file, base, 0);
  }
#else
  fseek(out_file, base, 0);
#endif  
  while (size && ok)
  {
    len = size;
    if (len > sizeof(buf))
    {
      len = sizeof(buf);
    }
    if (fread(buf, sizeof(char), len, in_file) == len)
    {
      fwrite(buf, sizeof(char), len, out_file);
    } else
    {
      fprintf(stderr,"Premature EOF encountered.\n");
      ok = FALSE;
    }
    size -= len;
  }
  return (ok);
}

io_err(what)
  char *what;
{
  fprintf(stderr, "Error %s: %s\n", what, strerror(errno));
}



#ifdef sun
extern int sys_nerr;
extern char *sys_errlist[];

char *
strerror(int errno)
{
	static char msg[32];
	if (errno <= sys_nerr)
	{
		return (sys_errlist[errno]);
	} else
	{
		sprintf(msg, "<<Unknown error: %d>>", errno);
		return (msg);
	}
}

strtoul(char *str, char **radix, int base)
{
	return(strtol(str, radix, base));
}
#endif



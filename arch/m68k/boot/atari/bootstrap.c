/*
** bootstrap.c -- Load and launch the Atari Linux kernel
**
** Copyright 1993 by Arjan Knor
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** History:
**	01 Feb 1997 Implemented kernel decompression (Roman)
**	28 Nov 1996 Fixed and tested previous change (James)
**	27 Nov 1996 Compatibility with bootinfo interface version 1.0 (Geert)
**	12 Nov 1996 Fixed and tested previous change (Andreas)
**	18 Aug 1996 Updated for the new boot information structure (untested!)
**		    (Geert)
**	10 Dec 1995 BOOTP/TFTP support (Roman)
**	03 Oct 1995 Allow kernel to be loaded to TT ram again (Andreas)
**	11 Jul 1995 Add support for ELF format kernel (Andreas)
**	16 Jun 1995 Adapted to Linux 1.2: kernel always loaded into ST ram
**		    (Andreas)
**      14 Nov 1994 YANML (Yet Another New Memory Layout :-) kernel
**		    start address is KSTART_ADDR + PAGE_SIZE, this
**		    does not need the ugly kludge with
**		    -fwritable-strings (++andreas)
**      09 Sep 1994 Adapted to the new memory layout: All the boot_info entry
**                  mentions all ST-Ram and the mover is located somewhere
**                  in the middle of memory (roman)
**                  Added the default arguments file known from the other
**                  bootstrap version
**      19 Feb 1994 Changed everything so that it works? (rdv)
**      14 Mar 1994 New mini-copy routine used (rdv)
*/


#define BOOTINFO_COMPAT_1_0	/* bootinfo interface version 1.0 compatible */
/* support compressed kernels? */
#define ZKERNEL

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include "sysvars.h"
#include <osbind.h>
#include <sys/types.h>
#include <sys/file.h>

/* linux specific include files */
#include <linux/a.out.h>
#include <linux/elf.h>
#include <asm/page.h>

#define _LINUX_TYPES_H		/* Hack to prevent including <linux/types.h> */
#include <asm/bootinfo.h>
#include <asm/setup.h>

/* Atari bootstrap include file */
#include "bootstrap.h"

#define MIN_RAMSIZE     (3)	/* 3 MB */
#define TEMP_STACKSIZE 256

extern char *optarg;
extern int optind;
static void get_default_args( int *argc, char ***argv );
static int create_bootinfo(void);
#ifdef BOOTINFO_COMPAT_1_0
static int create_compat_bootinfo(void);
#endif /* BOOTINFO_COMPAT_1_0 */
static int add_bi_record(u_short tag, u_short size, const void *data);
static int add_bi_string(u_short tag, const u_char *s);
/* This is missing in <unistd.h> */
extern int sync (void);

/* Bootinfo */
static struct atari_bootinfo bi;

#ifdef BOOTINFO_COMPAT_1_0
static struct compat_bootinfo compat_bootinfo;
#endif /* BOOTINFO_COMPAT_1_0 */

#define MAX_BI_SIZE     (4096)
static u_long bi_size;
static union {
struct bi_record record;
    u_char fake[MAX_BI_SIZE];
} bi_union;

u_long *cookiejar;
u_long userstk;

/* getcookie -- function to get the value of the given cookie. */
static int getcookie(char *cookie, u_long *value)
{
    int i = 0;

    while(cookiejar[i] != 0L) {
	if(cookiejar[i] == *(u_long *)cookie) {
	    *value = cookiejar[i + 1];
	    return 1;
	}
	i += 2;
    }
    return -1;
}

static void usage(void)
{
    fprintf(stderr, "Usage:\n"
	    "\tbootstrap [-dst] [-k kernel_executable] [-r ramdisk_file]"
	    " [option...]\n");
    exit(EXIT_FAILURE);
}

/*
 * Copy the kernel and the ramdisk to their final resting places.
 *
 * I assume that the kernel data and the ramdisk reside somewhere
 * in the middle of the memory.
 *
 * This program itself should be somewhere in the first 4096 bytes of memory
 * where the kernel never will be. In this way it can never be overwritten
 * by itself.
 *
 * At this point the registers have:
 * a0: the start of the final kernel
 * a1: the start of the current kernel
 * a2: the end of the final ramdisk
 * a3: the end of the current ramdisk
 * d0: the kernel size
 * d1: the ramdisk size 
 */
asm ("
.text
.globl _copyall, _copyallend
_copyall:

	movel	a0,a4	    	/* save the start of the kernel for booting */

1:	movel	a1@+,a0@+   	/* copy the kernel starting at the beginning */
	subql	#4,d0
	jcc	1b

	tstl	d1
	beq		3f

2:	movel	a3@-,a2@-   	/* copy the ramdisk starting at the end */
	subql	#4,d1
	jcc	2b

3:	jmp	a4@ 	    	/* jump to the start of the kernel */
_copyallend:
");

extern char copyall, copyallend;


/* Test for a Medusa: This is the only machine on which address 0 is
 * writeable!
 * ...err! On the Afterburner040 (for the Falcon) it's the same... So we do
 * another test with 0x00ff82fe, that gives a bus error on the Falcon, but is
 * in the range where the Medusa always asserts DTACK.
 * On the Hades address 0 is writeable as well and it asserts DTACK on
 * address 0x00ff82fe. To test if the machine is a Hades, address 0xb0000000
 * is tested. On the Medusa this gives a bus error.
 */

int test_medusa( void )

{	int		rv = 0;

	__asm__ __volatile__
		( "movel	0x8,a0\n\t"
		  "movel	sp,a1\n\t"
		  "moveb	0x0,d1\n\t"
		  "movel	#Lberr,0x8\n\t"
		  "moveq	#0,%0\n\t"
		  "clrb		0x0\n\t"
		  "nop		\n\t"
		  "moveb	d1,0x0\n\t"
		  "nop		\n\t"
		  "tstb		0x00ff82fe\n\t"
		  "nop		\n\t"
		  "moveq	#1,%0\n\t"
		  "tstb		0xb0000000\n\t"
		  "nop		\n\t"
		  "moveq	#0,%0\n"
		"Lberr:\t"
		  "movel	a1,sp\n\t"
		  "movel	a0,0x8"
		  : "=d" (rv)
		  : /* no inputs */
		  : "d1", "a0", "a1", "memory" );

	return( rv );
}


/* Test if FPU instructions are executed in hardware, or if they're
   emulated in software.  For this, the F-line vector is temporarily
   replaced. */

int test_software_fpu(void)
{
	int rv = 0;

	__asm__ __volatile__
		( "movel	0x2c,a0\n\t"
		  "movel	sp,a1\n\t"
		  "movel	#Lfline,0x2c\n\t"
		  "moveq	#1,%0\n\t"
		  "fnop 	\n\t"
		  "nop		\n\t"
		  "moveq	#0,%0\n"
		"Lfline:\t"
		  "movel	a1,sp\n\t"
		  "movel	a0,0x2c"
		  : "=d" (rv)
		  : /* no inputs */
		  : "a0", "a1" );

	return rv;
}


void get_medusa_bank_sizes( u_long *bank1, u_long *bank2 )

{	static u_long	save_addr;
	u_long			test_base, saved_contents[16];
#define	TESTADDR(i)	(*((u_long *)((char *)test_base + i*8*MB)))
#define	TESTPAT		0x12345678
	unsigned short	oldflags;
	int				i;

	/* This ensures at least that none of the test addresses conflicts
	 * with the test code itself */
	test_base = ((unsigned long)&save_addr & 0x007fffff) | 0x20000000;
	*bank1 = *bank2 = 0;
	
	/* Interrupts must be disabled because arbitrary addresses may be
	 * temporarily overwritten, even code of an interrupt handler */
	__asm__ __volatile__ ( "movew sr,%0; oriw #0x700,sr" : "=g" (oldflags) : );
	disable_cache();
	
	/* save contents of the test addresses */
	for( i = 0; i < 16; ++i )
		saved_contents[i] = TESTADDR(i);
	
	/* write 0s into all test addresses */
	for( i = 0; i < 16; ++i )
		TESTADDR(i) = 0;

	/* test for bank 1 */
#if 0
	/* This is Freddi's original test, but it didn't work. */
	TESTADDR(0) = TESTADDR(1) = TESTPAT;
	if (TESTADDR(1) == TESTPAT) {
		if (TESTADDR(2) == TESTPAT)
			*bank1 = 8*MB;
		else if (TESTADDR(3) == TESTPAT)
			*bank1 = 16*MB;
		else
			*bank1 = 32*MB;
	}
	else {
		if (TESTADDR(2) == TESTPAT)
			*bank1 = 0;
		else
			*bank1 = 16*MB;
	}
#else
	TESTADDR(0) = TESTPAT;
	if (TESTADDR(1) == TESTPAT)
		*bank1 = 8*MB;
	else if (TESTADDR(2) == TESTPAT)
		*bank1 = 16*MB;
	else if (TESTADDR(4) == TESTPAT)
		*bank1 = 32*MB;
	else
		*bank1 = 64*MB;
#endif

	/* test for bank2 */
	if (TESTADDR(8) != 0)
		*bank2 = 0;
	else {
		TESTADDR(8) = TESTPAT;
		if (TESTADDR(9) != 0) {
			if (TESTADDR(10) == TESTPAT)
				*bank2 = 8*MB;
			else
				*bank2 = 32*MB;
		}
		else {
			TESTADDR(9) = TESTPAT;
			if (TESTADDR(10) == TESTPAT)
				*bank2 = 16*MB;
			else
				*bank2 = 64*MB;
		}
	}
	
	/* restore contents of the test addresses and restore interrupt mask */
	for( i = 0; i < 16; ++i )
		TESTADDR(i) = saved_contents[i];
	__asm__ __volatile__ ( "movew %0,sr" : : "g" (oldflags) );
}

#undef TESTADDR
#undef TESTPAT


static int check_bootinfo_version(char *memptr)
{
    struct bootversion *bv = (struct bootversion *)memptr;
    unsigned long version = 0;
    int i, kernel_major, kernel_minor, boots_major, boots_minor;

    printf( "\n" );
    if (bv->magic == BOOTINFOV_MAGIC) {
	for( i = 0; bv->machversions[i].machtype != 0; ++i ) {
	    if (bv->machversions[i].machtype == MACH_ATARI) {
		version = bv->machversions[i].version;
		break;
	    }
	}
    }
    if (!version)
	printf("Kernel has no bootinfo version info, assuming 0.0\n");

    kernel_major = BI_VERSION_MAJOR(version);
    kernel_minor = BI_VERSION_MINOR(version);
    boots_major  = BI_VERSION_MAJOR(ATARI_BOOTI_VERSION);
    boots_minor  = BI_VERSION_MINOR(ATARI_BOOTI_VERSION);
    printf("Bootstrap's bootinfo version: %d.%d\n",
	   boots_major, boots_minor);
    printf("Kernel's bootinfo version   : %d.%d\n",
	   kernel_major, kernel_minor);
    
    switch (kernel_major) {
	case BI_VERSION_MAJOR(ATARI_BOOTI_VERSION):
	    if (kernel_minor > boots_minor) {
		printf("Warning: Bootinfo version of bootstrap and kernel "
		       "differ!\n");
		printf("         Certain features may not work.\n");
	    }
	    break;

#ifdef BOOTINFO_COMPAT_1_0
	case BI_VERSION_MAJOR(COMPAT_ATARI_BOOTI_VERSION):
	    printf("(using backwards compatibility mode)\n");
	    break;
#endif /* BOOTINFO_COMPAT_1_0 */

	default:
	    printf("\nThis bootstrap is too %s for this kernel!\n",
		   boots_major < kernel_major ? "old" : "new");
	    return 0;
    }
    return kernel_major;
}


#ifdef USE_BOOTP
# include "bootp.h"
#else
# define ll_read	read
# define ll_lseek	lseek
# define ll_close	close
#endif

#ifdef ZKERNEL
static int load_zkernel( int fd );
static int kread( int fd, void *buf, unsigned cnt );
static int klseek( int fd, int where, int whence );
static int kclose( int fd );
#else
# define kread		read
# define klseek		lseek
# define kclose		close
#endif

/* ++andreas: this must be inline due to Super */
static inline void boot_exit (int) __attribute__ ((noreturn));
static inline void boot_exit(int status)
{
    /* first go back to user mode */
    (void)Super(userstk);
	getchar();
    exit(status);
}

int main(int argc, char *argv[])
{
    int debugflag = 0, ch, kfd, rfd = -1, i, ignore_ttram = 0;
    int load_to_stram = 0;
    char *ramdisk_name, *kernel_name, *memptr;
    u_long ST_ramsize, TT_ramsize, memreq;
    u_long cpu_type, fpu_type, mch_type, mint;
    struct exec kexec;
    int elf_kernel = 0;
    Elf32_Ehdr kexec_elf;
    Elf32_Phdr *kernel_phdrs = NULL;
    u_long start_mem, mem_size, rd_size, text_offset = 0, kernel_size;
    int prefer_bootp = 1, kname_set = 0, n_knames;
#ifdef USE_BOOTP
    int err;
#endif
    char kname_list[5][64];
    void *bi_ptr;

    ramdisk_name = NULL;
    kernel_name = "vmlinux";

    /* print the startup message */
    puts("\fLinux/68k Atari Bootstrap version 2.2"
#ifdef USE_BOOTP
	 " (with BOOTP)"
#endif
	 );
    puts("Copyright 1993,1994 by Arjan Knor, Robert de Vries, Roman Hodek, Andreas Schwab\n");

	/* ++roman: If no arguments on the command line, read them from
	 * file */
	if (argc == 1)
		get_default_args( &argc, &argv );

    /* machine is Atari */
    bi.machtype = MACH_ATARI;

    /* check arguments */
    while ((ch = getopt(argc, argv, "bdtsk:r:")) != EOF)
	switch (ch) {
	  case 'd':
	    debugflag = 1;
	    break;
	  case 't':
	    ignore_ttram = 1;
	    break;
	  case 's':
	    load_to_stram = 1;
	    break;
	  case 'k':
	    kernel_name = optarg;
	    kname_set = 1;
	    break;
	  case 'r':
	    ramdisk_name = optarg;
	    break;
	  case 'b':
	    prefer_bootp = 0;
	    break;
	  case '?':
	  default:
	    usage();
	}

    argc -= optind;
    argv += optind;
  
    /* We have to access some system variables to get
     * the information we need, so we must switch to
     * supervisor mode first.
     */
    userstk = Super(0L);

    /* get the info we need from the cookie-jar */
    cookiejar = *_p_cookies;
    if(cookiejar == 0L) {
	/* if we find no cookies, it's probably an ST */
	fprintf(stderr, "Error: No cookiejar found. Is this an ST?\n");
	boot_exit(EXIT_FAILURE);
    }

    /* Exit if MiNT/MultiTOS is running.  */
    if(getcookie("MiNT", &mint) != -1)
    {
	puts("Warning: MiNT is running\n");
#if 0
	puts("Linux cannot be started when MiNT is running. Aborting...\n");
	boot_exit(EXIT_FAILURE);
#endif
    }

    /* get _CPU, _FPU and _MCH */
    getcookie("_CPU", &cpu_type);
    getcookie("_FPU", &fpu_type);
    getcookie("_MCH", &mch_type);

    /* check if we are on a 68030/40 with FPU */
    if ((cpu_type != 30 && cpu_type != 40 && cpu_type != 60))
    {
	puts("Machine type currently not supported. Aborting...");
	boot_exit(EXIT_FAILURE);
    }

    switch(cpu_type) {
      case  0:
      case 10: break;
      case 20: bi.cputype = CPU_68020; bi.mmutype = MMU_68851; break;
      case 30: bi.cputype = CPU_68030; bi.mmutype = MMU_68030; break;
      case 40: bi.cputype = CPU_68040; bi.mmutype = MMU_68040; break;
      case 60: bi.cputype = CPU_68060; bi.mmutype = MMU_68060; break;
      default:
	fprintf(stderr, "Error: Unknown CPU type. Aborting...\n");
	boot_exit(EXIT_FAILURE);
	break;
    }

    printf("CPU: %ld; ", cpu_type + 68000);
    printf("FPU: ");

    /* check for FPU; in case of a '040 or '060, don't look at _FPU itself,
     * some software may set it to wrong values (68882 or the like) */
	if (cpu_type == 40) {
		bi.fputype = FPU_68040;
		puts( "68040\n" );
	}
	else if (cpu_type == 60) {
		bi.fputype = FPU_68060;
		puts( "68060\n" );
	}
	else {
		switch ((fpu_type >> 16) & 7) {
		  case 0:
			puts("not present\n");
			break;
		  case 1:
			puts("SFP004 not supported. Assuming no FPU.");
			break;
		  case 2:
			/* try to determine real type */
			if (fpu_idle_frame_size () != 0x18)
				goto m68882;
			/* fall through */
		  case 4:
			bi.fputype = FPU_68881;
			puts("68881\n");
			break;
		  case 6:
		  m68882:
			bi.fputype = FPU_68882;
			puts("68882\n");
			break;
		  default:
			puts("Unknown FPU type. Assuming no FPU.");
			break;
		}
	}
	/* ++roman: If an FPU was announced in the cookie, test
	   whether it is a real hardware FPU or a software emulator!  */
	if (bi.fputype) {
		if (test_software_fpu()) {
			bi.fputype = 0;
			puts("FPU: software emulated. Assuming no FPU.");
		}
	}

    /* Get the amounts of ST- and TT-RAM. */
    /* The size must be a multiple of 1MB. */
    i = 0;
	
    if (!test_medusa()) {
	struct {
		unsigned short version;   /* version - currently 1 */
		unsigned long fr_start; /* start addr FastRAM */
		unsigned long fr_len;   /* length FastRAM */
	} *magn_cookie;
	struct {
		unsigned long version;
		unsigned long fr_start; /* start addr */
		unsigned long fr_len;   /* length */
	} *fx_cookie;

        TT_ramsize = 0;
        if (!ignore_ttram) {
	    /* "Original" or properly emulated TT-Ram */
	    if (*ramtop) {
		/* the 'ramtop' variable at 0x05a4 is not
		 * officially documented. We use it anyway
		 * because it is the only way to get the TTram size.
		 * (It is zero if there is no TTram.)
		 */
		bi.memory[i].addr = TT_RAM_BASE;
		bi.memory[i].size = (*ramtop - TT_RAM_BASE) & ~(MB - 1);
		TT_ramsize = bi.memory[i].size / MB;
		i++;
		printf("TT-RAM: %ld Mb; ", TT_ramsize);
	    }

	    /* test for MAGNUM alternate RAM
	     * added 26.9.1995 M. Schwingen, rincewind@discworld.oche.de
	     */
	    if (getcookie("MAGN", (u_long *)&magn_cookie) != -1) {
		bi.memory[i].addr = magn_cookie->fr_start;
		bi.memory[i].size = magn_cookie->fr_len & ~(MB - 1);
		TT_ramsize += bi.memory[i].size / MB;
		printf("MAGNUM alternate RAM: %ld Mb; ", bi.memory[i].size/MB);
		i++;
	    }

	    /* BlowUps FX */
	    if (getcookie("BPFX", (u_long *)&fx_cookie) != -1 && fx_cookie) {
		/* if fx is set (cookie call above),
		 * we assume that BlowUps FX-card
		 * is installed. (Nat!)
		 */
		bi.memory[i].addr = fx_cookie->fr_start;
		bi.memory[i].size = fx_cookie->fr_len & ~(MB - 1);
		printf("FX alternate RAM: %ld Mb; ", bi.memory[i].size/MB);
		i++;
	    }
	}

        bi.memory[i].addr = 0;
        bi.memory[i].size = *phystop & ~(MB - 1);
        ST_ramsize = bi.memory[i].size / MB;
        i++;
        printf("ST-RAM: %ld Mb\n", ST_ramsize );

        bi.num_memory = i;

	if (load_to_stram && i > 1) {
	    /* Put ST-RAM first in the list of mem blocks */
	    struct mem_info temp = bi.memory[i - 1];
	    bi.memory[i - 1] = bi.memory[0];
	    bi.memory[0] = temp;
	}
    }
    else {
        u_long	bank1, bank2, medusa_st_ram;

        get_medusa_bank_sizes( &bank1, &bank2 );
	medusa_st_ram = *phystop & ~(MB - 1);
        bank1 -= medusa_st_ram;
        TT_ramsize = 0;

        bi.memory[i].addr = 0;
        bi.memory[i].size = medusa_st_ram;
        ST_ramsize = bi.memory[i].size / MB;
        i++;
        printf("Medusa pseudo ST-RAM from bank 1: %ld Mb; ", ST_ramsize );

        if (!ignore_ttram && bank1 > 0) {
            bi.memory[i].addr = 0x20000000 + medusa_st_ram;
            bi.memory[i].size = bank1;
            TT_ramsize += bank1;
            i++;
            printf("TT-RAM bank 1: %ld Mb; ", bank1/MB );
        }
			
        if (!ignore_ttram && bank2 > 0) {
            bi.memory[i].addr = 0x24000000;
            bi.memory[i].size = bank2;
            TT_ramsize += bank2;
            i++;
            printf("TT-RAM bank 2: %ld Mb; ", bank2/MB );
        }
			
        bi.num_memory = i;
        printf("\n");
    }

    /* verify that there is enough RAM; ST- and TT-RAM combined */
    if (ST_ramsize + TT_ramsize < MIN_RAMSIZE) {
	puts("Not enough RAM. Aborting...");
	boot_exit(10);
    }

#if 0	
    /* Get language/keyboard info */
    /* TODO: do we need this ? */
    /* Could be used to auto-select keyboard map later on. (rdv) */
    if (getcookie("_AKP",&language) == -1)
    {
	/* Get the language info from the OS-header */
	os_header = *_sysbase;
	os_header = os_header->os_beg;
	lang = (os_header->os_conf) >> 1;
	printf("Language: ");
	switch(lang) {
	  case HOL: puts("Dutch"); break; /* Own country first :-) */
	  case USA: puts("American"); break;
	  case SWG: puts("Switzerland (German)"); break;
	  case FRG: puts("German"); break;
	  case FRA: puts("French"); break;
	  case SWF: puts("Switzerland (French)"); break;
	  case UK:  puts("English"); break;
	  case SPA: puts("Spanish"); break;
	  case ITA: puts("Italian"); break;
	  case SWE: puts("Swedish"); break;
	  case TUR: puts("Turkey"); break;
	  case FIN: puts("Finnish"); break;
	  case NOR: puts("Norwegian"); break;
	  case DEN: puts("Danish"); break;
	  case SAU: puts("Saudi-Arabian"); break;
	  default:  puts("Unknown"); break;
	}
    }
    else
    {
	printf("Language: ");
	switch(language & 0x0F)
	{
	  case 1: printf("German "); break;
	  case 2: printf("French "); break;
	  case 4: printf("Spanish "); break;
	  case 5: printf("Italian "); break;
	  case 7: printf("Swiss French "); break;
	  case 8: printf("Swiss German "); break;
	  default: printf("English ");
	}
	printf("Keyboard type :");
	switch(language >> 8)
	{
	  case 1: printf("German "); break;
	  case 2: printf("French "); break;
	  case 4: printf("Spanish "); break;
	  case 5: printf("Italian "); break;
	  case 7: printf("Swiss French "); break;
	  case 8: printf("Swiss German "); break;
	  default: printf("English ");
	}
	printf("\n");
    }
#endif
	
    /* Pass contents of the _MCH cookie to the kernel */
    bi.mch_cookie = mch_type;
    
    /*
     * Copy command line options into the kernel command line.
     */
    i = 0;
    while (argc--) {
	if ((i+strlen(*argv)+1) < CL_SIZE) {
	    i += strlen(*argv) + 1;
	    if (bi.command_line[0])
		strcat (bi.command_line, " ");
	    strcat (bi.command_line, *argv++);
	}
    }
    printf ("Command line is '%s'\n", bi.command_line);

    start_mem = bi.memory[0].addr;
    mem_size = bi.memory[0].size;

    /* tell us where the kernel will go */
    printf("\nThe kernel will be located at 0x%08lx\n", start_mem);

#ifdef TEST
    /*
    ** Temporary exit point for testing
    */
    boot_exit(-1);
#endif /* TEST */

    i = 0;
#ifdef USE_BOOTP
    if (!kname_set)
	kname_list[i++][0] = '\0'; /* default kernel which BOOTP server says */
#endif
#ifdef ZKERNEL
    strcpy( kname_list[i], kernel_name );
    strcat( kname_list[i], ".gz" );
    ++i;
#endif
    strcpy( kname_list[i++], kernel_name );
#ifdef ZKERNEL
    if (!kname_set)
	strcpy( kname_list[i++], "vmlinuz" );
#endif
    n_knames = i;

    kfd = -1;
#ifdef USE_BOOTP
    if (prefer_bootp) {
	for( i = 0; i < n_knames; ++i ) {
	    if ((err = get_remote_kernel( kname_list[i] )) >= 0)
		goto kernel_open;
	    if (err < -1) /* fatal error; retries don't help... */
		break;
	}
	printf( "\nremote boot failed; trying local kernel\n" );
    }
#endif
    for( i = 0; i < n_knames; ++i ) {
	if ((kfd = open( kname_list[i], O_RDONLY )) != -1)
	    goto kernel_open;
    }
#ifdef USE_BOOTP
    if (!prefer_bootp) {
	printf( "\nlocal kernel failed; trying remote boot\n" );
	for( i = 0; i < n_knames; ++i ) {
	    if ((err = get_remote_kernel( kname_list[i] )) >= 0)
		goto kernel_open;
	    if (err < -1) /* fatal error; retries don't help... */
		break;
	}
    }
#endif
    fprintf( stderr, "Unable to open any kernel file\n(Tried " );
    for( i = 0; i < n_knames; ++i ) {
	fprintf( stderr, "%s%s", kname_list[i],
		 i <  n_knames-2 ? ", " :
		 i == n_knames-2 ? ", and " :
		 ")\n" );
    }
    boot_exit( EXIT_FAILURE );
    
  kernel_open:

    if (kread (kfd, (void *)&kexec, sizeof(kexec)) != sizeof(kexec))
    {
	fprintf (stderr, "Unable to read exec header from %s\n", kernel_name);
	boot_exit (EXIT_FAILURE);
    }

#ifdef ZKERNEL
    if (((unsigned char *)&kexec)[0] == 037 &&
	(((unsigned char *)&kexec)[1] == 0213 ||
	 ((unsigned char *)&kexec)[1] == 0236)) {
	/* That's a compressed kernel */
	printf( "Kernel is compressed\n" );
	if (load_zkernel( kfd )) {
	    printf( "Decompression error -- aborting\n" );
	    boot_exit( EXIT_FAILURE );
	}
    }
#endif
    
    switch (N_MAGIC(kexec)) {
    case ZMAGIC:
	text_offset = N_TXTOFF(kexec);
	break;
    case QMAGIC:
	text_offset = sizeof(kexec);
	/* the text size includes the exec header; remove this */
	kexec.a_text -= sizeof(kexec);
	break;
    default:
	/* Try to parse it as an ELF header */
	klseek (kfd, 0, SEEK_SET);
	if (kread (kfd, (void *)&kexec_elf, sizeof (kexec_elf)) == sizeof (kexec_elf)
	    && memcmp (&kexec_elf.e_ident[EI_MAG0], ELFMAG, SELFMAG) == 0)
	  {
	    elf_kernel = 1;
	    /* A few plausibility checks */
	    if (kexec_elf.e_type != ET_EXEC || kexec_elf.e_machine != EM_68K
		|| kexec_elf.e_version != EV_CURRENT)
	      {
		fprintf (stderr, "Invalid ELF header contents in kernel\n");
		boot_exit (EXIT_FAILURE);
	      }
	    /* Load the program headers */
	    kernel_phdrs = (Elf32_Phdr *) Malloc (kexec_elf.e_phnum * sizeof (Elf32_Phdr));
	    if (kernel_phdrs == NULL)
	      {
		fprintf (stderr, "Unable to allocate memory for program headers\n");
		boot_exit (EXIT_FAILURE);
	      }
	    klseek (kfd, kexec_elf.e_phoff, SEEK_SET);
	    if (kread (kfd, (void *) kernel_phdrs,
		      kexec_elf.e_phnum * sizeof (*kernel_phdrs))
		!= kexec_elf.e_phnum * sizeof (*kernel_phdrs))
	      {
		fprintf (stderr, "Unable to read program headers from %s\n",
			 kernel_name);
		boot_exit (EXIT_FAILURE);
	      }
	    break;
	  }
	fprintf (stderr, "Wrong magic number %lo in kernel header\n",
		 N_MAGIC(kexec));
	boot_exit (EXIT_FAILURE);
    }

    /* Load the kernel one page after start of mem */
    start_mem += PAGE_SIZE;
    mem_size -= PAGE_SIZE;
    /* Align bss size to multiple of four */
    if (!elf_kernel)
      kexec.a_bss = (kexec.a_bss + 3) & ~3;

    /* init ramdisk */
    if(ramdisk_name) {
	if((rfd = open(ramdisk_name, O_RDONLY)) == -1) {
	    fprintf(stderr, "Unable to open ramdisk file %s\n",
		    ramdisk_name);
	    boot_exit(EXIT_FAILURE);
	}
	bi.ramdisk.size = lseek(rfd, 0, SEEK_END);
    }
    else
	bi.ramdisk.size = 0;
 
    /* calculate the total required amount of memory */
    if (elf_kernel)
      {
	u_long min_addr = 0xffffffff, max_addr = 0;
	for (i = 0; i < kexec_elf.e_phnum; i++)
	  {
	    if (min_addr > kernel_phdrs[i].p_vaddr)
	      min_addr = kernel_phdrs[i].p_vaddr;
	    if (max_addr < kernel_phdrs[i].p_vaddr + kernel_phdrs[i].p_memsz)
	      max_addr = kernel_phdrs[i].p_vaddr + kernel_phdrs[i].p_memsz;
	  }
	/* This is needed for newer linkers that include the header in
	   the first segment.  */
	if (min_addr == 0)
	  {
	    min_addr = PAGE_SIZE;
	    kernel_phdrs[0].p_vaddr += PAGE_SIZE;
	    kernel_phdrs[0].p_offset += PAGE_SIZE;
	    kernel_phdrs[0].p_filesz -= PAGE_SIZE;
	    kernel_phdrs[0].p_memsz -= PAGE_SIZE;
	  }
	kernel_size = max_addr - min_addr;
      }
    else
      kernel_size = kexec.a_text + kexec.a_data + kexec.a_bss;

    rd_size = bi.ramdisk.size;
    if (rd_size + kernel_size > mem_size - MB/2 && bi.num_memory > 1)
      /* If running low on ST ram load ramdisk into alternate ram.  */
      bi.ramdisk.addr = (u_long) bi.memory[1].addr + bi.memory[1].size - rd_size;
    else
      /* Else hopefully there is enough ST ram. */
      bi.ramdisk.addr = (u_long)start_mem + mem_size - rd_size;

    /* create the bootinfo structure */
    if (!create_bootinfo())
	boot_exit (EXIT_FAILURE);

    memreq = kernel_size + bi_size;
#ifdef BOOTINFO_COMPAT_1_0
    if (sizeof(compat_bootinfo) > bi_size)
	memreq = kernel_size+sizeof(compat_bootinfo);
#endif /* BOOTINFO_COMPAT_1_0 */
    /* align load address of ramdisk image, read() is sloooow on odd addr. */
    memreq = ((memreq + 3) & ~3) + rd_size;
	
    /* allocate RAM for the kernel */
    if (!(memptr = (char *)Malloc (memreq)))
    {
	fprintf (stderr, "Unable to allocate memory for kernel and ramdisk\n");
	boot_exit (EXIT_FAILURE);
    }
    else
	fprintf(stderr, "kernel at address %lx\n", (u_long) memptr);

    (void)memset(memptr, 0, memreq);

    /* read the text and data segments from the kernel image */
    if (elf_kernel)
      {
	for (i = 0; i < kexec_elf.e_phnum; i++)
	  {
	    if (klseek (kfd, kernel_phdrs[i].p_offset, SEEK_SET) == -1)
	      {
		fprintf (stderr, "Failed to seek to segment %d\n", i);
		boot_exit (EXIT_FAILURE);
	      }
	    if (kread (kfd, memptr + kernel_phdrs[i].p_vaddr - PAGE_SIZE,
		      kernel_phdrs[i].p_filesz)
		!= kernel_phdrs[i].p_filesz)
	      {
		fprintf (stderr, "Failed to read segment %d\n", i);
		boot_exit (EXIT_FAILURE);
	      }
	  }
      }
    else
      {
	if (klseek (kfd, text_offset, SEEK_SET) == -1)
	{
	    fprintf (stderr, "Failed to seek to text\n");
	    Mfree ((void *)memptr);
	    boot_exit (EXIT_FAILURE);
	}

	if (kread (kfd, memptr, kexec.a_text) != kexec.a_text)
	{
	    fprintf (stderr, "Failed to read text\n");
	    Mfree ((void *)memptr);
	    boot_exit (EXIT_FAILURE);
	}

	/* data follows immediately after text */
	if (kread (kfd, memptr + kexec.a_text, kexec.a_data) != kexec.a_data)
	{
	    fprintf (stderr, "Failed to read data\n");
	    Mfree ((void *)memptr);
	    boot_exit (EXIT_FAILURE);
	}
      }
    kclose (kfd);

    /* Check kernel's bootinfo version */
    switch (check_bootinfo_version(memptr)) {
	case BI_VERSION_MAJOR(ATARI_BOOTI_VERSION):
	    bi_ptr = &bi_union.record;
	    break;

#ifdef BOOTINFO_COMPAT_1_0
	case BI_VERSION_MAJOR(COMPAT_ATARI_BOOTI_VERSION):
	    if (!create_compat_bootinfo()) {
		Mfree ((void *)memptr);
		boot_exit (EXIT_FAILURE);
	    }
	    bi_ptr = &compat_bootinfo;
	    bi_size = sizeof(compat_bootinfo);
	    break;
#endif /* BOOTINFO_COMPAT_1_0 */

	default:
	    Mfree ((void *)memptr);
	    boot_exit (EXIT_FAILURE);
    }

    /* copy the boot_info struct to the end of the kernel image */
    memcpy ((void *)(memptr + kernel_size), bi_ptr, bi_size);

    /* read the ramdisk image */
    if (rfd != -1)
    {
	if (lseek (rfd, 0, SEEK_SET) == -1)
	{
	    fprintf (stderr, "Failed to seek to beginning of ramdisk file\n");
	    Mfree ((void *)memptr);
	    boot_exit (EXIT_FAILURE);
	}
	if (read (rfd, memptr + memreq - rd_size,
		  rd_size) != rd_size)
	{
	    fprintf (stderr, "Failed to read ramdisk file\n");
	    Mfree ((void *)memptr);
	    boot_exit (EXIT_FAILURE);
	}
	close (rfd);
    }

    /* for those who want to debug */
    if (debugflag)
    {
	if (bi.ramdisk.size)
	    printf ("RAM disk at %#lx, size is %ld\n",
		    (u_long)(memptr + memreq - rd_size),
		    bi.ramdisk.size);

	if (elf_kernel)
	  {
	    for (i = 0; i < kexec_elf.e_phnum; i++)
	      {
		printf ("Kernel segment %d at %#lx, size %ld\n", i,
			start_mem + kernel_phdrs[i].p_vaddr - PAGE_SIZE,
			kernel_phdrs[i].p_memsz);
	      }
	  }
	else
	  {
	    printf ("\nKernel text at %#lx, code size %d\n",
		    start_mem, kexec.a_text);
	    printf ("Kernel data at %#lx, data size %d\n",
		    start_mem + kexec.a_text, kexec.a_data );
	    printf ("Kernel bss  at %#lx, bss  size %d\n",
		    start_mem + kexec.a_text + kexec.a_data, kexec.a_bss );
	  }
	printf ("\nboot_info is at %#lx\n",
		start_mem + kernel_size);
	printf ("\nKernel entry is %#lx\n",
		elf_kernel ? kexec_elf.e_entry : kexec.a_entry);
	printf ("ramdisk dest top is %#lx\n", bi.ramdisk.addr + rd_size);
	printf ("ramdisk lower limit is %#lx\n",
		(u_long)(memptr + memreq - rd_size));
	printf ("ramdisk src top is %#lx\n", (u_long)(memptr + memreq));

	printf ("Type a key to continue the Linux boot...");
	fflush (stdout);
	getchar();
    }

    printf("Booting Linux...\n");

    sync ();

    /* turn off interrupts... */
    disable_interrupts();

    /* turn off caches... */
    disable_cache();

    /* ..and any MMU translation */
    disable_mmu();

    /* ++guenther: allow reset if launched with MiNT */
    *(long*)0x426 = 0;

    /* copy mover code to a safe place if needed */
    memcpy ((void *) 0x400, &copyall, &copyallend - &copyall);

    /* setup stack */
    change_stack ((void *) PAGE_SIZE);

    /*
     * On the Atari you can have two situations:
     * 1. One piece of contiguous RAM (Falcon)
     * 2. Two pieces of contiguous RAM (TT)
     * In case 2 you can load your program into ST-ram and load your data in
     * any old RAM you have left.
     * In case 1 you could overwrite your own program when copying the
     * kernel and ramdisk to their final positions.
     * To solve this the mover code is copied to a safe place first.
     * Then this program jumps to the mover code. After the mover code
     * has finished it jumps to the start of the kernel in its new position.
     * I thought the memory just after the interrupt vector table was a safe
     * place because it is used by TOS to store some system variables.
     * This range goes from 0x400 to approx. 0x5B0.
     * This is more than enough for the miniscule mover routine (16 bytes).
     */

    jump_to_mover((char *) start_mem, memptr,
		  (char *) bi.ramdisk.addr + rd_size, memptr + memreq,
		  kernel_size + bi_size, rd_size,
		  (void *) 0x400);

    for (;;);
    /* NOTREACHED */
}



#define	MAXARGS		30

static void get_default_args( int *argc, char ***argv )

{	FILE		*f;
	static char	*nargv[MAXARGS];
	char		arg[256], *p;
	int			c, quote, state;

	if (!(f = fopen( "bootargs", "r" )))
		return;
	
	*argc = 1;
	if (***argv)
	  nargv[0] = **argv;
	else
	  nargv[0] = "bootstrap";
	*argv = nargv;

	quote = state = 0;
	p = arg;
	while( (c = fgetc(f)) != EOF ) {		

		if (state == 0) {
			/* outside args, skip whitespace */
			if (!isspace(c)) {
				state = 1;
				p = arg;
			}
		}
		
		if (state) {
			/* inside an arg: copy it into 'arg', obeying quoting */
			if (!quote && (c == '\'' || c == '"'))
				quote = c;
			else if (quote && c == quote)
				quote = 0;
			else if (!quote && isspace(c)) {
				/* end of this arg */
				*p = 0;
				nargv[(*argc)++] = strdup(arg);
				state = 0;
			}
			else
				*p++ = c;
		}
	}
	if (state) {
		/* last arg finished by EOF! */
		*p = 0;
		nargv[(*argc)++] = strdup(arg);
	}
	fclose( f );
	
	nargv[*argc] = 0;
}    


    /*
     *  Create the Bootinfo Structure
     */

static int create_bootinfo(void)
{
    int i;
    struct bi_record *record;

    /* Initialization */
    bi_size = 0;

    /* Generic tags */
    if (!add_bi_record(BI_MACHTYPE, sizeof(bi.machtype), &bi.machtype))
	return(0);
    if (!add_bi_record(BI_CPUTYPE, sizeof(bi.cputype), &bi.cputype))
	return(0);
    if (!add_bi_record(BI_FPUTYPE, sizeof(bi.fputype), &bi.fputype))
	return(0);
    if (!add_bi_record(BI_MMUTYPE, sizeof(bi.mmutype), &bi.mmutype))
	return(0);
    for (i = 0; i < bi.num_memory; i++)
	if (!add_bi_record(BI_MEMCHUNK, sizeof(bi.memory[i]), &bi.memory[i]))
	    return(0);
    if (bi.ramdisk.size)
	if (!add_bi_record(BI_RAMDISK, sizeof(bi.ramdisk), &bi.ramdisk))
	    return(0);
    if (!add_bi_string(BI_COMMAND_LINE, bi.command_line))
	return(0);

    /* Atari tags */
    if (!add_bi_record(BI_ATARI_MCH_COOKIE, sizeof(bi.mch_cookie),
		       &bi.mch_cookie))
	return(0);

    /* Trailer */
    record = (struct bi_record *)((u_long)&bi_union.record+bi_size);
    record->tag = BI_LAST;
    bi_size += sizeof(bi_union.record.tag);

    return(1);
}


    /*
     *  Add a Record to the Bootinfo Structure
     */

static int add_bi_record(u_short tag, u_short size, const void *data)
{
    struct bi_record *record;
    u_short size2;

    size2 = (sizeof(struct bi_record)+size+3)&-4;
    if (bi_size+size2+sizeof(bi_union.record.tag) > MAX_BI_SIZE) {
	fprintf (stderr, "Can't add bootinfo record. Ask a wizard to enlarge me.\n");
	return(0);
    }
    record = (struct bi_record *)((u_long)&bi_union.record+bi_size);
    record->tag = tag;
    record->size = size2;
    memcpy(record->data, data, size);
    bi_size += size2;
    return(1);
}


    /*
     *  Add a String Record to the Bootinfo Structure
     */

static int add_bi_string(u_short tag, const u_char *s)
{
    return add_bi_record(tag, strlen(s)+1, (void *)s);
}


#ifdef BOOTINFO_COMPAT_1_0

    /*
     *  Create the Bootinfo structure for backwards compatibility mode
     */

static int create_compat_bootinfo(void)
{
    u_int i;

    compat_bootinfo.machtype = bi.machtype;
    if (bi.cputype & CPU_68020)
	compat_bootinfo.cputype = COMPAT_CPU_68020;
    else if (bi.cputype & CPU_68030)
	compat_bootinfo.cputype = COMPAT_CPU_68030;
    else if (bi.cputype & CPU_68040)
	compat_bootinfo.cputype = COMPAT_CPU_68040;
    else if (bi.cputype & CPU_68060)
	compat_bootinfo.cputype = COMPAT_CPU_68060;
    else {
	printf("CPU type 0x%08lx not supported by kernel\n", bi.cputype);
	return(0);
    }
    if (bi.fputype & FPU_68881)
	compat_bootinfo.cputype |= COMPAT_FPU_68881;
    else if (bi.fputype & FPU_68882)
	compat_bootinfo.cputype |= COMPAT_FPU_68882;
    else if (bi.fputype & FPU_68040)
	compat_bootinfo.cputype |= COMPAT_FPU_68040;
    else if (bi.fputype & FPU_68060)
	compat_bootinfo.cputype |= COMPAT_FPU_68060;
    else {
	printf("FPU type 0x%08lx not supported by kernel\n", bi.fputype);
	return(0);
    }
    compat_bootinfo.num_memory = bi.num_memory;
    if (compat_bootinfo.num_memory > COMPAT_NUM_MEMINFO) {
	printf("Warning: using only %d blocks of memory\n",
	       COMPAT_NUM_MEMINFO);
	compat_bootinfo.num_memory = COMPAT_NUM_MEMINFO;
    }
    for (i = 0; i < compat_bootinfo.num_memory; i++) {
	compat_bootinfo.memory[i].addr = bi.memory[i].addr;
	compat_bootinfo.memory[i].size = bi.memory[i].size;
    }
    if (bi.ramdisk.size) {
	compat_bootinfo.ramdisk_size = (bi.ramdisk.size+1023)/1024;
	compat_bootinfo.ramdisk_addr = bi.ramdisk.addr;
    } else {
	compat_bootinfo.ramdisk_size = 0;
	compat_bootinfo.ramdisk_addr = 0;
    }
    strncpy(compat_bootinfo.command_line, bi.command_line, COMPAT_CL_SIZE);
    compat_bootinfo.command_line[COMPAT_CL_SIZE-1] = '\0';

    compat_bootinfo.bi_atari.hw_present = 0;
    compat_bootinfo.bi_atari.mch_cookie = bi.mch_cookie;
    return(1);
}
#endif /* BOOTINFO_COMPAT_1_0 */


#ifdef ZKERNEL

#define	ZFILE_CHUNK_BITS	16  /* chunk is 64 KB */
#define	ZFILE_CHUNK_SIZE	(1 << ZFILE_CHUNK_BITS)
#define	ZFILE_CHUNK_MASK	(ZFILE_CHUNK_SIZE-1)
#define	ZFILE_N_CHUNKS		(2*1024*1024/ZFILE_CHUNK_SIZE)

/* variables for storing the uncompressed data */
static char *ZFile[ZFILE_N_CHUNKS];
static int ZFileSize = 0;
static int ZFpos = 0;
static int Zwpos = 0;

static int Zinfd = 0;	     /* fd of compressed file */

/*
 * gzip declarations
 */

#define OF(args)  args

#define memzero(s, n)     memset ((s), 0, (n))

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

#define INBUFSIZ 4096
#define WSIZE 0x8000    /* window size--must be a power of two, and */
			/*  at least 32K for zip's deflate method */

static uch *inbuf;
static uch *window;

static unsigned insize = 0;  /* valid bytes in inbuf */
static unsigned inptr = 0;   /* index of next byte to be processed in inbuf */
static unsigned outcnt = 0;  /* bytes in output buffer */
static int exit_code = 0;
static long bytes_out = 0;

#define get_byte()  (inptr < insize ? inbuf[inptr++] : fill_inbuf())
		
/* Diagnostic functions (stubbed out) */
#define Assert(cond,msg)
#define Trace(x)
#define Tracev(x)
#define Tracevv(x)
#define Tracec(c,x)
#define Tracecv(c,x)

#define STATIC static

static int  fill_inbuf(void);
static void flush_window(void);
static void error(char *m);
static void gzip_mark(void **);
static void gzip_release(void **);

#include "../../../../lib/inflate.c"

static void gzip_mark( void **ptr )
{
}

static void gzip_release( void **ptr )
{
}


/*
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
static int fill_inbuf( void )
{
    if (exit_code)
	return -1;

    insize = ll_read( Zinfd, inbuf, INBUFSIZ );
    if (insize <= 0)
	return -1;

    inptr = 1;
    return( inbuf[0] );
}

/*
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
static void flush_window( void )
{
    ulg c = crc;         /* temporary variable */
    unsigned n;
    uch *in, ch;
    int chunk = Zwpos >> ZFILE_CHUNK_BITS;

    if (chunk >= ZFILE_N_CHUNKS) {
	fprintf( stderr, "compressed image too large! Aborting.\n" );
	boot_exit( EXIT_FAILURE );
    }
    if (!ZFile[chunk]) {
	if (!(ZFile[chunk] = (char *)Malloc( ZFILE_CHUNK_SIZE ))) {
	    fprintf( stderr, "Out of memory for decompresing kernel image\n" );
	    boot_exit( EXIT_FAILURE );
	}
    }
    memcpy( ZFile[chunk] + (Zwpos & ZFILE_CHUNK_MASK), window, outcnt );
    Zwpos += outcnt;
    
#define	DISPLAY_BITS 13
    if ((Zwpos & ((1 << DISPLAY_BITS)-1)) == 0) {
	printf( "." );
	fflush( stdout );
    }
    
    in = window;
    for (n = 0; n < outcnt; n++) {
	    ch = *in++;
	    c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
    }
    crc = c;
    bytes_out += (ulg)outcnt;
    outcnt = 0;
}

static void error( char *x )
{
    fprintf( stderr, "\n%s", x);
    exit_code = 1;
}

static int load_zkernel( int fd )
{
    int i, err;
    
    for( i = 0; i < ZFILE_N_CHUNKS; ++i )
	ZFile[i] = NULL;
    Zinfd = fd;
    ll_lseek( fd, 0, SEEK_SET );
    
    if (!(inbuf = (uch *)Malloc( INBUFSIZ ))) {
	fprintf( stderr, "Couldn't allocate gunzip buffer\n" );
	boot_exit( EXIT_FAILURE );
    }
    if (!(window = (uch *)Malloc( WSIZE ))) {
	fprintf( stderr, "Couldn't allocate gunzip window\n" );
	boot_exit( EXIT_FAILURE );
    }

    printf( "Uncompressing kernel image " );
    fflush( stdout );
    makecrc();
    if (!(err = gunzip()))
	printf( "done\n" );
    ZFileSize = Zwpos;
    ll_close( Zinfd ); /* input file not needed anymore */
    
    Mfree( inbuf );
    Mfree( window );
    return( err );
}

/* Note about the read/lseek wrapper and its memory management: It assumes
 * that all seeks are only forward, and thus data already read or skipped can
 * be freed. This is true for current organization of bootstrap and kernels.
 * Little exception: The struct kexec at the start of the file. After reading
 * it, there may be a seek back to the end of the file. But this currently
 * doesn't hurt. Same considerations apply to the TFTP file buffers. (Roman)
 */

static int kread( int fd, void *buf, unsigned cnt )
{
    unsigned done = 0;
	
    if (!ZFileSize)
	return( ll_read( fd, buf, cnt ) );
    
    if (ZFpos + cnt > ZFileSize)
	cnt = ZFileSize - ZFpos;
    
    while( cnt > 0 ) {
	unsigned chunk = ZFpos >> ZFILE_CHUNK_BITS;
	unsigned endchunk = (chunk+1) << ZFILE_CHUNK_BITS;
	unsigned n = cnt;

	if (ZFpos + n > endchunk)
	    n = endchunk - ZFpos;
	memcpy( buf, ZFile[chunk] + (ZFpos & ZFILE_CHUNK_MASK), n );
	cnt -= n;
	buf += n;
	done += n;
	ZFpos += n;

	if (ZFpos == endchunk) {
	    Mfree( ZFile[chunk] );
	    ZFile[chunk] = NULL;
	}
    }

    return( done );
}


static int klseek( int fd, int where, int whence )
{
    unsigned oldpos, oldchunk, newchunk;

    if (!ZFileSize)
	return( ll_lseek( fd, where, whence ) );

    oldpos = ZFpos;
    switch( whence ) {
      case SEEK_SET:
	ZFpos = where;
	break;
      case SEEK_CUR:
	ZFpos += where;
	break;
      case SEEK_END:
	ZFpos = ZFileSize + where;
	break;
      default:
	return( -1 );
    }
    if (ZFpos < 0) {
	ZFpos = 0;
	return( -1 );
    }
    else if (ZFpos > ZFileSize) {
	ZFpos = ZFileSize;
	return( -1 );
    }

    /* free memory of skipped-over data */
    oldchunk = oldpos >> ZFILE_CHUNK_BITS;
    newchunk = ZFpos  >> ZFILE_CHUNK_BITS;
    while( oldchunk < newchunk ) {
	if (ZFile[oldchunk]) {
	    Mfree( ZFile[oldchunk] );
	    ZFile[oldchunk] = NULL;
	}
	++oldchunk;
    }
    
    return( ZFpos );
}


static void free_zfile( void )
{
    int i;

    for( i = 0; i < ZFILE_N_CHUNKS; ++i )
	if (ZFile[i]) Mfree( ZFile[i] );
}

static int kclose( int fd )
{
    if (ZFileSize) {
	free_zfile();
	return( 0 );
    }
    else
	return( ll_close( fd ) );
}



#endif /* ZKERNEL */

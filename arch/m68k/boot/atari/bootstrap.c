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
**  10 Dec 1995 BOOTP/TFTP support (Roman)
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
#include <asm/setup.h>

/* Atari bootstrap include file */
#include "bootstrap.h"

#define MIN_RAMSIZE     (3)	/* 3 MB */
#define TEMP_STACKSIZE 256

extern char *optarg;
extern int optind;
static void get_default_args( int *argc, char ***argv );
/* This is missing in <unistd.h> */
extern int sync (void);

struct bootinfo bi;
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
		  "moveq	#1,%0\n"
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
    
    if (kernel_major != boots_major) {
	printf("\nThis bootstrap is too %s for this kernel!\n",
	       boots_major < kernel_major ? "old" : "new");
	return 0;
    }
    if (kernel_minor > boots_minor) {
	printf("Warning: Bootinfo version of bootstrap and kernel differ!\n");
	printf("         Certain features may not work.\n");
    }
    return 1;
}


#ifdef USE_BOOTP
# include "bootp.h"
#else
# define kread	read
# define klseek	lseek
# define kclose	close
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
#ifdef USE_BOOTP
    int prefer_bootp = 1, kname_set = 0;
#endif

    ramdisk_name = NULL;
    kernel_name = "vmlinux";

    /* print the startup message */
    puts("\fLinux/68k Atari Bootstrap version 1.8"
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
#ifdef USE_BOOTP
    while ((ch = getopt(argc, argv, "bdtsk:r:")) != EOF)
#else
    while ((ch = getopt(argc, argv, "dtsk:r:")) != EOF)
#endif
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
#ifdef USE_BOOTP
	    kname_set = 1;
#endif
	    break;
	  case 'r':
	    ramdisk_name = optarg;
	    break;
#ifdef USE_BOOTP
	  case 'b':
	    prefer_bootp = 0;
	    break;
#endif
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
      case 20: bi.cputype = CPU_68020; break;
      case 30: bi.cputype = CPU_68030; break;
      case 40: bi.cputype = CPU_68040; break;
      case 60: bi.cputype = CPU_68060; break;
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
		bi.cputype |= FPU_68040;
		puts( "68040\n" );
	}
	else if (cpu_type == 60) {
		bi.cputype |= FPU_68060;
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
			bi.cputype |= FPU_68881;
			puts("68881\n");
			break;
		  case 6:
		  m68882:
			bi.cputype |= FPU_68882;
			puts("68882\n");
			break;
		  default:
			puts("Unknown FPU type. Assuming no FPU.");
			break;
		}
	}
	/* ++roman: If an FPU was announced in the cookie, test
	   whether it is a real hardware FPU or a software emulator!  */
	if (bi.cputype & FPU_MASK) {
		if (test_software_fpu()) {
			bi.cputype &= ~FPU_MASK;
			puts("FPU: software emulated. Assuming no FPU.");
		}
	}

    memset(&bi.bi_atari.hw_present, 0, sizeof(bi.bi_atari.hw_present));

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
    bi.bi_atari.mch_cookie = mch_type;
    
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

#ifdef USE_BOOTP
    kfd = -1;
    if (prefer_bootp) {
	/* First try to get a remote kernel, then use a local kernel (if
	 * present) */
	if (get_remote_kernel( kname_set ? kernel_name : NULL ) < 0) {
	    printf( "\nremote boot failed; trying local kernel\n" );
	    if ((kfd = open (kernel_name, O_RDONLY)) == -1) {
		fprintf (stderr, "Unable to open kernel file %s\n",
			 kernel_name);
		boot_exit (EXIT_FAILURE);
	    }
	}
    }
    else {
	/* Try BOOTP if local kernel cannot be opened */
	if ((kfd = open (kernel_name, O_RDONLY)) == -1) {
	    printf( "\nlocal kernel failed; trying remote boot\n" );
	    if (get_remote_kernel( kname_set ? kernel_name : NULL ) < 0) {
		fprintf (stderr, "Unable to remote boot and "
			 "to open kernel file %s\n", kernel_name);
		boot_exit (EXIT_FAILURE);
	    }
	}
    }
#else
    /* open kernel executable and read exec header */
    if ((kfd = open (kernel_name, O_RDONLY)) == -1) {
	fprintf (stderr, "Unable to open kernel file %s\n", kernel_name);
	boot_exit (EXIT_FAILURE);
    }
#endif

    if (kread (kfd, (void *)&kexec, sizeof(kexec)) != sizeof(kexec))
    {
	fprintf (stderr, "Unable to read exec header from %s\n", kernel_name);
	boot_exit (EXIT_FAILURE);
    }

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
	bi.ramdisk_size = (lseek(rfd, 0, SEEK_END) + 1023) / 1024;
    }
    else
	bi.ramdisk_size = 0;

    rd_size = bi.ramdisk_size << 10;
    if (mem_size - rd_size < MB && bi.num_memory > 1)
      /* If running low on ST ram load ramdisk into alternate ram.  */
      bi.ramdisk_addr = (u_long) bi.memory[1].addr + bi.memory[1].size - rd_size;
    else
      /* Else hopefully there is enough ST ram. */
      bi.ramdisk_addr = (u_long)start_mem + mem_size - rd_size;

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
    memreq = kernel_size + sizeof (bi);
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
    if (!check_bootinfo_version(memptr)) {
	Mfree ((void *)memptr);
	boot_exit (EXIT_FAILURE);
    }
    
    /* copy the boot_info struct to the end of the kernel image */
    memcpy ((void *)(memptr + kernel_size),
	    &bi, sizeof(bi));

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
	if (bi.ramdisk_size)
	    printf ("RAM disk at %#lx, size is %ldK\n",
		    (u_long)(memptr + memreq - rd_size),
		    bi.ramdisk_size);

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
	printf ("ramdisk dest top is %#lx\n", bi.ramdisk_addr + rd_size);
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
		  (char *) bi.ramdisk_addr + rd_size, memptr + memreq,
		  kernel_size + sizeof (bi),
		  rd_size,
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


/*
** bootstrap.c -- This program loads the Linux/68k kernel into an Amiga
**                and and launches it.
**
** Copyright 1993,1994 by Hamish Macdonald, Greg Harp
**
** Modified 11-May-94 by Geert Uytterhoeven
**                      (Geert.Uytterhoeven@cs.kuleuven.ac.be)
**     - A3640 MapROM check
** Modified 31-May-94 by Geert Uytterhoeven
**     - Memory thrash problem solved
** Modified 07-March-95 by Geert Uytterhoeven
**     - Memory block sizes are rounded to a multiple of 256K instead of 1M
**       This _requires_ >0.9pl5 to work!
**       (unless all block sizes are multiples of 1M :-)
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file README.legal in the main directory of this archive
** for more details.
**
*/

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

/* Amiga bootstrap include file */
#include "bootstrap.h"

/* required Linux/68k include files */
#include <linux/a.out.h>
#include <asm/bootinfo.h>

/* temporary stack size */
#define TEMP_STACKSIZE	256

/* Exec Base */
extern struct ExecBase *SysBase;

extern char *optarg;

struct exec kexec;
char *memptr;
u_long start_mem;
u_long mem_size;
u_long rd_size;

struct ExpansionBase *ExpansionBase;
struct GfxBase *GfxBase;

struct bootinfo bi;
u_long bi_size = sizeof bi;

caddr_t CustomBase = (caddr_t)CUSTOM_PHYSADDR;

void usage(void)
{
	fprintf (stderr, "Usage:\n"
		 "\tbootstrap [-d] [-k kernel_executable] [-r ramdisk_file]"
		 " [option...]\n");
	exit (EXIT_FAILURE);
}

/*
 * This assembler code is copied to chip ram, and
 * then executed.
 * It copies the kernel (and ramdisk) to their
 * final resting place.
 */
#ifndef __GNUC__
#error GNU CC is required to compile the bootstrap program
#endif
asm("
.text
.globl _copyall, _copyallend
_copyall:
				| /* put variables in registers because they may */
	lea	_kexec,a3	| /* be overwritten by kernel/ramdisk copy!! - G.U. */
	movel	_memptr,a4
	movel	_start_mem,a5
	movel	_mem_size,d0
	movel	_rd_size,d1
	movel	_bi_size,d5
	movel	a3@(4),d2	| kexec.a_text
	movel	a3@(8),d3	| kexec.a_data
	movel	a3@(12),d4	| kexec.a_bss

				| /* copy kernel text and data */
	movel	a4,a0		| src = (u_long *)memptr;
	movel	a0,a2		| limit = (u_long *)(memptr + kexec.a_text + kexec.a_data);
	addl	d2,a2
	addl	d3,a2
	movel	a5,a1		| dest = (u_long *)start_mem;
1:	cmpl	a0,a2
	beqs	2f		| while (src < limit)
	moveb	a0@+,a1@+	|	*dest++ = *src++;
	bras	1b
2:

				| /* clear kernel bss */
	movel	a1,a0		| dest = (u_long *)(start_mem + kexec.a_text + kexec.a_data);
	movel	a1,a2		| limit = dest + kexec.a_bss / sizeof(u_long);
	addl	d4,a2
1:	cmpl	a0,a2
	beqs	2f		| while (dest < limit)
	clrb	a0@+		|	*dest++ = 0;
	bras	1b
2:

				| /* copy bootinfo to end of bss */
	movel	a4,a1		| src = (u long *)memptr + kexec.a_text + kexec.a_data);
	addl	d2,a1
	addl	d3,a1		| dest = end of bss (already in a0)
	movel   d5,d7		| count = sizeof bi
	subql	#1,d7
1:	moveb	a1@+,a0@+	| while (--count > -1)
	dbra	d7,1b		|	*dest++ = *src++
	

				| /* copy the ramdisk to the top of memory (from back to front) */
	movel	a5,a1		| dest = (u_long *)(start_mem + mem_size);
	addl	d0,a1
	movel	a4,a2		| limit = (u_long *)(memptr + kexec.a_text + kexec.a_data + sizeof bi);
	addl	d2,a2
	addl	d3,a2
        addl    d5,a2
	movel	a2,a0		| src = (u_long *)((u_long)limit + rd_size);
	addl	d1,a0
1:	cmpl	a0,a2
	beqs	2f		| while (src > limit)
	moveb	a0@-,a1@-	| 	*--dest = *--src;
	bras	1b
2:
				| /* jump to start of kernel */
	movel	a5,a0		| jump_to (START_MEM);
	jsr	a0@
_copyallend:
");

asm("
.text
.globl _maprommed
_maprommed:
	oriw	#0x0700,sr
	moveml	#0x3f20,sp@-
/* Save cache settings */
	.long 	0x4e7a1002	/* movec cacr,d1 */
/* Save MMU settings */
	.long 	0x4e7a2003	/* movec tc,d2 */
	.long 	0x4e7a3004	/* movec itt0,d3 */
	.long 	0x4e7a4005	/* movec itt1,d4 */
	.long 	0x4e7a5006	/* movec dtt0,d5 */
	.long 	0x4e7a6007	/* movec dtt1,d6 */
	moveq	#0,d0
	movel	d0,a2
/* Disable caches */
	.long 	0x4e7b0002	/* movec d0,cacr */
/* Disable MMU */
	.long 	0x4e7b0003	/* movec d0,tc */
	.long 	0x4e7b0004	/* movec d0,itt0 */
	.long 	0x4e7b0005	/* movec d0,itt1 */
	.long 	0x4e7b0006	/* movec d0,dtt0 */
	.long 	0x4e7b0007	/* movec d0,dtt1 */
	lea	0x07f80000,a0
	lea	0x00f80000,a1
	movel	a0@,d7
	cmpl	a1@,d7
	jnes	1f
	movel	d7,d0
	notl	d0
	movel	d0,a0@
	nop
	cmpl	a1@,d0
	jnes	1f
/* MapROMmed A3640 present */
	moveq	#-1,d0
	movel	d0,a2
1:	movel	d7,a0@
/* Restore MMU settings */
	.long 	0x4e7b2003	/* movec d2,tc */
	.long 	0x4e7b3004	/* movec d3,itt0 */
	.long 	0x4e7b4005	/* movec d4,itt1 */
	.long 	0x4e7b5006	/* movec d5,dtt0 */
	.long 	0x4e7b6007	/* movec d6,dtt1 */
/* Restore cache settings */
	.long 	0x4e7b1002	/* movec d1,cacr */
	movel	a2,d0
	moveml	sp@+,#0x04fc
	rte
");

extern unsigned long maprommed();


extern char copyall, copyallend;

int main(int argc, char *argv[])
{
	int ch, debugflag = 0, kfd, rfd = -1, i;
	long fast_total = 0;	     /* total Fast RAM in system */
	struct MemHeader *mnp;
	struct ConfigDev *cdp = NULL;
	char *kernel_name = "vmlinux";
	char *ramdisk_name = NULL;
	char *memfile = NULL;
	u_long memreq;
	void (*startfunc)(void);
	long startcodesize;
	u_long *stack, text_offset;
	unsigned char *rb3_reg = NULL, *piccolo_reg = NULL, *sd64_reg = NULL;

	/* print the greet message */
	puts("Linux/68k Amiga Bootstrap version 1.11");
	puts("Copyright 1993,1994 by Hamish Macdonald and Greg Harp\n");

	/* machine is Amiga */
	bi.machtype = MACH_AMIGA;

	/* check arguments */
	while ((ch = getopt(argc, argv, "dk:r:m:")) != EOF)
		switch (ch) {
		    case 'd':
			debugflag = 1;
			break;
		    case 'k':
			kernel_name = optarg;
			break;
		    case 'r':
			ramdisk_name = optarg;
			break;
		    case 'm':
			memfile = optarg;
			break;
		    case '?':
		    default:
			usage();
		}
	argc -= optind;
	argv += optind;

	SysBase = *(struct ExecBase **)4;

	/* Memory & AutoConfig based on 'unix_boot.c' by C= */

	/* open Expansion Library */
	ExpansionBase = (struct ExpansionBase *)OpenLibrary("expansion.library", 36);
	if (!ExpansionBase) {
		puts("Unable to open expansion.library V36 or greater!  Aborting...");
		exit(EXIT_FAILURE);
	}

	/* find all of the autoconfig boards in the system */
	cdp = (struct ConfigDev *)FindConfigDev(cdp, -1, -1);
	for (i=0; (i < NUM_AUTO) && cdp; i++) {
		/* copy the contents of each structure into our boot info */
		memcpy(&bi.bi_amiga.autocon[i], cdp, sizeof(struct ConfigDev));

		/* count this device */
		bi.bi_amiga.num_autocon++;

		/* get next device */
		cdp = (struct ConfigDev *)FindConfigDev(cdp, -1, -1);
	}

	/* find out the memory in the system */
	for (mnp = (struct MemHeader *)SysBase->MemList.l_head;
	     (bi.num_memory < NUM_MEMINFO) && mnp->mh_Node.ln_Succ;
	     mnp = (struct MemHeader *)mnp->mh_Node.ln_Succ)
	{
		struct MemHeader mh;

		/* copy the information */
		mh = *mnp;

		/* if we suspect that Kickstart is shadowed in an A3000,
		   modify the entry to show 512K more at the top of RAM
		   Check first for a MapROMmed A3640 board: overwriting the
		   Kickstart image causes an infinite lock-up on reboot! */

		if (mh.mh_Upper == (void *)0x07f80000)
			if ((SysBase->AttnFlags & AFF_68040) && Supervisor(maprommed))
				printf("A3640 MapROM detected.\n");
			else {
				mh.mh_Upper = (void *)0x08000000;
				printf("A3000 shadowed Kickstart detected.\n");
			}

		/* if we suspect that Kickstart is zkicked,
		   modify the entry to show 512K more at the bottom of RAM */
		if (mh.mh_Lower == (void *)0x00280020) {
		    mh.mh_Lower =  (void *)0x00200000;
		    printf("ZKick detected.\n");
		}

		/*
		 * If this machine has "LOCAL" memory between 0x07000000
		 * and 0x080000000, then we'll call it an A3000.
		 */
		if (mh.mh_Lower >= (void *)0x07000000 &&
		    mh.mh_Lower <  (void *)0x08000000 &&
		    (mh.mh_Attributes & MEMF_LOCAL))
			bi.bi_amiga.model = AMI_3000;

		/* mask the memory limit values */
		mh.mh_Upper = (void *)((u_long)mh.mh_Upper & 0xfffff000);
		mh.mh_Lower = (void *)((u_long)mh.mh_Lower & 0xfffff000);

		/* if fast memory */
		if (mh.mh_Attributes & MEMF_FAST) {
			unsigned long size;

			/* record the start */
			bi.memory[bi.num_memory].addr = (u_long)mh.mh_Lower;

			/* set the size value to the size of this block */
			size = (u_long)mh.mh_Upper - (u_long)mh.mh_Lower;

			/* mask off to a 256K increment */
			size &= 0xfffc0000;

			fast_total += size;

			if (size > 0)
				/* count this block */
				bi.memory[bi.num_memory++].size  = size;

		} else if (mh.mh_Attributes & MEMF_CHIP) {
			/* if CHIP memory, record the size */
			bi.bi_amiga.chip_size =
				(u_long)mh.mh_Upper; /* - (u_long)mh.mh_Lower; */
		}
	}

	CloseLibrary((struct Library *)ExpansionBase);

	/*
	 * if we have a memory file, read the memory information from it
	 */
	if (memfile) {
	    FILE *fp;
	    int i;

	    if ((fp = fopen (memfile, "r")) == NULL) {
		perror ("open memory file");
		fprintf (stderr, "Cannot open memory file %s\n", memfile);
		exit (EXIT_FAILURE);
	    }

	    if (fscanf (fp, "%lu", &bi.bi_amiga.chip_size) != 1) {
		fprintf (stderr, "memory file does not contain chip memory size\n");
		fclose (fp);
		exit (EXIT_FAILURE);
	    }
		
	    for (i = 0; i < NUM_MEMINFO; i++) {
		if (fscanf (fp, "%lx %lu", &bi.memory[i].addr,
			    &bi.memory[i].size) != 2)
		    break;
	    }

	    fclose (fp);

	    if (i != bi.num_memory && i > 0)
		bi.num_memory = i;
	}

	/* get info from ExecBase */
	bi.bi_amiga.vblank = SysBase->VBlankFrequency;
	bi.bi_amiga.psfreq = SysBase->PowerSupplyFrequency;
	bi.bi_amiga.eclock = SysBase->EClockFrequency;

	/* open graphics library */
	GfxBase = (struct GfxBase *)OpenLibrary ("graphics.library", 0);

	/* determine chipset */
	bi.bi_amiga.chipset = CS_STONEAGE;
	if(GfxBase)
	{
	    if(GfxBase->ChipRevBits0 & GFXG_AGA)
	    {
		bi.bi_amiga.chipset = CS_AGA;
		/*
		 *  we considered this machine to be an A3000 because of its
		 *  local memory just beneath $8000000; now if it has AGA, it
		 *  must be an A4000
		 *  except the case no RAM is installed on the motherboard but
		 *  on an additional card like FastLane Z3 or on the processor
		 *  board itself. Gotta check this out.
		 */
		bi.bi_amiga.model =
		    (bi.bi_amiga.model == AMI_3000) ? AMI_4000 : AMI_1200;
	    }
	    else if(GfxBase->ChipRevBits0 & GFXG_ECS)
		bi.bi_amiga.chipset = CS_ECS;
	    else if(GfxBase->ChipRevBits0 & GFXG_OCS)
		bi.bi_amiga.chipset = CS_OCS;
	}

	/* Display amiga model */
	switch (bi.bi_amiga.model) {
	    case AMI_UNKNOWN:
		break;
	    case AMI_500:
		printf ("Amiga 500 ");
		break;
	    case AMI_2000:
		printf ("Amiga 2000 ");
		break;
	    case AMI_3000:
		printf ("Amiga 3000 ");
		break;
	    case AMI_4000:
		printf ("Amiga 4000 ");
		break;
	    case AMI_1200:		/* this implies an upgraded model   */
		printf ("Amiga 1200 "); /* equipped with at least 68030 !!! */
		break;
	}

	/* display and set the CPU <type */
	printf("CPU: ");
	if (SysBase->AttnFlags & AFF_68040) {
		printf("68040");
		bi.cputype = CPU_68040;
		if (SysBase->AttnFlags & AFF_FPU40) {
			printf(" with internal FPU");
			bi.cputype |= FPU_68040;
		} else
			printf(" without FPU");
	} else {
		if (SysBase->AttnFlags & AFF_68030) {
			printf("68030");
			bi.cputype = CPU_68030;
		} else if (SysBase->AttnFlags & AFF_68020) {
			printf("68020 (Do you have an MMU?)");
			bi.cputype = CPU_68020;
		} else {
			puts("Insufficient for Linux.  Aborting...");
			printf("SysBase->AttnFlags = %#x\n", SysBase->AttnFlags);
			exit (EXIT_FAILURE);
		}
		if (SysBase->AttnFlags & AFF_68882) {
			printf(" with 68882 FPU");
			bi.cputype |= FPU_68882;
		} else if (SysBase->AttnFlags & AFF_68881) {
			printf(" with 68881 FPU");
			bi.cputype |= FPU_68881;
		} else
			printf(" without FPU");
	}

	switch(bi.bi_amiga.chipset)
	{
	    case CS_STONEAGE:
		printf(", old or unknown chipset");
		break;
	    case CS_OCS:
		printf(", OCS");
		break;
	    case CS_ECS:
		printf(", ECS");
		break;
	    case CS_AGA:
		printf(", AGA chipset");
		break;
	}

	putchar ('\n');
	putchar ('\n');

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

	/* display the clock statistics */
	printf("Vertical Blank Frequency: %dHz\nPower Supply Frequency: %dHz\n",
	       bi.bi_amiga.vblank, bi.bi_amiga.psfreq);
	printf("EClock Frequency: %7.5fKHz\n\n",
	       (float)bi.bi_amiga.eclock / 1000);

	/* display autoconfig devices */
	if (bi.bi_amiga.num_autocon) {
		printf("Found %d AutoConfig Device%s", bi.bi_amiga.num_autocon,
		       (bi.bi_amiga.num_autocon > 1)?"s\n":"\n");
		for (i=0; i<bi.bi_amiga.num_autocon; i++)
		{
			printf("Device %d: addr = %08lx\n", i,
			       (u_long)bi.bi_amiga.autocon[i].cd_BoardAddr);
			/* check for a Rainbow 3 and prepare to reset it if there is one */
			if ( (bi.bi_amiga.autocon[i].cd_Rom.er_Manufacturer == MANUF_HELFRICH1) &&
				 (bi.bi_amiga.autocon[i].cd_Rom.er_Product == PROD_RAINBOW3) )
			{
				printf("(Found a Rainbow 3 board - will reset it at kernel boot time)\n");
				rb3_reg = (unsigned char *)(bi.bi_amiga.autocon[i].cd_BoardAddr + 0x01002000);
			}

			/* check for a Piccolo and prepare to reset it if there is one */
			if ( (bi.bi_amiga.autocon[i].cd_Rom.er_Manufacturer == MANUF_HELFRICH2) &&
				 (bi.bi_amiga.autocon[i].cd_Rom.er_Product == PROD_PICCOLO_REG) )
			{
				printf("(Found a Piccolo board - will reset it at kernel boot time)\n");
				piccolo_reg = (unsigned char *)(bi.bi_amiga.autocon[i].cd_BoardAddr + 0x8000);
			}

			/* check for a SD64 and prepare to reset it if there is one */
			if ( (bi.bi_amiga.autocon[i].cd_Rom.er_Manufacturer == MANUF_HELFRICH2) &&
				 (bi.bi_amiga.autocon[i].cd_Rom.er_Product == PROD_SD64_REG) )
			{
				printf("(Found a SD64 board - will reset it at kernel boot time)\n");
				sd64_reg = (unsigned char *)(bi.bi_amiga.autocon[i].cd_BoardAddr + 0x8000);
			}

			/* what this code lacks - what if there are several boards of  */
			/* the same brand ? In that case I should reset them one after */
			/* the other, which is currently not done - a rare case...FN   */
			/* ok, MY amiga currently hosts all three of the above boards ;-) */
		}
	} else
		puts("No AutoConfig Devices Found");

	/* display memory */
	if (bi.num_memory) {
		printf("\n%d Block%sof Memory Found\n", bi.num_memory,
		       (bi.num_memory > 1)?"s ":" ");
		for (i=0; i<bi.num_memory; i++) {
			printf("Block %d: %08lx to %08lx (%ldKB)\n",
			       i, bi.memory[i].addr,
			       bi.memory[i].addr + bi.memory[i].size,
			       bi.memory[i].size >> 10);
		}
	} else {
		puts("No memory found?!  Aborting...");
		exit(10);
	}

	/* display chip memory size */
	printf ("%ldK of CHIP memory\n", bi.bi_amiga.chip_size >> 10);

	start_mem = bi.memory[0].addr;
	mem_size = bi.memory[0].size;

	/* tell us where the kernel will go */
	printf("\nThe kernel will be located at %08lx\n", start_mem);

	/* verify that there is enough Chip RAM */
	if (bi.bi_amiga.chip_size < 512*1024) {
		puts("\nNot enough Chip RAM in this system.  Aborting...");
		exit(10);
	}

	/* verify that there is enough Fast RAM */
	if (fast_total < 2*1024*1024) {
		puts("\nNot enough Fast RAM in this system.  Aborting...");
		exit(10);
	}

	/* open kernel executable and read exec header */
	if ((kfd = open (kernel_name, O_RDONLY)) == -1) {
		fprintf (stderr, "Unable to open kernel file %s\n", kernel_name);
		exit (EXIT_FAILURE);
	}

	if (read (kfd, (void *)&kexec, sizeof(kexec)) != sizeof(kexec)) {
		fprintf (stderr, "Unable to read exec header from %s\n",
			 kernel_name);
		exit (EXIT_FAILURE);
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
	    fprintf (stderr, "Wrong magic number %lo in kernel header\n",
		     N_MAGIC(kexec));
	    exit (EXIT_FAILURE);
	}

	/* Load the kernel at one page after start of mem */
	start_mem += PAGE_SIZE;
	mem_size -= PAGE_SIZE;
	/* Align bss size to multiple of four */
	kexec.a_bss = (kexec.a_bss + 3) & ~3;

	if (ramdisk_name) {
		if ((rfd = open (ramdisk_name, O_RDONLY)) == -1) {
			fprintf (stderr, "Unable to open ramdisk file %s\n",
				 ramdisk_name);
			exit (EXIT_FAILURE);
		}
		/* record ramdisk size */
		bi.ramdisk_size = (lseek (rfd, 0, L_XTND) + 1023) >> 10;
	} else
		bi.ramdisk_size = 0;

	rd_size = bi.ramdisk_size << 10;
	bi.ramdisk_addr = (u_long)start_mem + mem_size - rd_size;

	memreq = kexec.a_text + kexec.a_data + sizeof(bi) + rd_size;
	if (!(memptr = (char *)AllocMem (memreq, MEMF_FAST | MEMF_CLEAR))) {
		fprintf (stderr, "Unable to allocate memory\n");
		exit (EXIT_FAILURE);
	}

	if (lseek (kfd, text_offset, L_SET) == -1) {
		fprintf (stderr, "Failed to seek to text\n");
		FreeMem ((void *)memptr, memreq);
		exit (EXIT_FAILURE);
	}
	if (read (kfd, memptr, kexec.a_text) != kexec.a_text) {
		fprintf (stderr, "Failed to read text\n");
		FreeMem ((void *)memptr, memreq);
		exit (EXIT_FAILURE);
	}

	/* data follows immediately after text */
	if (read (kfd, memptr + kexec.a_text, kexec.a_data) != kexec.a_data) {
		fprintf (stderr, "Failed to read data\n");
		FreeMem ((void *)memptr, memreq);
		exit (EXIT_FAILURE);
	}
	close (kfd);

	/* copy the boot_info struct to the end of the kernel image */
	memcpy ((void *)(memptr + kexec.a_text + kexec.a_data), &bi,
		sizeof(bi));

	if (rfd != -1) {
		if (lseek (rfd, 0, L_SET) == -1) {
			fprintf (stderr, "Failed to seek to beginning of ramdisk file\n");
			FreeMem ((void *)memptr, memreq);
			exit (EXIT_FAILURE);
		}
		if (read (rfd, memptr + kexec.a_text + kexec.a_data 
			  + sizeof(bi), rd_size) != rd_size) {
			fprintf (stderr, "Failed to read ramdisk file\n");
			FreeMem ((void *)memptr, memreq);
			exit (EXIT_FAILURE);
		}
		close (rfd);
	}

	/* allocate temporary chip ram stack */
	stack = (u_long *)AllocMem( TEMP_STACKSIZE, MEMF_CHIP|MEMF_CLEAR);
	if (!stack) {
		fprintf (stderr, "Unable to allocate memory for stack\n");
		FreeMem ((void *)memptr, memreq);
		exit (EXIT_FAILURE);
	}

	/* allocate chip ram for copy of startup code */
	startcodesize = &copyallend - &copyall;
	startfunc = (void (*)(void))AllocMem( startcodesize, MEMF_CHIP);
	if (!startfunc) {
		fprintf (stderr, "Unable to allocate memory for code\n");
		FreeMem ((void *)memptr, memreq);
		FreeMem ((void *)stack, TEMP_STACKSIZE);
		exit (EXIT_FAILURE);
	}

	/* copy startup code to CHIP RAM */
	memcpy (startfunc, &copyall, startcodesize);

	if (debugflag) {
		if (bi.ramdisk_size)
			printf ("RAM disk at %#lx, size is %ldK\n",
				(u_long)memptr + kexec.a_text + kexec.a_data,
				bi.ramdisk_size);

		printf ("\nKernel text at %#lx, code size %x\n",
			start_mem, kexec.a_text);
		printf ("Kernel data at %#lx, data size %x\n",
			start_mem + kexec.a_text, kexec.a_data );
		printf ("Kernel bss  at %#lx, bss  size %x\n",
			start_mem + kexec.a_text + kexec.a_data,
			kexec.a_bss );
		printf ("boot info at %#lx\n", start_mem + kexec.a_text
			+ kexec.a_data + kexec.a_bss);

		printf ("\nKernel entry is %#x\n", kexec.a_entry );

		printf ("ramdisk dest top is %#lx\n", start_mem + mem_size);
		printf ("ramdisk lower limit is %#lx\n",
			(u_long)(memptr + kexec.a_text + kexec.a_data));
		printf ("ramdisk src top is %#lx\n",
			(u_long)(memptr + kexec.a_text + kexec.a_data)
			+ rd_size);

		printf ("Type a key to continue the Linux boot...");
		fflush (stdout);
		getchar();
	}

	/* wait for things to settle down */
	sleep(2);

	/* FN: If a Rainbow III board is present, reset it to disable */
	/* its (possibly activated) vertical blank interrupts as the */
	/* kernel is not yet prepared to handle them (level 6). */
	if (rb3_reg != NULL)
	{
		/* set RESET bit in special function register */
		*rb3_reg = 0x01;
		/* actually, only a few cycles delay are required... */
		sleep(1);
		/* clear reset bit */
		*rb3_reg = 0x00;
	}

	/* the same stuff as above, for the Piccolo board. */
	/* this also has the side effect of resetting the board's */
	/* output selection logic to use the Amiga's display in single */
	/* monitor systems - which is currently what we want. */
	if (piccolo_reg != NULL)
	{
		/* set RESET bit in special function register */
		*piccolo_reg = 0x01;
		/* actually, only a few cycles delay are required... */
		sleep(1);
		/* clear reset bit */
		*piccolo_reg = 0x51;
	}

	/* the same stuff as above, for the SD64 board. */
	/* just as on the Piccolo, this also resets the monitor switch */
	if (sd64_reg != NULL)
	{
		/* set RESET bit in special function register */
		*sd64_reg = 0x1f;
		/* actually, only a few cycles delay are required... */
		sleep(1);
	/* clear reset bit AND switch monitor bit (0x20) */
	*sd64_reg = 0x4f;
	}

	if (GfxBase) {
		/* set graphics mode to a nice normal one */
		LoadView (NULL);
		CloseLibrary ((struct Library *)GfxBase);
	}

	Disable();

	/* Turn off all DMA */
	custom.dmacon = DMAF_ALL | DMAF_MASTER;

	/* turn off caches */
	CacheControl (0L, ~0L);

	/* Go into supervisor state */
	SuperState ();

	/* setup stack */
	change_stack ((char *) stack + TEMP_STACKSIZE);

	/* turn off any mmu translation */
	disable_mmu ();

	/* execute the copy-and-go code (from CHIP RAM) */
	startfunc();

	/* NOTREACHED */
}

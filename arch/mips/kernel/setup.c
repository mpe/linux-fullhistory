/*
 *  linux/arch/mips/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 1995, 1996  Ralf Baechle
 *  Copyright (C) 1996  Stoned Elipot
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/utsname.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#ifdef CONFIG_RTC
#include <linux/ioport.h>
#include <linux/timex.h>
#endif

#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/io.h>
#include <asm/vector.h>
#include <asm/stackframe.h>
#include <asm/system.h>
#ifdef CONFIG_SGI
#include <asm/sgialib.h>
#endif

/*
 * How to handle the machine's features
 */
struct feature *feature;

/*
 * What to do to keep the caches consistent with memory
 * We don't use the normal cacheflush routine to keep Tyne caches happier.
 */
void (*fd_cacheflush)(const void *addr, size_t size);

/*
 * Not all of the MIPS CPUs have the "wait" instruction available.  This
 * is set to true if it is available.  The wait instruction stops the
 * pipeline and reduces the power consumption of the CPU very much.
 */
char wait_available;

/*
 * There are several bus types available for MIPS machines.  "RISC PC"
 * type machines have ISA, EISA, VLB or PCI available, DECstations
 * have Turbochannel or Q-Bus, SGI has GIO, there are lots of VME
 * boxes ...
 * This flag is set if a EISA slots are available.
 */
int EISA_bus = 0;

/*
 * Milo passes some information to the kernel that looks like as if it
 * had been returned by a Intel PC BIOS.  Milo doesn't fill the passed
 * drive_info and Linux can find out about this anyway, so I'm going to
 * remove this sometime.  screen_info contains information about the 
 * resolution of the text screen.  For VGA graphics based machine this
 * information is being use to continue the screen output just below
 * the BIOS printed text and with the same text resolution.
 */
struct drive_info_struct drive_info = DEFAULT_DRIVE_INFO;
struct screen_info screen_info = DEFAULT_SCREEN_INFO;

/*
 * setup informations
 *
 * These are intialized so they are in the .data section
 */
unsigned long mips_memory_upper = KSEG0; /* this is set by kernel_entry() */
unsigned long mips_cputype = CPU_UNKNOWN;
unsigned long mips_machtype = MACH_UNKNOWN;
unsigned long mips_machgroup = MACH_GROUP_UNKNOWN;
unsigned long mips_tlb_entries = 48; /* Guess which CPU I've got :) */
unsigned long mips_vram_base = KSEG0;

unsigned char aux_device_present;
extern int root_mountflags;
extern int _end;

extern char empty_zero_page[PAGE_SIZE];

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	empty_zero_page
#if 0
#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
#endif
#define LOADER_TYPE (*(unsigned char *) (PARAM+0x210))
#define KERNEL_START (*(unsigned long *) (PARAM+0x214))
#define INITRD_START (*(unsigned long *) (PARAM+0x218))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x21c))

static char command_line[CL_SIZE] = { 0, };
       char saved_command_line[CL_SIZE];

/*
 * The board specific setup routine sets irq_setup to point to a board
 * specific setup routine.
 */
void (*irq_setup)(void);

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is is mapped
 * for the processor.
 */
unsigned long isa_slot_offset;

__initfunc(static void default_irq_setup(void))
{
	panic("Unknown machtype in init_IRQ");
}

__initfunc(static void default_fd_cacheflush(const void *addr, size_t size))
{
}

__initfunc(void setup_arch(char **cmdline_p,
           unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	unsigned long memory_end;
	tag* atag;
	void decstation_setup(void);
	void deskstation_setup(void);
	void jazz_setup(void);
	void sni_rm200_pci_setup(void);
	void sgi_setup(void);

	/* Perhaps a lot of tags are not getting 'snarfed' - */
	/* please help yourself */

	atag = bi_TagFind(tag_machtype);
	memcpy(&mips_machtype, TAGVALPTR(atag), atag->size);

	atag = bi_TagFind(tag_machgroup);
	memcpy(&mips_machgroup, TAGVALPTR(atag), atag->size);

	atag = bi_TagFind(tag_vram_base);
	memcpy(&mips_vram_base, TAGVALPTR(atag), atag->size);

	irq_setup = default_irq_setup;
	fd_cacheflush = default_fd_cacheflush;

	switch(mips_machgroup)
	{
#ifdef CONFIG_MIPS_DECSTATION
	case MACH_GROUP_DEC:
		decstation_setup();
		break;
#endif
#if defined(CONFIG_MIPS_ARC) 
/* Perhaps arch/mips/deskstation should be renommed arch/mips/arc.
 * For now CONFIG_MIPS_ARC means DeskStation. -Stoned.
 */
	case MACH_GROUP_ARC:
		deskstation_setup();
		break;
#endif
#ifdef CONFIG_MIPS_JAZZ
	case MACH_GROUP_JAZZ:
		jazz_setup();
		break;
#endif
#ifdef CONFIG_SGI
	case MACH_GROUP_SGI:
		sgi_setup();
		break;
#endif
#ifdef CONFIG_SNI_RM200_PCI
	case MACH_GROUP_SNI_RM:
		sni_rm200_pci_setup();
		break;
#endif
	default:
		panic("Unsupported architecture");
	}

	atag = bi_TagFind(tag_drive_info);
	memcpy(&drive_info, TAGVALPTR(atag), atag->size);
#if 0
	aux_device_present = AUX_DEVICE_INFO;
#endif

	memory_end = mips_memory_upper;
	/*
	 * Due to prefetching and similar mechanism the CPU sometimes
	 * generates addresses beyond the end of memory.  We leave the size
	 * of one cache line at the end of memory unused to make shure we
	 * don't catch this type of bus errors.
	 */
	memory_end -= 128;
	memory_end &= PAGE_MASK;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif

	atag = bi_TagFind(tag_mount_root_rdonly);
	if (atag)
	  root_mountflags |= MS_RDONLY;

	atag = bi_TagFind(tag_command_line);
	if (atag)
		memcpy(&command_line, TAGVALPTR(atag), atag->size);	  

	memcpy(saved_command_line, command_line, CL_SIZE);
	saved_command_line[CL_SIZE-1] = '\0';

	*cmdline_p = command_line;
	*memory_start_p = (unsigned long) &_end;
	*memory_end_p = memory_end;

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE) {
		initrd_start = INITRD_START;
		initrd_end = INITRD_START+INITRD_SIZE;
		if (initrd_end > memory_end) {
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_end,memory_end);
		initrd_start = 0;
		}
	}
#endif
}

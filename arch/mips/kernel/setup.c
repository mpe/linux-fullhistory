/*
 *  linux/arch/mips/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 1995  Ralf Baechle
 */
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>

#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/vector.h>
#include <asm/segment.h>
#include <asm/stackframe.h>
#include <asm/system.h>

/*
 * How to handle the machine's features
 */
struct feature *feature;

#ifdef CONFIG_ACER_PICA_61
void acer_pica_61_handle_int(void);
/*
 * How to access the floppy controller's ports
 */
unsigned char jazz_fd_inb(unsigned int port);
void jazz_fd_outb(unsigned char value, unsigned int port);
/*
 * How to access the floppy DMA functions.
 */
void jazz_fd_enable_dma(void);
void jazz_fd_disable_dma(void);
int jazz_fd_request_dma(void);
void jazz_fd_free_dma(void);
void jazz_fd_clear_dma_ff(void);
void jazz_fd_set_dma_mode(char mode);
void jazz_fd_set_dma_addr(unsigned int a);
void jazz_fd_set_dma_count(unsigned int count);
int jazz_fd_get_dma_residue(void);
void jazz_fd_enable_irq(void);
void jazz_fd_disable_irq(void);
void jazz_fd_cacheflush(unsigned char *addr, unsigned int size);
/*
 * How to access the RTC functions.
 */
unsigned char jazz_rtc_read_data(void);
void jazz_rtc_write_data(unsigned char data);

struct feature acer_pica_61_feature = {
	acer_pica_61_handle_int,
	/*
	 * How to access the floppy controller's ports
	 */
	jazz_fd_inb,
	jazz_fd_outb,
	/*
	 * How to access the floppy DMA functions.
	 */
	jazz_fd_enable_dma,
	jazz_fd_disable_dma,
	jazz_fd_request_dma,
	jazz_fd_free_dma,
	jazz_fd_clear_dma_ff,
	jazz_fd_set_dma_mode,
	jazz_fd_set_dma_addr,
	jazz_fd_set_dma_count,
	jazz_fd_get_dma_residue,
	jazz_fd_enable_irq,
	jazz_fd_disable_irq,
	jazz_fd_cacheflush,
	/*
	 * How to access the RTC functions.
	 */
	jazz_rtc_read_data,
	jazz_rtc_write_data
};
#endif
#ifdef CONFIG_DECSTATION
void decstation_handle_handle_int(void);
void isa_outb(unsigned char value, unsigned int port);
unsigned char isa_inb(unsigned int port);
struct feature decstation_feature = {
	decstation_handle_handle_int,
	isa_inb /* Dummy - dunno how to handle this yet */
	isa_outb, /* Dummy - dunno how to handle this yet */
};
#endif
#ifdef CONFIG_DESKSTATION_RPC44
void deskstation_rpc44_handle_int(void);
void isa_outb(unsigned char value, unsigned int port);
unsigned char isa_inb(unsigned int port);
struct feature deskstation_rpc44_feature = {
	deskstation_rpc44_handle_int,
	isa_inb
	isa_outb,
};
#endif
#ifdef CONFIG_DESKSTATION_TYNE
void deskstation_tyne_handle_int(void);
void isa_outb(unsigned char value, unsigned int port);
unsigned char isa_inb(unsigned int port);
struct feature deskstation_tyne_feature = {
	deskstation_tyne_handle_int,
	isa_inb,
	isa_outb,
};
#endif
#ifdef CONFIG_MIPS_MAGNUM_4000
void mips_magnum_4000_handle_int(void);
/*
 * How to access the floppy controller's ports
 */
unsigned char jazz_fd_inb(unsigned int port);
void jazz_fd_outb(unsigned char value, unsigned int port);
/*
 * How to access the floppy DMA functions.
 */
void jazz_fd_enable_dma(void);
void jazz_fd_disable_dma(void);
int jazz_fd_request_dma(void);
void jazz_fd_free_dma(void);
void jazz_fd_clear_dma_ff(void);
void jazz_fd_set_dma_mode(char mode);
void jazz_fd_set_dma_addr(unsigned int a);
void jazz_fd_set_dma_count(unsigned int count);
int jazz_fd_get_dma_residue(void);
void jazz_fd_enable_irq(void);
void jazz_fd_disable_irq(void);
void jazz_fd_cacheflush(unsigned char *addr, unsigned int size);
/*
 * How to access the RTC functions.
 */
unsigned char jazz_rtc_read_data(void);
void jazz_rtc_write_data(unsigned char data);

struct feature mips_magnum_4000_feature = {
	mips_magnum_4000_handle_int,
	/*
	 * How to access the floppy controller's ports
	 */
	jazz_fd_inb,
	jazz_fd_outb,
	/*
	 * How to access the floppy DMA functions.
	 */
	jazz_fd_enable_dma,
	jazz_fd_disable_dma,
	jazz_fd_request_dma,
	jazz_fd_free_dma,
	jazz_fd_clear_dma_ff,
	jazz_fd_set_dma_mode,
	jazz_fd_set_dma_addr,
	jazz_fd_set_dma_count,
	jazz_fd_get_dma_residue,
	jazz_fd_enable_irq,
	jazz_fd_disable_irq,
	jazz_fd_cacheflush,
	/*
	 * How to access the RTC functions.
	 */
	jazz_rtc_read_data,
	jazz_rtc_write_data
};
#endif

/*
 * Tell us the machine setup..
 */
char wait_available;		/* set if the "wait" instruction available */

/*
 * Bus types ..
 */
int EISA_bus = 0;

/*
 * Setup options
 */
struct drive_info_struct drive_info;
struct screen_info screen_info = SCREEN_INFO;

unsigned char aux_device_present;
extern int ramdisk_size;
extern int root_mountflags;
extern int _end;

extern char empty_zero_page[PAGE_SIZE];

/*
 * Initialise this structure so that it will be placed in the
 * .data section of the object file
 */
struct bootinfo boot_info = BOOT_INFO;

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	empty_zero_page
#if 0
#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
#endif

static char command_line[CL_SIZE] = { 0, };
       char saved_command_line[CL_SIZE];

#if 0
/*
 * Code for easy access to new style bootinfo
 *
 * Parameter:  tag      -- taglist entry
 *
 * returns  :  (tag *) -- pointer to taglist entry, NULL for not found
 */
tag *
bi_TagFind(enum bi_tag tag)
{
	/* TBD */
	return 0;
}

/*
 * Only for taglist creators (bootloaders)
 *
 * Parameter:  tag       -- (enum bi_tag) taglist entry
 *
 * returns  :  1         -- success
 *             0         -- failure
 */
int
bi_TagAdd(enum bi_tag tag, unsigned long size, void *tagdata)
{
	/* TBD */
	return 0;
}
#endif /* 0 */

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	unsigned long memory_start, memory_end;

	switch(boot_info.machtype)
	{
#ifdef CONFIG_ACER_PICA_61
	case MACH_ACER_PICA_61:
		feature = &acer_pica_61_feature;
		break;
#endif
#ifdef CONFIG_DECSTATION
	case MACH_DECSTATION:
		feature = &decstation_feature;
		break;
#endif
#ifdef CONFIG_DESKSTATION_RPC
	case MACH_DESKSTATION_RPC:
		feature = &deskstation_rpc44_feature;
		break;
#endif
#ifdef CONFIG_DESKSTATION_TYNE
	case MACH_DESKSTATION_TYNE:
		feature = &deskstation_tyne_feature;
		break;
#endif
#ifdef CONFIG_MIPS_MAGNUM_4000
	case MACH_MIPS_MAGNUM_4000:
		feature = &mips_magnum_4000_feature;
		break;
#endif
	default:
		panic("Unsupported architecture");
	}

#if 0
 	ROOT_DEV = to_kdev_t(ORIG_ROOT_DEV);
#else
#ifdef CONFIG_BLK_DEV_FD
	ROOT_DEV = to_kdev_t(0x021c);	/* fd0H1440 */
#else
	ROOT_DEV = to_kdev_t(0x0101);	/* ram */ 
#endif
/*	ROOT_DEV = to_kdev_t(0x00ff); */	/* NFS */
#endif
 	memcpy(&drive_info, &boot_info.drive_info, sizeof(drive_info));
#if 0
	aux_device_present = AUX_DEVICE_INFO;
#endif
	memory_end = boot_info.memupper;
	memory_end &= PAGE_MASK;
	ramdisk_size = boot_info.ramdisk_size;
	if (boot_info.mount_root_rdonly)
		root_mountflags |= MS_RDONLY;

	memory_start = (unsigned long) &_end;
	memory_start += (ramdisk_size << 10);

	memcpy(saved_command_line, command_line, CL_SIZE);
	saved_command_line[CL_SIZE-1] = '\0';

	*cmdline_p = command_line;
	*memory_start_p = memory_start;
	*memory_end_p = memory_end;

#if 0
	/*
	 * Check that struct pt_regs is defined properly
	 * (Should be optimized away, but gcc 2.6.3 is too bad..)
	 */
	if (FR_SIZE != sizeof(struct pt_regs) ||
	    FR_SIZE & 7)
	{
		panic("Check_definition_of_struct_pt_regs\n");
	}
#endif
}

#ifdef CONFIG_PROC_FS
/*
 * BUFFER is PAGE_SIZE bytes long.
 */
int get_cpuinfo(char *buffer)
{
	const char *cpu_name[] = CPU_NAMES;
	const char *mach_name[] = MACH_NAMES;

	return sprintf(buffer,
		       "cpu\t\t\t: MIPS\n"
		       "cpu model\t\t: %s\n"
		       "system type\t\t: %s\n"
		       "BogoMIPS\t\t: %lu.%02lu\n",

		       cpu_name[boot_info.cputype < CPU_LAST ? boot_info.cputype : CPU_UNKNOWN],
		       mach_name[boot_info.machtype < CPU_LAST ? boot_info.machtype : CPU_UNKNOWN],
		       loops_per_sec / 500000, (loops_per_sec / 5000) % 100);
}
#endif /* CONFIG_PROC_FS */

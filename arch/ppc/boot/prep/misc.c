/*
 * arch/ppc/boot/prep/misc.c
 *
 * Adapted for PowerPC by Gary Thomas
 *
 * Rewritten by Cort Dougan (cort@cs.nmt.edu)
 * One day to be replaced by a single bootloader for chrp/prep/pmac. -- Cort
 */

#include <linux/types.h>
#include <linux/string.h>
#include <asm/residual.h>
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/elf.h>
#include <linux/pci_ids.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/bootinfo.h>
#include <asm/mmu.h>
#include <asm/byteorder.h>

#include "nonstdio.h"
#include "zlib.h"

/*
 * Please send me load/board info and such data for hardware not
 * listed here so I can keep track since things are getting tricky
 * with the different load addrs with different firmware.  This will
 * help to avoid breaking the load/boot process.
 * -- Cort
 */
char *avail_ram;
char *end_avail;

/* The linker tells us where the image is. */
extern char __image_begin, __image_end;
extern char __ramdisk_begin, __ramdisk_end;
extern char _end[];

#ifdef CONFIG_CMDLINE
#define CMDLINE CONFIG_CMDLINE
#else
#define CMDLINE ""
#endif
char cmd_preset[] = CMDLINE;
char cmd_buf[256];
char *cmd_line = cmd_buf;

int keyb_present = 1;	/* keyboard controller is present by default */
RESIDUAL hold_resid_buf;
RESIDUAL *hold_residual = &hold_resid_buf;
unsigned long initrd_size = 0;

char *zimage_start;
int zimage_size;

#if defined(CONFIG_SERIAL_8250_CONSOLE)
unsigned long com_port;
#endif /* CONFIG_SERIAL_8250_CONSOLE */
#ifdef CONFIG_VGA_CONSOLE
char *vidmem = (char *)0xC00B8000;
int lines = 25, cols = 80;
int orig_x, orig_y = 24;
#endif /* CONFIG_VGA_CONSOLE */

extern int CRT_tstc(void);
extern int vga_init(unsigned char *ISA_mem);
extern void gunzip(void *, int, unsigned char *, int *);
extern unsigned long serial_init(int chan, void *ignored);
extern void serial_fixups(void);
extern unsigned long get_mem_size(void);

void
writel(unsigned int val, unsigned int address)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	*(unsigned int *)address = cpu_to_le32(val);
}

unsigned int
readl(unsigned int address)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	return le32_to_cpu(*(unsigned int *)address);
}

#define PCI_CFG_ADDR(dev,off)	((0x80<<24) | (dev<<8) | (off&0xfc))
#define PCI_CFG_DATA(off)	(0x80000cfc+(off&3))

static void
pci_read_config_32(unsigned char devfn,
		unsigned char offset,
		unsigned int *val)
{
	writel(PCI_CFG_ADDR(devfn,offset), 0x80000cf8);
	*val = readl(PCI_CFG_DATA(offset));
	return;
}

#ifdef CONFIG_VGA_CONSOLE
void
scroll(void)
{
	int i;

	memcpy ( vidmem, vidmem + cols * 2, ( lines - 1 ) * cols * 2 );
	for ( i = ( lines - 1 ) * cols * 2; i < lines * cols * 2; i += 2 )
		vidmem[i] = ' ';
}
#endif /* CONFIG_VGA_CONSOLE */

unsigned long
decompress_kernel(unsigned long load_addr, int num_words, unsigned long cksum,
		  RESIDUAL *residual)
{
	int timer = 0;
	extern unsigned long start;
	char *cp, ch;
	unsigned long TotalMemory;
	unsigned char board_type;
	unsigned char base_mod;
	int start_multi = 0;
	unsigned int pci_viddid, pci_did, tulip_pci_base, tulip_base;

	serial_fixups();
#if defined(CONFIG_SERIAL_8250_CONSOLE)
	com_port = serial_init(0, NULL);
#endif /* CONFIG_SERIAL_8250_CONSOLE */
#if defined(CONFIG_VGA_CONSOLE)
	vga_init((unsigned char *)0xC0000000);
#endif /* CONFIG_VGA_CONSOLE */

	/*
	 * Tell the user where we were loaded at and where we were relocated
	 * to for debugging this process.
	 */
	puts("loaded at:     "); puthex(load_addr);
	puts(" "); puthex((unsigned long)(load_addr + (4*num_words))); puts("\n");
	if ( (unsigned long)load_addr != (unsigned long)&start ) {
		puts("relocated to:  "); puthex((unsigned long)&start);
		puts(" ");
		puthex((unsigned long)((unsigned long)&start + (4*num_words)));
		puts("\n");
	}

	if (residual) {
		/*
		 * Tell the user where the residual data is.
		 */
		puts("board data at: "); puthex((unsigned long)residual);
		puts(" ");
		puthex((unsigned long)((unsigned long)residual +
					sizeof(RESIDUAL)));
		puts("\nrelocated to:  ");puthex((unsigned long)hold_residual);
		puts(" ");
		puthex((unsigned long)((unsigned long)hold_residual +
					sizeof(RESIDUAL)));
		puts("\n");

		/* Is this Motorola PPCBug? */
		if ((1 & residual->VitalProductData.FirmwareSupports) &&
		    (1 == residual->VitalProductData.FirmwareSupplier)) {
			board_type = inb(0x800) & 0xF0;

			/*
			 * Reset the onboard 21x4x Ethernet
			 * Motorola Ethernet is at IDSEL 14 (devfn 0x70)
			 */
			pci_read_config_32(0x70, 0x00, &pci_viddid);
			pci_did = (pci_viddid & 0xffff0000) >> 16;
			/* Be sure we've really found a 21x4x chip */
			if (((pci_viddid & 0xffff) == PCI_VENDOR_ID_DEC) &&
				((pci_did == PCI_DEVICE_ID_DEC_TULIP_FAST) ||
				(pci_did == PCI_DEVICE_ID_DEC_TULIP) ||
				(pci_did == PCI_DEVICE_ID_DEC_TULIP_PLUS) ||
				(pci_did == PCI_DEVICE_ID_DEC_21142))) {
				pci_read_config_32(0x70,
						0x10,
						&tulip_pci_base);
				/* Get the physical base address */
				tulip_base =
					(tulip_pci_base & ~0x03UL) + 0x80000000;
				/* Strobe the 21x4x reset bit in CSR0 */
				writel(0x1, tulip_base);
			}

			/* If this is genesis 2 board then check for no
			 * keyboard controller and more than one processor.
			 */
			if (board_type == 0xe0) {
				base_mod = inb(0x803);
				/* if a MVME2300/2400 or a Sitka then no keyboard */
				if((base_mod == 0xFA) || (base_mod == 0xF9) ||
				   (base_mod == 0xE1)) {
					keyb_present = 0;	/* no keyboard */
				}
			}
			/* If this is a multiprocessor system then
			 * park the other processor so that the
			 * kernel knows where to find them.
			 */
			if (residual->MaxNumCpus > 1)
				start_multi = 1;
		}
		memcpy(hold_residual,residual,sizeof(RESIDUAL));
	} else {
		/* Tell the user we didn't find anything. */
		puts("No residual data found.\n");
        }

	/* First, figure out what kind of host bridge we are on.  If it's
	 * an MPC10x, we can ask it directly how much memory it has.
	 * Otherwise, see if the residual data has anything.  This isn't
	 * the best way, but it can be the only way.  If there's nothing,
	 * assume 32MB. -- Tom.
	 */
	/* See what our host bridge is. */
	pci_read_config_32(0x00, 0x00, &pci_viddid);
	pci_did = (pci_viddid & 0xffff0000) >> 16;
	/* See if we are on an MPC10x. */
	if (((pci_viddid & 0xffff) == PCI_VENDOR_ID_MOTOROLA)
			&& ((pci_did == PCI_DEVICE_ID_MOTOROLA_MPC105)
				|| (pci_did == PCI_DEVICE_ID_MOTOROLA_MPC106)
				|| (pci_did == PCI_DEVICE_ID_MOTOROLA_MPC107)))
		TotalMemory = get_mem_size();
	/* If it's not, see if we have anything in the residual data. */
	else if (residual && residual->TotalMemory)
		TotalMemory = residual->TotalMemory;
	/* Fall back to hard-coding 32MB. */
	else
		TotalMemory = 32*1024*1024;


	/* assume the chunk below 8M is free */
	end_avail = (char *)0x00800000;

	/*
	 * We link ourself to 0x00800000.  When we run, we relocate
	 * ourselves there.  So we just need __image_begin for the
	 * start. -- Tom
	 */
	zimage_start = (char *)(unsigned long)(&__image_begin);
	zimage_size = (unsigned long)(&__image_end) -
			(unsigned long)(&__image_begin);

	initrd_size = (unsigned long)(&__ramdisk_end) -
		(unsigned long)(&__ramdisk_begin);

	/*
	 * The zImage and initrd will be between start and _end, so they've
	 * already been moved once.  We're good to go now. -- Tom
	 */
	avail_ram = (char *)PAGE_ALIGN((unsigned long)_end);
	puts("zimage at:     "); puthex((unsigned long)zimage_start);
	puts(" "); puthex((unsigned long)(zimage_size+zimage_start));
	puts("\n");

	if ( initrd_size ) {
		puts("initrd at:     ");
		puthex((unsigned long)(&__ramdisk_begin));
		puts(" "); puthex((unsigned long)(&__ramdisk_end));puts("\n");
	}

	avail_ram = (char *)0x00400000;
	end_avail = (char *)0x00800000;
	puts("avail ram:     "); puthex((unsigned long)avail_ram); puts(" ");
	puthex((unsigned long)end_avail); puts("\n");

	if (keyb_present)
		CRT_tstc();  /* Forces keyboard to be initialized */

	puts("\nLinux/PPC load: ");
	cp = cmd_line;
	memcpy (cmd_line, cmd_preset, sizeof(cmd_preset));
	while ( *cp )
		putc(*cp++);
	while (timer++ < 5*1000) {
		if (tstc()) {
			while ((ch = getc()) != '\n' && ch != '\r') {
				/* Test for backspace/delete */
				if (ch == '\b' || ch == '\177') {
					if (cp != cmd_line) {
						cp--;
						puts("\b \b");
					}
				/* Test for ^x/^u (and wipe the line) */
				} else if (ch == '\030' || ch == '\025') {
					while (cp != cmd_line) {
						cp--;
						puts("\b \b");
					}
				} else {
					*cp++ = ch;
					putc(ch);
				}
			}
			break;  /* Exit 'timer' loop */
		}
		udelay(1000);  /* 1 msec */
	}
	*cp = 0;
	puts("\nUncompressing Linux...");

	gunzip(0, 0x400000, zimage_start, &zimage_size);
	puts("done.\n");

	if (start_multi) {
		puts("Parking cpu1 at 0xc0\n");
		residual->VitalProductData.SmpIar = (unsigned long)0xc0;
		residual->Cpus[1].CpuState = CPU_GOOD;
		hold_residual->VitalProductData.Reserved5 = 0xdeadbeef;
	}

	{
		struct bi_record *rec;
		unsigned long initrd_loc;
		unsigned long rec_loc = _ALIGN((unsigned long)(zimage_size) +
				(1 << 20) - 1, (1 << 20));
		
		rec = (struct bi_record *)rec_loc;

		/* We need to make sure that the initrd and bi_recs do not
		 * overlap. */
		if ( initrd_size ) {
			initrd_loc = (unsigned long)(&__ramdisk_begin);
			/* If the bi_recs are in the middle of the current
			 * initrd, move the initrd to the next MB
			 * boundary. */
			if ((rec_loc > initrd_loc) &&
					((initrd_loc + initrd_size)
					 > rec_loc)) {
				initrd_loc = _ALIGN((unsigned long)(zimage_size)
						+ (2 << 20) - 1, (2 << 20));
			 	memmove((void *)initrd_loc, &__ramdisk_begin,
					 initrd_size);
		         	puts("initrd moved:  "); puthex(initrd_loc);
			 	puts(" "); puthex(initrd_loc + initrd_size);
			 	puts("\n");
			}
		}

		rec->tag = BI_FIRST;
		rec->size = sizeof(struct bi_record);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);

		rec->tag = BI_BOOTLOADER_ID;
		memcpy( (void *)rec->data, "prepboot", 9);
		rec->size = sizeof(struct bi_record) + 8 + 1;
		rec = (struct bi_record *)((unsigned long)rec + rec->size);

		rec->tag = BI_MACHTYPE;
		rec->data[0] = _MACH_prep;
		rec->data[1] = 0;
		rec->size = sizeof(struct bi_record) + 2 *
			sizeof(unsigned long);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);

		rec->tag = BI_MEMSIZE;
		rec->data[0] = TotalMemory;
		rec->size = sizeof(struct bi_record) + sizeof(unsigned long);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);

		rec->tag = BI_CMD_LINE;
		memcpy( (char *)rec->data, cmd_line, strlen(cmd_line)+1);
		rec->size = sizeof(struct bi_record) + strlen(cmd_line) + 1;
		rec = (struct bi_record *)((unsigned long)rec + rec->size);

		if ( initrd_size ) {
			rec->tag = BI_INITRD;
			rec->data[0] = initrd_loc;
			rec->data[1] = initrd_size;
			rec->size = sizeof(struct bi_record) + 2 *
				sizeof(unsigned long);
			rec = (struct bi_record *)((unsigned long)rec +
					rec->size);
		}

		rec->tag = BI_LAST;
		rec->size = sizeof(struct bi_record);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);
	}
	puts("Now booting the kernel\n");
	return (unsigned long)hold_residual;
}

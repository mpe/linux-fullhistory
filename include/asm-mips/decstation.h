/*
 * Hardware info about DEC Personal DECStation systems (otherwise known
 * as maxine or pmax (internal DEC codenames).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Paul M. Antoine, some code and definitions are
 * by curteousy of Chris Fraser.
 *
 * This file is under construction - you were warned!
 */

#ifndef __ASM_MIPS_PMAX_H 
#define __ASM_MIPS_PMAX_H 

/*
 * The addresses below are virtual address. The mappings are
 * created on startup via wired entries in the tlb.
 */

#define PMAX_LOCAL_IO_SPACE     0xe0000000

/*
 * Motherboard regs (kseg1 addresses)
 */
#define PMAX_SSR_ADDR		0xbc040100		/* system support reg */

/*
 * SSR defines
 */
#define PMAX_SSR_LEDMASK	0x00000001		/* power LED */

/*
 * REX functions -- these are for the new TURBOchannel style ROMs
 */
#define REX_PROM_MAGIC  0x30464354			/* passed in a2 */

#define REX_GETBITMAP		0x84			/* get mem bitmap */
#define REX_GETCHAR		0x24			/* getch() */
#define REX_PUTCHAR		0x13			/* putch() */
#define REX_HALT		0x9c			/* halt the system */
#define REX_PRINTF		0x30			/* printf() */
#define REX_PUTS		0x2c			/* puts() */
#define REX_SLOTADDR		0x6c			/* slotaddr */

#ifndef __LANGUAGE_ASSEMBLY__

extern __inline__ void pmax_set_led(unsigned int bits)
{
	volatile unsigned int *led_register = (unsigned int *) PMAX_SSR_ADDR;

	*led_register = bits & PMAX_SSR_LEDMASK;
}

/*
 * Glue code to call the PMAX boot proms.
 */
extern asmlinkage void pmax_printf(const char *);

#endif

/*
 * These are just hacked out of the JAZZ ones, no ideas really.
 */
#define PMAX_KEYBOARD_ADDRESS   0xe0005000
#define PMAX_KEYBOARD_DATA      0xe0005000
#define PMAX_KEYBOARD_COMMAND   0xe0005001

#ifndef __LANGUAGE_ASSEMBLY__

typedef struct {
	unsigned char data;
	unsigned char command;
} pmax_keyboard_hardware;

typedef struct {
	unsigned char pad0[3];
	unsigned char data;
	unsigned char pad1[3];
	unsigned char command;
} mips_keyboard_hardware;

/*
 * For now.
 */
#define keyboard_hardware       pmax_keyboard_hardware

#endif

/*
 * Serial ports on DEC - maybe!
 */

#define PMAX_SERIAL1_BASE       (unsigned int)0xe0006000
#define PMAX_SERIAL2_BASE       (unsigned int)0xe0007000

/*
 * Dummy Device Address. Used in pmaxdma.c
 */

#define PMAX_DUMMY_DEVICE       0xe000d000
     
/*
 * PMAX timer registers and interrupt no.
 * Note that the hardware timer interrupt is actually on
 * cpu level 6, but to keep compatibility with PC stuff
 * it is remapped to vector 0. See arch/mips/kernel/entry.S.
 */
#define PMAX_TIMER_INTERVAL     0xe0000228
#define PMAX_TIMER_REGISTER     0xe0000230

/*
 * DRAM configuration register
 */
#ifndef __LANGUAGE_ASSEMBLY__
#ifdef __MIPSEL__
typedef struct {
	unsigned int bank2 : 3;
	unsigned int bank1 : 3;
	unsigned int mem_bus_width : 1;
	unsigned int reserved2 : 1;
	unsigned int page_mode : 1;
	unsigned int reserved1 : 23;
} dram_configuration;
#else /* defined (__MIPSEB__) */
typedef struct {
	unsigned int reserved1 : 23;
	unsigned int page_mode : 1;
	unsigned int reserved2 : 1;
	unsigned int mem_bus_width : 1;
	unsigned int bank1 : 3;
	unsigned int bank2 : 3;
} dram_configuration;
#endif
#endif /* __LANGUAGE_ASSEMBLY__ */

#define PMAX_DRAM_CONFIG        0xe00fffe0

/*
 * PMAX interrupt control registers
 */
#define PMAX_IO_IRQ_SOURCE      0xe0100000
#define PMAX_IO_IRQ_ENABLE      0xe0100002

/*
 * PMAX interrupt enable bits
 */
#define PMAX_IE_PARALLEL            (1 << 0)
#define PMAX_IE_FLOPPY              (1 << 1)
#define PMAX_IE_SOUND               (1 << 2)
#define PMAX_IE_VIDEO               (1 << 3)
#define PMAX_IE_ETHERNET            (1 << 4)
#define PMAX_IE_SCSI                (1 << 5)
#define PMAX_IE_KEYBOARD            (1 << 6)
#define PMAX_IE_MOUSE               (1 << 7)
#define PMAX_IE_SERIAL1             (1 << 8)
#define PMAX_IE_SERIAL2             (1 << 9)

/*
 * PMAX Interrupt Level definitions
 */

#define PMAX_TIMER_IRQ          0
#define PMAX_KEYBOARD_IRQ       1
#define PMAX_ETHERNET_IRQ       2 /* 15 */
#define PMAX_SERIAL1_IRQ        3
#define PMAX_SERIAL2_IRQ        4
#define PMAX_PARALLEL_IRQ       5
#define PMAX_FLOPPY_IRQ         6 /* needs to be consistent with floppy driver! */

/*
 * PMAX DMA Channels
 * Note: Channels 4...7 are not used with respect to the Acer PICA-61
 * chipset which does not provide these DMA channels.
 */

#define PMAX_SCSI_DMA           0              /* SCSI */
#define PMAX_FLOPPY_DMA         1              /* FLOPPY */
#define PMAX_AUDIOL_DMA         2              /* AUDIO L */
#define PMAX_AUDIOR_DMA         3              /* AUDIO R */

/*
 * PMAX R4030 MCT_ADR chip (DMA controller)
 * Note: Virtual Addresses !
 */

#define PMAX_R4030_CONFIG	0xE0000000	/* R4030 config register */
#define PMAX_R4030_REVISION     0xE0000008	/* same as PICA_ASIC_REVISION */
#define PMAX_R4030_INV_ADDR	0xE0000010	/* Invalid Address register */

#define PMAX_R4030_TRSTBL_BASE  0xE0000018	/* Translation Table Base */
#define PMAX_R4030_TRSTBL_LIM   0xE0000020	/* Translation Table Limit */
#define PMAX_R4030_TRSTBL_INV   0xE0000028	/* Translation Table Invalidate */

#define PMAX_R4030_CACHE_MTNC   0xE0000030	/* Cache Maintenance */
#define PMAX_R4030_R_FAIL_ADDR  0xE0000038	/* Remote Failed Address */
#define PMAX_R4030_M_FAIL_ADDR  0xE0000040	/* Memory Failed Adresss */

#define PMAX_R4030_CACHE_PTAG   0xE0000048	/* I/O Cache Physical Tag */
#define PMAX_R4030_CACHE_LTAG   0xE0000050	/* I/O Cache Logical Tag */
#define PMAX_R4030_CACHE_BMASK  0xE0000058	/* I/O Cache Byte Mask */
#define PMAX_R4030_CACHE_BWIN   0xE0000060	/* I/O Cache Buffer Window */

/*
 * Remote Speed Registers. 
 *
 *  0: free,      1: Ethernet,  2: SCSI,      3: Floppy,
 *  4: RTC,       5: Kb./Mouse  6: serial 1,  7: serial 2,
 *  8: parallel,  9: NVRAM,    10: CPU,      11: PROM,
 * 12: reserved, 13: free,     14: 7seg LED, 15: ???
 */

#define PMAX_R4030_REM_SPEED	0xE0000070	/* 16 Remote Speed Registers */
						/* 0xE0000070,78,80... 0xE00000E8 */
#define PMAX_R4030_IRQ_ENABLE   0xE00000E8	/* Internal Interrupt Enable */

#define PMAX_R4030_IRQ_SOURCE   0xE0000200	/* Interrupt Source Reg */
#define PMAX_R4030_I386_ERROR   0xE0000208	/* i386/EISA Bus Error */


/*
 * Access the R4030 DMA and I/O Controller
 */

#ifndef __LANGUAGE_ASSEMBLY__

extern inline unsigned short r4030_read_reg16(unsigned addr) {
	unsigned short ret = *((volatile unsigned short *)addr);
	__asm__ __volatile__("nop; nop; nop; nop;");
	return ret;
}

extern inline unsigned int r4030_read_reg32(unsigned addr) {
	unsigned int ret = *((volatile unsigned int *)addr);
	__asm__ __volatile__("nop; nop; nop; nop;");
	return ret;
}

extern inline void r4030_write_reg16(unsigned addr, unsigned val) {
	*((volatile unsigned short *)addr) = val;
	__asm__ __volatile__("nop; nop; nop; nop;");
}

extern inline unsigned int r4030_write_reg32(unsigned addr, unsigned val) {
	*((volatile unsigned int *)addr) = val;
	__asm__ __volatile__("nop; nop; nop; nop;");
}

#endif /* !LANGUAGE_ASSEMBLY__ */

	
#endif /* __ASM_MIPS_PMAX_H */

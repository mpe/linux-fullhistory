/*
 * linux/include/asm-arm/floppy.h
 *
 * (C) 1996 Russell King
 */
#ifndef __ASM_ARM_FLOPPY_H
#define __ASM_ARM_FLOPPY_H
#if 0
#include <asm/arch/floppy.h>
#endif

#define fd_outb(val,port)			\
	do {					\
		if ((port) == FD_DOR)		\
			fd_setdor((val));	\
		else				\
			outb((val),(port));	\
	} while(0)

#define fd_inb(port)		inb((port))
#define fd_request_irq()	request_irq(IRQ_FLOPPYDISK,floppy_interrupt,\
					SA_INTERRUPT|SA_SAMPLE_RANDOM,"floppy",NULL)
#define fd_free_irq()		free_irq(IRQ_FLOPPYDISK,NULL)
#define fd_disable_irq()	disable_irq(IRQ_FLOPPYDISK)
#define fd_enable_irq()		enable_irq(IRQ_FLOPPYDISK)

#define fd_request_dma()	request_dma(FLOPPY_DMA,"floppy")
#define fd_free_dma()		free_dma(FLOPPY_DMA)
#define fd_disable_dma()	disable_dma(FLOPPY_DMA)
#define fd_enable_dma()		enable_dma(FLOPPY_DMA)
#define fd_clear_dma_ff()	clear_dma_ff(FLOPPY_DMA)
#define fd_set_dma_mode(mode)	set_dma_mode(FLOPPY_DMA, (mode))
#define fd_set_dma_addr(addr)	set_dma_addr(FLOPPY_DMA, virt_to_bus((addr)))
#define fd_set_dma_count(len)	set_dma_count(FLOPPY_DMA, (len))
#define fd_cacheflush(addr,sz)

/* need to clean up dma.h */
#define DMA_FLOPPYDISK		DMA_FLOPPY

/* Floppy_selects is the list of DOR's to select drive fd
 *
 * On initialisation, the floppy list is scanned, and the drives allocated
 * in the order that they are found.  This is done by seeking the drive
 * to a non-zero track, and then restoring it to track 0.  If an error occurs,
 * then there is no floppy drive present.       [to be put back in again]
 */
static unsigned char floppy_selects[2][4] =
{
	{ 0x10, 0x21, 0x23, 0x33 },
	{ 0x10, 0x21, 0x23, 0x33 }
};

#define fd_setdor(dor)								\
do {										\
	int new_dor = (dor);							\
	if (new_dor & 0xf0)							\
		new_dor = (new_dor & 0x0c) | floppy_selects[fdc][new_dor & 3];	\
	else									\
		new_dor &= 0x0c;						\
	outb(new_dor, FD_DOR);							\
} while (0)

/*
 * Someday, we'll automatically detect which drives are present...
 */
extern __inline__ void fd_scandrives (void)
{
#if 0
	int floppy, drive_count;

	fd_disable_irq();
	raw_cmd = &default_raw_cmd;
	raw_cmd->flags = FD_RAW_SPIN | FD_RAW_NEED_SEEK;
	raw_cmd->track = 0;
	raw_cmd->rate = ?;
	drive_count = 0;
	for (floppy = 0; floppy < 4; floppy ++) {
		current_drive = drive_count;
		/*
		 * Turn on floppy motor
		 */
		if (start_motor(redo_fd_request))
			continue;
		/*
		 * Set up FDC
		 */
		fdc_specify();
		/*
		 * Tell FDC to recalibrate
		 */
		output_byte(FD_RECALIBRATE);
		LAST_OUT(UNIT(floppy));
		/* wait for command to complete */
		if (!successful) {
			int i;
			for (i = drive_count; i < 3; i--)
				floppy_selects[fdc][i] = floppy_selects[fdc][i + 1];
			floppy_selects[fdc][3] = 0;
			floppy -= 1;
		} else
			drive_count++;
	}
#else
	floppy_selects[0][0] = 0x10;
	floppy_selects[0][1] = 0x21;
	floppy_selects[0][2] = 0x23;
	floppy_selects[0][3] = 0x33;
#endif
}

#define FDC1 (0x3f0)
static int FDC2 = -1;

#define FLOPPY0_TYPE 4
#define FLOPPY1_TYPE 4

#define N_FDC 1
#define N_DRIVE 8

#define FLOPPY_MOTOR_MASK 0xf0

#define CROSS_64KB(a,s) (0)
	
#endif

/*
 * linux/include/asm-arm/arch-sa1100/ide.h
 *
 * Copyright (c) 1998 Hugo Fiennes & Nicolas Pitre
 *
 */

#include <linux/config.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>

/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static __inline__ void
ide_init_hwif_ports(hw_regs_t *hw, int data_port, int ctrl_port, int *irq)
{
	ide_ioreg_t reg;
	int i;
	int ioshift = 0;

	/* The Empeg board has the first two address lines unused */
	if (machine_is_empeg())
		ioshift = 2;
	
	memset(hw, 0, sizeof(*hw));

	reg = (ide_ioreg_t) (data_port << ioshift);
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += (1 << ioshift);
	}
	
	hw->io_ports[IDE_CONTROL_OFFSET] = 
		(ide_ioreg_t) (ctrl_port << ioshift);
	
	if (irq)
		*irq = 0;
}

/*
 * Special case for the empeg board which has the first two 
 * address lines unused 
 */
static __inline__ void
empeg_ide_init_hwif_ports(hw_regs_t *hw, int data_port, int ctrl_port)
{
	ide_ioreg_t reg;
	int i;

	memset(hw, 0, sizeof(*hw));

	reg = (ide_ioreg_t) (0xe0000000 + (data_port << 2));
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += (1 << 2);
	}
	hw->io_ports[IDE_CONTROL_OFFSET] = 
		(ide_ioreg_t) (0xe0000000 + (ctrl_port << 2));
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void
ide_init_default_hwifs(void)
{
    if( machine_is_empeg() ){
#ifdef CONFIG_SA1100_EMPEG
	hw_regs_t hw;

	/* First, do the SA1100 setup */

	/* PCMCIA IO space */
	MECR=0x21062106;

        /* Issue 3 is much neater than issue 2 */
	GPDR&=~(EMPEG_IDE1IRQ|EMPEG_IDE2IRQ);

	/* Interrupts on rising edge: lines are inverted before they get to
           the SA */
	set_GPIO_IRQ_edge( (EMPEG_IDE1IRQ|EMPEG_IDE2IRQ), GPIO_FALLING_EDGE );

	/* Take hard drives out of reset */
	GPSR=(EMPEG_IDERESET);

	/* Sonja and her successors have two IDE ports. */
	/* MAC 23/4/1999, swap these round so that the left hand
	   hard disk is hda when viewed from the front. This
	   doesn't match the silkscreen however. */
	empeg_ide_init_hwif_ports(&hw,0x10,0x1e);
	hw.irq = EMPEG_IRQ_IDE2;
	ide_register_hw(&hw, NULL);
	empeg_ide_init_hwif_ports(&hw,0x00,0x0e);
	hw.irq = ,EMPEG_IRQ_IDE1;
	ide_register_hw(&hw, NULL);
#endif
    }

    else if( machine_is_victor() ){
#ifdef CONFIG_SA1100_VICTOR
	hw_regs_t hw;

	/* Enable appropriate GPIOs as interrupt lines */
	GPDR &= ~GPIO_GPIO7;
	set_GPIO_IRQ_edge( GPIO_GPIO7, GPIO_RISING_EDGE );

	/* set the pcmcia interface timing */
	MECR = 0x00060006;

	ide_init_hwif_ports(&hw, 0xe00001f0, 0xe00003f6, NULL);
	hw.irq = IRQ_GPIO7;
	ide_register_hw(&hw, NULL);
#endif
    }
    else if (machine_is_lart()) {
#ifdef CONFIG_SA1100_LART
        hw_regs_t hw;

        /* Enable GPIO as interrupt line */
        GPDR &= ~GPIO_GPIO1;
        set_GPIO_IRQ_edge(GPIO_GPIO1, GPIO_RISING_EDGE);
        
        /* set PCMCIA interface timing */
        MECR = 0x00060006;

        /* init the interface */
/*         ide_init_hwif_ports(&hw, 0xe00000000, 0xe00001000, NULL); */
        ide_init_hwif_ports(&hw, 0xe00001000, 0xe00000000, NULL);
        hw.irq = IRQ_GPIO1;
        ide_register_hw(&hw, NULL);
#endif
    }
}



/*
 * linux/include/asm-arm/arch-sa1100/ide.h
 *
 * Copyright (c) 1998 Hugo Fiennes & Nicolas Pitre
 *
 */

#include <linux/config.h>

#ifdef CONFIG_BLK_DEV_IDE

#include <asm/irq.h>
#include <asm/arch/hardware.h>

/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static __inline__ void
ide_init_hwif_ports(hw_regs_t *hw, int data_port, int ctrl_port, int irq)
{
	ide_ioreg_t reg;
	int i;

	memset(hw, 0, sizeof(*hw));

#ifdef CONFIG_SA1100_EMPEG
/* The Empeg board has the first two address lines unused */
#define IO_SHIFT 2
#else
#define IO_SHIFT 0
#endif

	reg = (ide_ioreg_t) (data_port << IO_SHIFT);
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += (1 << IO_SHIFT);
	}
	hw->io_ports[IDE_CONTROL_OFFSET] = (ide_ioreg_t) (ctrl_port << IO_SHIFT);
	hw->irq = irq;
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void
ide_init_default_hwifs(void)
{
	hw_regs_t hw;

#if defined( CONFIG_SA1100_EMPEG )
	/* First, do the SA1100 setup */

	/* PCMCIA IO space */
	MECR=0x21062106;

        /* Issue 3 is much neater than issue 2 */
	GPDR&=~(EMPEG_IDE1IRQ|EMPEG_IDE2IRQ);

	/* Interrupts on rising edge: lines are inverted before they get to
           the SA */
	GRER&=~(EMPEG_IDE1IRQ|EMPEG_IDE2IRQ);
	GFER|=(EMPEG_IDE1IRQ|EMPEG_IDE2IRQ);

	/* Take hard drives out of reset */
	GPSR=(EMPEG_IDERESET);

	/* Clear GEDR */
	GEDR=0xffffffff;

	/* Sonja and her successors have two IDE ports. */
	/* MAC 23/4/1999, swap these round so that the left hand
	   hard disk is hda when viewed from the front. This
	   doesn't match the silkscreen however. */
	ide_init_hwif_ports(&hw,0x10,0x1e,EMPEG_IRQ_IDE2);
	ide_register_hw(&hw, NULL);
	ide_init_hwif_ports(&hw,0x00,0x0e,EMPEG_IRQ_IDE1);
	ide_register_hw(&hw, NULL);

#elif defined( CONFIG_SA1100_VICTOR )
	/* Enable appropriate GPIOs as interrupt lines */
	GPDR &= ~GPIO_GPIO7;
	GRER |= GPIO_GPIO7;
	GFER &= ~GPIO_GPIO7;
	GEDR = GPIO_GPIO7;
	/* set the pcmcia interface timing */
	MECR = 0x00060006;

	ide_init_hwif_ports(&hw, 0x1f0, 0x3f6, IRQ_GPIO7);
	ide_register_hw(&hw, NULL);
#else
#error Missing IDE interface definition in include/asm/arch/ide.h
#endif
}

#endif


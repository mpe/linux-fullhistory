/*
 * linux/include/asm-arm/arch-sa1100/assabet.h
 *
 * Created 2000/06/05 by Nicolas Pitre <nico@cam.org>
 *
 * This file contains the hardware specific definitions for Assabet
 * Only include this file from SA1100-specific files.
 *
 * 2000/05/23 John Dorsey <john+@cs.cmu.edu>
 *      Definitions for Neponset added.
 */
#ifndef __ASM_ARCH_ASSABET_H
#define __ASM_ARCH_ASSABET_H

/* System Configuration Register flags */

#define ASSABET_SCR_SDRAM_LOW	(1<<2)	/* SDRAM size (low bit) */
#define ASSABET_SCR_SDRAM_HIGH	(1<<3)	/* SDRAM size (high bit) */
#define ASSABET_SCR_FLASH_LOW	(1<<4)	/* Flash size (low bit) */
#define ASSABET_SCR_FLASH_HIGH	(1<<5)	/* Flash size (high bit) */
#define ASSABET_SCR_GFX		(1<<8)	/* Graphics Accelerator (0 = present) */
#define ASSABET_SCR_SA1111	(1<<9)	/* Neponset (0 = present) */

#define ASSABET_SCR_INIT	-1


/* Board Control Register */

#define ASSABET_BCR_BASE  0xf1000000
#define ASSABET_BCR (*(volatile unsigned int *)(ASSABET_BCR_BASE))

#define ASSABET_BCR_DB1110 \
	(ASSABET_BCR_SPK_OFF    | ASSABET_BCR_QMUTE     | \
	 ASSABET_BCR_LED_GREEN  | ASSABET_BCR_LED_RED   | \
	 ASSABET_BCR_RS232EN    | ASSABET_BCR_LCD_12RGB | \
	 ASSABET_BCR_IRDA_MD0)

#define ASSABET_BCR_DB1111 \
	(ASSABET_BCR_SPK_OFF    | ASSABET_BCR_QMUTE     | \
	 ASSABET_BCR_LED_GREEN  | ASSABET_BCR_LED_RED   | \
	 ASSABET_BCR_RS232EN    | ASSABET_BCR_LCD_12RGB | \
	 ASSABET_BCR_CF_BUS_OFF | ASSABET_BCR_STEREO_LB | \
	 ASSABET_BCR_IRDA_MD0   | ASSABET_BCR_CF_RST)

#define ASSABET_BCR_CF_PWR	(1<<0)	/* Compact Flash Power (1 = 3.3v, 0 = off) */
#define ASSABET_BCR_CF_RST	(1<<1)	/* Compact Flash Reset (1 = power up reset) */
#define ASSABET_BCR_GFX_RST	(1<<1)	/* Graphics Accelerator Reset (0 = hold reset) */
#define ASSABET_BCR_CODEC_RST	(1<<2)	/* 0 = Holds UCB1300, ADI7171, and UDA1341 in reset */
#define ASSABET_BCR_IRDA_FSEL	(1<<3)	/* IRDA Frequency select (0 = SIR, 1 = MIR/ FIR) */
#define ASSABET_BCR_IRDA_MD0	(1<<4)	/* Range/Power select */
#define ASSABET_BCR_IRDA_MD1	(1<<5)	/* Range/Power select */
#define ASSABET_BCR_STEREO_LB	(1<<6)	/* Stereo Loopback */
#define ASSABET_BCR_CF_BUS_OFF	(1<<7)	/* Compact Flash bus (0 = on, 1 = off (float)) */
#define ASSABET_BCR_AUDIO_ON	(1<<8)	/* Audio power on */
#define ASSABET_BCR_LIGHT_ON	(1<<9)	/* Backlight */
#define ASSABET_BCR_LCD_12RGB	(1<<10)	/* 0 = 16RGB, 1 = 12RGB */
#define ASSABET_BCR_LCD_ON	(1<<11)	/* LCD power on */
#define ASSABET_BCR_RS232EN	(1<<12)	/* RS232 transceiver enable */
#define ASSABET_BCR_LED_RED	(1<<13)	/* D9 (0 = on, 1 = off) */
#define ASSABET_BCR_LED_GREEN	(1<<14)	/* D8 (0 = on, 1 = off) */
#define ASSABET_BCR_VIB_ON	(1<<15)	/* Vibration motor (quiet alert) */
#define ASSABET_BCR_COM_DTR	(1<<16)	/* COMport Data Terminal Ready */
#define ASSABET_BCR_COM_RTS	(1<<17)	/* COMport Request To Send */
#define ASSABET_BCR_RAD_WU	(1<<18)	/* Radio wake up interrupt */
#define ASSABET_BCR_SMB_EN	(1<<19)	/* System management bus enable */
#define ASSABET_BCR_TV_IR_DEC	(1<<20)	/* TV IR Decode Enable (not implemented) */
#define ASSABET_BCR_QMUTE	(1<<21)	/* Quick Mute */
#define ASSABET_BCR_RAD_ON	(1<<22)	/* Radio Power On */
#define ASSABET_BCR_SPK_OFF	(1<<23)	/* 1 = Speaker amplifier power off */

extern unsigned long SCR_value;
extern unsigned long BCR_value;
#define ASSABET_BCR_set(x)	ASSABET_BCR = (BCR_value |= (x))
#define ASSABET_BCR_clear(x)	ASSABET_BCR = (BCR_value &= ~(x))

#define ASSABET_BSR_BASE	0xf1000000
#define ASSABET_BSR (*(volatile unsigned int*)(ASSABET_BSR_BASE))

#define ASSABET_BSR_RS232_VALID	(1 << 24)
#define ASSABET_BSR_COM_DCD	(1 << 25)
#define ASSABET_BSR_COM_CTS	(1 << 26)
#define ASSABET_BSR_COM_DSR	(1 << 27)
#define ASSABET_BSR_RAD_CTS	(1 << 28)
#define ASSABET_BSR_RAD_DSR	(1 << 29)
#define ASSABET_BSR_RAD_DCD	(1 << 30)
#define ASSABET_BSR_RAD_RI	(1 << 31)


/* GPIOs for which the generic definition doesn't say much */
#define ASSABET_GPIO_RADIO_IRQ		GPIO_GPIO (14)	/* Radio interrupt request  */
#define ASSABET_GPIO_L3_I2C_SDA		GPIO_GPIO (15)	/* L3 and SMB control ports */
#define ASSABET_GPIO_PS_MODE_SYNC	GPIO_GPIO (16)	/* Power supply mode/sync   */
#define ASSABET_GPIO_L3_MODE		GPIO_GPIO (17)	/* L3 mode signal with LED  */
#define ASSABET_GPIO_L3_I2C_SCL		GPIO_GPIO (18)	/* L3 and I2C control ports */
#define ASSABET_GPIO_STEREO_64FS_CLK	GPIO_GPIO (19)	/* SSP UDA1341 clock input  */
#define ASSABET_GPIO_CF_IRQ		GPIO_GPIO (21)	/* CF IRQ   */
#define ASSABET_GPIO_CF_CD		GPIO_GPIO (22)	/* CF CD */
#define ASSABET_GPIO_UCB1300_IRQ	GPIO_GPIO (23)	/* UCB GPIO and touchscreen */
#define ASSABET_GPIO_CF_BVD2		GPIO_GPIO (24)	/* CF BVD */
#define ASSABET_GPIO_GFX_IRQ		GPIO_GPIO (24)	/* Graphics IRQ */
#define ASSABET_GPIO_CF_BVD1		GPIO_GPIO (25)	/* CF BVD */
#define ASSABET_GPIO_NEP_IRQ		GPIO_GPIO (25)	/* Neponset IRQ */
#define ASSABET_GPIO_BATT_LOW		GPIO_GPIO (26)	/* Low battery */
#define ASSABET_GPIO_RCLK		GPIO_GPIO (26)	/* CCLK/2  */

#define ASSABET_IRQ_GPIO_CF_IRQ		IRQ_GPIO21
#define ASSABET_IRQ_GPIO_CF_CD		IRQ_GPIO22
#define ASSABET_IRQ_GPIO_UCB1300_IRQ	IRQ_GPIO23
#define ASSABET_IRQ_GPIO_CF_BVD2	IRQ_GPIO24
#define ASSABET_IRQ_GPIO_CF_BVD1	IRQ_GPIO25
#define ASSABET_IRQ_GPIO_NEP_IRQ	IRQ_GPIO25


/*
 * Neponset definitions: 
 */

#define SA1111_BASE             (0x40000000)

#define NEPONSET_ETHERNET_IRQ	MISC_IRQ0
#define NEPONSET_USAR_IRQ	MISC_IRQ1

#define NEPONSET_CPLD_BASE      (0x10000000)
#define Nep_p2v( x )            ((x) - NEPONSET_CPLD_BASE + 0xf3000000)
#define Nep_v2p( x )            ((x) - 0xf3000000 + NEPONSET_CPLD_BASE)

#define _IRR                    0x10000024      /* Interrupt Reason Register */
#define _AUD_CTL                0x100000c0      /* Audio controls (RW)       */
#define _MDM_CTL_0              0x100000b0      /* Modem control 0 (RW)      */
#define _MDM_CTL_1              0x100000b4      /* Modem control 1 (RW)      */
#define _NCR_0	                0x100000a0      /* Control Register (RW)     */
#define _KP_X_OUT               0x10000090      /* Keypad row write (RW)     */
#define _KP_Y_IN                0x10000080      /* Keypad column read (RO)   */
#define _SWPK                   0x10000020      /* Switch pack (RO)          */
#define _WHOAMI                 0x10000000      /* System ID Register (RO)   */

#define _LEDS                   0x10000010      /* LEDs [31:0] (WO)          */

#define IRR                     (*((volatile u_char *) Nep_p2v(_IRR)))
#define AUD_CTL                 (*((volatile u_char *) Nep_p2v(_AUD_CTL)))
#define MDM_CTL_0               (*((volatile u_char *) Nep_p2v(_MDM_CTL_0)))
#define MDM_CTL_1               (*((volatile u_char *) Nep_p2v(_MDM_CTL_1)))
#define NCR_0			(*((volatile u_char *) Nep_p2v(_NCR_0)))
#define KP_X_OUT                (*((volatile u_char *) Nep_p2v(_KP_X_OUT)))
#define KP_Y_IN                 (*((volatile u_char *) Nep_p2v(_KP_Y_IN)))
#define SWPK                    (*((volatile u_char *) Nep_p2v(_SWPK)))
#define WHOAMI                  (*((volatile u_char *) Nep_p2v(_WHOAMI)))

#define LEDS                    (*((volatile Word   *) Nep_p2v(_LEDS)))

#define IRR_ETHERNET		(1<<0)
#define IRR_USAR		(1<<1)
#define IRR_SA1111		(1<<2)

#define AUD_SEL_1341            (1<<0)
#define AUD_MUTE_1341           (1<<1)

#define MDM_CTL0_RTS1		(1 << 0)
#define MDM_CTL0_DTR1		(1 << 1)
#define MDM_CTL0_RTS2		(1 << 2)
#define MDM_CTL0_DTR2		(1 << 3)

#define MDM_CTL1_CTS1		(1 << 0)
#define MDM_CTL1_DSR1		(1 << 1)
#define MDM_CTL1_DCD1		(1 << 2)
#define MDM_CTL1_CTS2		(1 << 3)
#define MDM_CTL1_DSR2		(1 << 4)
#define MDM_CTL1_DCD2		(1 << 5)

#define NCR_GP01_OFF		(1<<0)
#define NCR_TP_PWR_EN		(1<<1)
#define NCR_MS_PWR_EN		(1<<2)
#define NCR_ENET_OSC_EN		(1<<3)
#define NCR_SPI_KB_WK_UP	(1<<4)
#define NCR_A0VPP		(1<<5)
#define NCR_A1VPP		(1<<6)

#ifdef CONFIG_ASSABET_NEPONSET
#define machine_has_neponset()  ((SCR_value & ASSABET_SCR_SA1111) == 0)
#else
#define machine_has_neponset()	(0)
#endif

#endif

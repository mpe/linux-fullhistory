/*
 * heathrow.h: definitions for using the "Heathrow" I/O controller chip.
 *
 * Grabbed from Open Firmware definitions on a PowerBook G3 Series
 *
 * Copyright (C) 1997 Paul Mackerras.
 */

/* offset from ohare base for feature control register */
#define HEATHROW_FEATURE_REG	0x38

/*
 * Bits in feature control register.
 * Bits postfixed with a _N are in inverse logic
 */
#define HRW_MODEM_POWER_N	1		/* turns off modem power */
#define HRW_BAY_POWER_N		2
#define HRW_BAY_PCI_ENABLE	4
#define HRW_BAY_IDE_ENABLE	8
#define HRW_BAY_FLOPPY_ENABLE	0x10
#define HRW_IDE0_ENABLE		0x20
#define HRW_IDE0_RESET_N	0x40
#define HRW_BAY_RESET_N		0x80
#define HRW_IOBUS_ENABLE	0x100		/* Internal IDE ? */
#define HRW_SCC_ENABLE		0x200
#define HRW_MESH_ENABLE		0x400
#define HRW_SWIM_ENABLE		0x800
#define HRW_SOUND_POWER_N	0x1000
#define HRW_SOUND_CLK_ENABLE	0x2000
#define HRW_SCCA_IO		0x4000
#define HRW_SCCB_IO		0x8000
#define HRW_PORT_OR_DESK_VIA_N	0x10000		/* This one is 0 on PowerBook */
#define HRW_PWM_MON_ID_N	0x20000		/* ??? (0) */
#define HRW_HOOK_MB_CNT_N	0x40000		/* ??? (0) */
#define HRW_SWIM_CLONE_FLOPPY	0x80000		/* ??? (0) */
#define HRW_AUD_RUN22		0x100000	/* ??? (1) */
#define HRW_SCSI_LINK_MODE	0x200000	/* Read ??? (1) */
#define HRW_ARB_BYPASS		0x400000	/* ??? (0 on main, 1 on gatwick) */
#define HRW_IDE1_RESET_N	0x800000	/* Media bay */
#define HRW_SLOW_SCC_PCLK	0x1000000	/* ??? (0) */
#define HRW_RESET_SCC		0x2000000	/* perhaps? */
#define HRW_MFDC_CELL_ENABLE	0x4000000	/* ??? (0) */
#define HRW_USE_MFDC		0x8000000	/* ??? (0) */
#define HRW_BMAC_IO_ENABLE	0x60000000	/* two bits, not documented in OF */
#define HRW_BMAC_RESET		0x80000000	/* not documented in OF */


#define PADD_MODEM_POWER_N	0x00000001	/* modem power on paddington */

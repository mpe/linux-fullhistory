/*
 * ohare.h: definitions for using the "O'Hare" I/O controller chip.
 *
 * Copyright (C) 1997 Paul Mackerras.
 */

/* offset from ohare base for feature control register */
#define OHARE_FEATURE_REG	0x38

/*
 * Bits in feature control register.
 * These were mostly derived by experiment on a powerbook 3400
 * and may differ for other machines.
 */
#define OH_SCC_RESET		1
#define OH_BAY_RESET		2	/* a guess */
#define OH_BAY_PCI_ENABLE	4	/* a guess */
#define OH_BAY_IDE_ENABLE	8
#define OH_BAY_FLOPPY_ENABLE	0x10
#define OH_IDE_ENABLE		0x20
#define OH_BAY_ENABLE		0x80
#define OH_SCC_ENABLE		0x200
#define OH_MESH_ENABLE		0x400
#define OH_FLOPPY_ENABLE	0x800
#define OH_SCCA_IO		0x2000
#define OH_SCCB_IO		0x4000
#define OH_VIA_ENABLE		0x10000
#define OH_IDECD_POWER		0x800000

/*
 * Bits to set in the feature control register on PowerBooks.
 */
#define PBOOK_FEATURES		(OH_IDE_ENABLE | OH_SCC_ENABLE | \
				 OH_MESH_ENABLE | OH_SCCA_IO | OH_SCCB_IO)

/*
 * A magic value to put into the feature control register of the
 * "ohare" I/O controller on Starmaxes to enable the IDE CD interface.
 * Contributed by Harry Eaton.
 */
#define STARMAX_FEATURES	0xbeff7a

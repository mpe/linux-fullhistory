/*
 * Definitions for accessing the Feature Control Register (FCR)
 * on Power Macintoshes and similar machines.  The FCR lets us
 * enable/disable, reset, and power up/down various peripherals.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Paul Mackerras.
 */
#ifndef __ASM_PPC_FEATURE_H
#define __ASM_PPC_FEATURE_H

/*
 * The FCR bits for particular features vary somewhat between
 * different machines.  So we abstract a list of features here
 * and let the feature_* routines map them to the actual bits.
 */
enum system_feature {
	FEATURE_null,
	FEATURE_Serial_reset,
	FEATURE_Serial_enable,
	FEATURE_Serial_IO_A,
	FEATURE_Serial_IO_B,
	FEATURE_SWIM3_enable,
	FEATURE_MESH_enable,
	FEATURE_IDE_enable,
	FEATURE_VIA_enable,
	FEATURE_CD_power,
	FEATURE_Mediabay_reset,
	FEATURE_Mediabay_enable,
	FEATURE_Mediabay_PCI_enable,
	FEATURE_Mediabay_IDE_enable,
	FEATURE_Mediabay_floppy_enable,
	FEATURE_BMac_reset,
	FEATURE_BMac_IO_enable,
	FEATURE_Modem_PowerOn,
	FEATURE_Modem_Reset,
	FEATURE_last,
};

/* Note about the device parameter: Each device gives it's own entry. If NULL,
   the feature function will just do nothing and return -EINVAL.
   The feature management will walk up the device tree until in reaches a recognized
   chip for which features can be changed and it will then apply the necessary
   features to that chip. If it's not found, -ENODEV is returned.
   Note also that feature_test/set/clear are interrupt-safe provided that they are
   called _after_ feature_init() is completed.
 */

/* Test whether a particular feature is enabled. May return -ENODEV*/
extern int	feature_test(struct device_node* device, enum system_feature f);

/* Set a particular feature. Returns 0 or -ENODEV */
extern int	feature_set(struct device_node* device, enum system_feature f);

/* Clear a particular feature */
extern int	feature_clear(struct device_node* device, enum system_feature f);

/* Initialize feature stuff */
extern void	feature_init(void);


#endif /* __ASM_PPC_FEATURE_H */

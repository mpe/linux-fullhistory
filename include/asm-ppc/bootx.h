/*
 * This file describes the structure passed from the BootX application
 * (for MacOS) when it is used to boot Linux.
 *
 * Written by Benjamin Herrenschmidt.
 */


#ifndef __ASM_BOOTX_H__
#define __ASM_BOOTX_H__

#ifdef macintosh
#include <Types.h>
#endif

#ifdef macintosh
/* All this requires PowerPC alignment */
#pragma options align=power
#endif

#define BOOT_INFO_VERSION		1
#define BOOT_INFO_COMPATIBLE_VERSION	1

/* Here are the boot informations that are passed to the bootstrap
 * Note that the kernel arguments and the device tree are appended
 * at the end of this structure. */
typedef struct boot_infos
{
	/* Version of this structure */
	unsigned long	version;
	/* backward compatible down to version: */
	unsigned long	compatible_version;
	
	/* Set to 0 by current BootX */
	unsigned long	unused[3];
	
	/* The device tree (internal addresses relative to the beginning of the tree,
	 * device tree offset relative to the beginning of this structure). */
	unsigned long	deviceTreeOffset;	/* Device tree offset */
	unsigned long	deviceTreeSize;		/* Size of the device tree */
		
	/* Some infos about the current MacOS display */
	unsigned long	dispDeviceRect[4];	/* left,top,right,bottom */
	unsigned long	dispDeviceDepth;	/* (8, 16 or 32) */
	unsigned char*	dispDeviceBase;		/* base address (physical) */
	unsigned long	dispDeviceRowBytes;	/* rowbytes (in bytes) */
	unsigned long	dispDeviceColorsOffset;	/* Colormap (8 bits only) or 0 (*) */
	/* Optional offset in the registry to the current
	 * MacOS display. (Can be 0 when not detected) */
 	unsigned long	dispDeviceRegEntryOffset;

	/* Optional pointer to boot ramdisk (offset from this structure) */
	unsigned long	ramDisk;
	unsigned long	ramDiskSize;		/* size of ramdisk image */
	
	/* Kernel command line arguments (offset from this structure) */
	unsigned long	kernelParamsOffset;
	
} boot_infos_t;

/* (*) The format of the colormap is 256 * 3 * 2 bytes. Each color index is represented
 * by 3 short words containing a 16 bits (unsigned) color component.
 * Later versions may contain the gamma table for direct-color devices here.
 */
#define BOOTX_COLORTABLE_SIZE	(256UL*3UL*2UL);

#ifdef macintosh
#pragma options align=reset
#endif

#endif

/* $Id: fs_mount.h,v 1.2 1997/04/18 14:34:46 jj Exp $
 * fs_mount.h:  Definitions for mount structure conversions.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
#ifndef __ASM_FS_MOUNT_H
#define __ASM_FS_MOUNT_H

#if defined(CONFIG_SPARC32_COMPAT) || defined(CONFIG_SPARC32_COMPAT_MODULE)

#include <linux/sched.h>

/* We need this to convert 32bit mount structures to 64bit */

extern void *do_ncp_super_data_conv(void *raw_data);
extern void *do_smb_super_data_conv(void *raw_data);

extern __inline__ void *ncp_super_data_conv(void *raw_data)
{
	if (current->tss.flags & SPARC_FLAG_32BIT)
		return do_ncp_super_data_conv(raw_data);
	else
		return raw_data;
}

extern __inline__ void *smb_super_data_conv(void *raw_data)
{
	if (current->tss.flags & SPARC_FLAG_32BIT)
		return do_smb_super_data_conv(raw_data);
	else
		return raw_data;
}

#else /* CONFIG_SPARC32_COMPAT* */

#define ncp_super_data_conv(__x) __x
#define smb_super_data_conv(__x) __x

#endif /* CONFIG_SPARC32_COMPAT* */

#define nfs_super_data_conv(__x) __x

#endif /* __ASM_FS_MOUNT_H */

/*
 *  linux/fs/ufs/ufs_swab.c
 *
 * Copyright (C) 1997
 * Francois-Rene Rideau <rideau@ens.fr>
 *
 */

/*
 * For inspiration, you might wanna check sys/ufs/ffs/fs.h from whateverBSD
 *
 * NOTES
 * 19970406 - Fare <rideau@ens.fr>
 *   1) I began from old very preliminary 2.0.x sources,
 *	but it was underfeatured;
 *	I later saw that 2.1.1 sources had a *global* UFS byteswap flag.
 *	EVIL: imagine that a swabbed partition be mounted
 *	while a non-swabbed partition are active (that sucks!)
 *	I merged that source tree with mine.
 *   2) I hope no one is using obNNUUXXIIous byteorder.
 *	That's the only thing I might have broken,
 *	though I rather think it's a fix:
 *	instead of __u64 like BSD,
 *	the former driver used an explicitly bigendian array of __u32!
 *   3) I provide a few macros that use GCC C Extensions.
 *	Port to other compilers would require avoiding them.
 *	in any case, 64 bit (long long) support is required,
 *	unless you're ready to workaround
 *   4) the swab routines below depend on the precise name and order
 *	of the structure elements. Watch out any modification in ufs_fs.h!!!
 *   5) putting byteswapping stuff in ufs_swab* seems cleaner to me.
 *   6) These sources should work with both 2.0 and 2.1 kernels...
 *
 * 19971013 - Fare <rideaufr@issy.cnet.fr>
 *   1) Ported to 2.1.57
 *   2) instead of byteswapping, use [bl]e_to_cpu:
 *     it might be that we run on a VAX!
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 *
 * HOWTO continue adding swab support:
 *	basically, anywhere metadata is bread() (i.e. mapped to block device),
 *      data should either be SWAB()ed on the fly,
 *      or copied to a buffer and globally bswap_ufs_*() there.
 *
 */

#include <linux/fs.h>
#include "ufs_swab.h"

static __inline__ void n_be16_to_cpus(__u16*p,unsigned n) {
#ifndef __BIG_ENDIAN
        unsigned i;
        for(i=0;i<n;i++) {
		be16_to_cpus(&p[i]);
        }
#endif
}
static __inline__ void n_be32_to_cpus(__u32*p,unsigned n) {
#ifndef __BIG_ENDIAN
        unsigned i;
        for(i=0;i<n;i++) {
		be32_to_cpus(&p[i]);
        }
#endif
}
static __inline__ void n_le16_to_cpus(__u16*p,unsigned n) {
#ifndef __LITTLE_ENDIAN
        unsigned i;
        for(i=0;i<n;i++) {
		le16_to_cpus(&p[i]);
        }
#endif
}
static __inline__ void n_le32_to_cpus(__u32*p,unsigned n) {
#ifndef __LITTLE_ENDIAN
        unsigned i;
        for(i=0;i<n;i++) {
		le32_to_cpus(&p[i]);
        }
#endif
}

#define __length_before(p,member) \
        ((unsigned)(((char*)&((p)->member))-(char*)(p)))
#define __length_since(p,member) \
        ((unsigned)(sizeof(*p)-__length_before(p,member)))
#define __length_between(p,begin,after_end) \
        ((unsigned)(__length_before(p,after_end)-__length_before(p,begin)))
#define be32_to_cpus__between(s,begin,after_end) \
        n_be32_to_cpus((__u32*)&((s).begin), \
                __length_between(&s,begin,after_end)/4)
#define le32_to_cpus__between(s,begin,after_end) \
        n_le32_to_cpus((__u32*)&((s).begin), \
                __length_between(&s,begin,after_end)/4)
#define be32_to_cpus__since(s,begin) \
	n_be32_to_cpus((__u32*)&((s).begin), \
                __length_since(&s,begin)/4)
#define le32_to_cpus__since(s,begin) \
	n_le32_to_cpus((__u32*)&((s).begin), \
                __length_since(&s,begin)/4)
#define be16_to_cpus__between(s,begin,after_end) \
	n_be16_to_cpus((__u16*)&((s).begin), \
                __length_between(&s,begin,after_end)/2)
#define le16_to_cpus__between(s,begin,after_end) \
	n_le16_to_cpus((__u16*)&((s).begin), \
                __length_between(&s,begin,after_end)/2)

/*
 * Here are the whole-structure swabping routines...
 * They were fun to design, but I don't understand why we
 * need a copy of the superblock, anyway. -- Fare'
 */

extern void ufs_superblock_be_to_cpus(struct ufs_superblock * usb) {
#ifndef __BIG_ENDIAN
	__u16 sb_type = 1;	/* SUN type superblock */

	if (usb->fs_u.fs_44.fs_maxsymlinklen >= 0)
		sb_type = 0;	/* 4.4BSD (FreeBSD) type superblock */
 
	be32_to_cpus__between(*usb,fs_link,fs_fmod);
        /* XXX - I dunno what to do w/ fs_csp,
         * but it is unused by the current code, so that's ok for now.
         */
	be32_to_cpus(&usb->fs_cpc);
	if (sb_type) {
        	be16_to_cpus__between(*usb,fs_opostbl,fs_u.fs_sun.fs_sparecon);
        	be32_to_cpus__between(*usb,fs_u.fs_sun.fs_sparecon,fs_u.fs_sun.fs_qbmask);
                /* Might fail on strictly aligning 64-bit big-endian
                 * architectures. Ouch!
                 */
        	be64_to_cpus((__u64 *) &usb->fs_u.fs_sun.fs_qbmask);
        	be64_to_cpus((__u64 *) &usb->fs_u.fs_sun.fs_qfmask);
	} else {
        	be16_to_cpus__between(*usb,fs_opostbl,fs_u.fs_44.fs_sparecon);
        	be32_to_cpus__between(*usb,fs_u.fs_sun.fs_sparecon,fs_u.fs_44.fs_maxfilesize);
        	be64_to_cpus((__u64 *) &usb->fs_u.fs_44.fs_maxfilesize);
        	be64_to_cpus((__u64 *) &usb->fs_u.fs_44.fs_qbmask);
        	be64_to_cpus((__u64 *) &usb->fs_u.fs_44.fs_qfmask);
        	be32_to_cpus((__s32 *) &usb->fs_u.fs_44.fs_state);
	}
	be32_to_cpus__between(*usb,fs_postblformat,fs_magic);
#endif
}
extern void ufs_superblock_le_to_cpus(struct ufs_superblock * usb) {
#ifndef __LITTLE_ENDIAN
	__u16 sb_type = 1;	/* SUN type superblock */

	if (usb->fs_u.fs_44.fs_maxsymlinklen >= 0)
		sb_type = 0;	/* 4.4BSD (FreeBSD) type superblock */
 
	le32_to_cpus__between(*usb,fs_link,fs_fmod);
        /* XXX - I dunno what to do w/ fs_csp,
         * but it is unused by the current code, so that's ok for now.
         */
	le32_to_cpus(&usb->fs_cpc);
	if (sb_type) {
        	le16_to_cpus__between(*usb,fs_opostbl,fs_u.fs_sun.fs_sparecon);
        	le32_to_cpus__between(*usb,fs_u.fs_sun.fs_sparecon,fs_u.fs_sun.fs_qbmask);
                /* Might fail on strictly aligning 64-bit big-endian
                 * architectures. Ouch!
                 */
        	le64_to_cpus((__u64 *) &usb->fs_u.fs_sun.fs_qbmask);
        	le64_to_cpus((__u64 *) &usb->fs_u.fs_sun.fs_qfmask);
	} else {
        	le16_to_cpus__between(*usb,fs_opostbl,fs_u.fs_44.fs_sparecon);
        	le32_to_cpus__between(*usb,fs_u.fs_sun.fs_sparecon,fs_u.fs_44.fs_maxfilesize);
        	le64_to_cpus((__u64 *) &usb->fs_u.fs_44.fs_maxfilesize);
        	le64_to_cpus((__u64 *) &usb->fs_u.fs_44.fs_qbmask);
        	le64_to_cpus((__u64 *) &usb->fs_u.fs_44.fs_qfmask);
        	le32_to_cpus((__s32 *) &usb->fs_u.fs_44.fs_state);
	}
	le32_to_cpus__between(*usb,fs_postblformat,fs_magic);
#endif
}

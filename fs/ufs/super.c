/*
 *  linux/fs/ufs/super.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * Copyright (C) 1996  Eddie C. Dost  (ecd@skynet.be)
 *
 * Kernel module support added on 96/04/26 by
 * Stefan Reinauer <stepan@home.culture.mipt.ru>
 *
 * Module usage counts added on 96/04/29 by
 * Gertjan van Wingerde <gertjan@cs.vu.nl>
 *
 * Clean swab support on 19970406 by
 * Francois-Rene Rideau <rideau@ens.fr>
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 *
 * NeXTstep support added on February 5th 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk>.
 *
 * write support Daniel Pirkl <daniel.pirkl@email.cz> 1998
 * 
 
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/locks.h>
#include <asm/uaccess.h>
#include <linux/malloc.h>

#include "swab.h"
#include "util.h"


#undef UFS_SUPER_DEBUG
#undef UFS_SUPER_DEBUG_MORE

#ifdef UFS_SUPER_DEBUG
#define UFSD(x) printk("(%s, %d), %s: ", __FILE__, __LINE__, __FUNCTION__); printk x;
#else
#define UFSD(x)
#endif

#ifdef UFS_SUPER_DEBUG_MORE
/*
 * Print contents of ufs_super_block, useful for debuging
 */
void ufs_print_super_stuff(struct ufs_super_block_first * usb1,
	struct ufs_super_block_second * usb2, 
	struct ufs_super_block_third * usb3, unsigned swab)
{
	printk("\nufs_print_super_stuff\n");
	printk("size of usb:    %lu\n", sizeof(struct ufs_super_block));
	printk("  magic:        0x%x\n", SWAB32(usb3->fs_magic));
	printk("  sblkno:       %u\n", SWAB32(usb1->fs_sblkno));
	printk("  cblkno:       %u\n", SWAB32(usb1->fs_cblkno));
	printk("  iblkno:       %u\n", SWAB32(usb1->fs_iblkno));
	printk("  dblkno:       %u\n", SWAB32(usb1->fs_dblkno));
	printk("  cgoffset:     %u\n", SWAB32(usb1->fs_cgoffset));
	printk("  ~cgmask:      0x%x\n", ~SWAB32(usb1->fs_cgmask));
	printk("  size:         %u\n", SWAB32(usb1->fs_size));
	printk("  dsize:        %u\n", SWAB32(usb1->fs_dsize));
	printk("  ncg:          %u\n", SWAB32(usb1->fs_ncg));
	printk("  bsize:        %u\n", SWAB32(usb1->fs_bsize));
	printk("  fsize:        %u\n", SWAB32(usb1->fs_fsize));
	printk("  frag:         %u\n", SWAB32(usb1->fs_frag));
	printk("  fragshift:    %u\n", SWAB32(usb1->fs_fragshift));
	printk("  ~fmask:       %u\n", ~SWAB32(usb1->fs_fmask));
	printk("  fshift:       %u\n", SWAB32(usb1->fs_fshift));
	printk("  sbsize:       %u\n", SWAB32(usb1->fs_sbsize));
	printk("  spc:          %u\n", SWAB32(usb1->fs_spc));
	printk("  cpg:          %u\n", SWAB32(usb1->fs_cpg));
	printk("  ipg:          %u\n", SWAB32(usb1->fs_ipg));
	printk("  fpg:          %u\n", SWAB32(usb1->fs_fpg));
	printk("  csaddr:       %u\n", SWAB32(usb1->fs_csaddr));
	printk("  cssize:       %u\n", SWAB32(usb1->fs_cssize));
	printk("  cgsize:       %u\n", SWAB32(usb1->fs_cgsize));
	printk("  fstodb:       %u\n", SWAB32(usb1->fs_fsbtodb));
	printk("  postblformat: %u\n", SWAB32(usb3->fs_postblformat));
	printk("  nrpos:        %u\n", SWAB32(usb3->fs_nrpos));
	printk("  ndir          %u\n", SWAB32(usb1->fs_cstotal.cs_ndir));
	printk("  nifree        %u\n", SWAB32(usb1->fs_cstotal.cs_nifree));
	printk("  nbfree        %u\n", SWAB32(usb1->fs_cstotal.cs_nbfree));
	printk("  nffree        %u\n", SWAB32(usb1->fs_cstotal.cs_nffree));
}


/*
 * Print contents of ufs_cylinder_group, useful for debuging
 */
void ufs_print_cylinder_stuff(struct ufs_cylinder_group *cg, unsigned swab)
{
	printk("\nufs_print_cylinder_stuff\n");
	printk("size of ucg: %lu\n", sizeof(struct ufs_cylinder_group));
	printk("  magic:       %x\n", SWAB32(cg->cg_magic));
	printk("  time:        %u\n", SWAB32(cg->cg_time));
	printk("  cgx:         %u\n", SWAB32(cg->cg_cgx));
	printk("  ncyl:        %u\n", SWAB16(cg->cg_ncyl));
	printk("  niblk:       %u\n", SWAB16(cg->cg_niblk));
	printk("  ndblk:       %u\n", SWAB32(cg->cg_ndblk));
	printk("  cs_ndir:     %u\n", SWAB32(cg->cg_cs.cs_ndir));
	printk("  cs_nbfree:   %u\n", SWAB32(cg->cg_cs.cs_nbfree));
	printk("  cs_nifree:   %u\n", SWAB32(cg->cg_cs.cs_nifree));
	printk("  cs_nffree:   %u\n", SWAB32(cg->cg_cs.cs_nffree));
	printk("  rotor:       %u\n", SWAB32(cg->cg_rotor));
	printk("  frotor:      %u\n", SWAB32(cg->cg_frotor));
	printk("  irotor:      %u\n", SWAB32(cg->cg_irotor));
	printk("  frsum:       %u, %u, %u, %u, %u, %u, %u, %u\n",
	    SWAB32(cg->cg_frsum[0]), SWAB32(cg->cg_frsum[1]),
	    SWAB32(cg->cg_frsum[2]), SWAB32(cg->cg_frsum[3]),
	    SWAB32(cg->cg_frsum[4]), SWAB32(cg->cg_frsum[5]),
	    SWAB32(cg->cg_frsum[6]), SWAB32(cg->cg_frsum[7]));
	printk("  btotoff:     %u\n", SWAB32(cg->cg_btotoff));
	printk("  boff:        %u\n", SWAB32(cg->cg_boff));
	printk("  iuseoff:     %u\n", SWAB32(cg->cg_iusedoff));
	printk("  freeoff:     %u\n", SWAB32(cg->cg_freeoff));
	printk("  nextfreeoff: %u\n", SWAB32(cg->cg_nextfreeoff));
}
#endif /* UFS_SUPER_DEBUG_MORE */

/*
 * Called while file system is mounted, read super block
 * and create important imtermal structures.
 */
struct super_block * ufs_read_super (
	struct super_block * sb,
	void * data,
	int silent)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_super_block_second * usb2;
	struct ufs_super_block_third * usb3;
	struct ufs_buffer_head * ubh;
	unsigned char * base, * space;
	unsigned size, blks, i;
	unsigned block_size, super_block_size;
	unsigned flags, swab;
	s64 tmp;
	static unsigned offsets[] = {0, 96, 160};  /* different superblock locations */
	            
	UFSD(("ENTER\n"))
	
	uspi = NULL;
	ubh = NULL;
	base = space = NULL;
	sb->u.ufs_sb.s_ucg = NULL;
	flags = 0;
	swab = 0;
	
	/* sb->s_dev and sb->s_flags are set by our caller
	 * data is the mystery argument to sys_mount()
	 *
	 * Our caller also sets s_dev, s_covered, s_rd_only, s_dirt,
 	 *   and s_type when we return.
	 */

	MOD_INC_USE_COUNT;
	lock_super (sb);

	sb->u.ufs_sb.s_uspi = uspi = 
		kmalloc (sizeof(struct ufs_sb_private_info), GFP_KERNEL);
	if (!uspi)
		goto failed;
	
	block_size = BLOCK_SIZE;
	super_block_size = BLOCK_SIZE * 2;
	
	uspi->s_fsize = block_size;
	uspi->s_fmask = ~(BLOCK_SIZE - 1);
	uspi->s_fshift = BLOCK_SIZE_BITS;
	uspi->s_sbsize = super_block_size;
	i = 0;
	uspi->s_sbbase = offsets[i];
	
again:	
	set_blocksize (sb->s_dev, block_size);

	/*
	 * read ufs super block from device
	 */
	ubh = ubh_bread2 (sb->s_dev, uspi->s_sbbase + UFS_SBLOCK/block_size, super_block_size);
	if (!ubh) 
		goto failed;
	
	usb1 = ubh_get_usb_first(USPI_UBH);
	usb2 = ubh_get_usb_second(USPI_UBH);
	usb3 = ubh_get_usb_third(USPI_UBH);
	
	/*
	 * Check ufs magic number
	 */
	if (usb3->fs_magic != UFS_MAGIC) {
		switch (le32_to_cpup(&usb3->fs_magic)) {
		case UFS_MAGIC:
			swab = UFS_LITTLE_ENDIAN; break;
		case UFS_CIGAM:
			swab = UFS_BIG_ENDIAN; break;
		default:
			/*
			 * Try another super block location
			 */
			if (++i < sizeof(offsets)/sizeof(unsigned)) {
				ubh_brelse2(ubh);
				ubh = NULL;
				uspi->s_sbbase = offsets[i];
				goto again;
			}
			else {
				printk("ufs_read_super: super block loacation not in { 0, 96, 160} or bad magic number\n");
				goto failed;
			}
		}
	}
	
	/*
	 * Check block and fragment sizes
	 */
	uspi->s_bsize = SWAB32(usb1->fs_bsize);
	uspi->s_fsize = SWAB32(usb1->fs_fsize);
	uspi->s_sbsize = SWAB32(usb1->fs_sbsize);

	if (uspi->s_bsize != 4096 && uspi->s_bsize != 8192) {
		printk("ufs_read_super: fs_bsize %u != {4096, 8192}\n", uspi->s_bsize);
		goto failed;
	}
	if (uspi->s_fsize != 512 && uspi->s_fsize != 1024) {
		printk("ufs_read_super: fs_fsize %u != {512, 1024}\n", uspi->s_fsize);
	        goto failed;
	}
	
	/*
	 * Block size is not 1024, set block_size to 512, 
	 * free buffers and read it again
	 */
	if (uspi->s_fsize != block_size || uspi->s_sbsize != super_block_size) {
		ubh_brelse2(ubh);
		ubh = NULL;
		uspi->s_fmask = SWAB32(usb1->fs_fmask);
		uspi->s_fshift = SWAB32(usb1->fs_fshift);
		goto again;
	}

#ifdef UFS_SUPER_DEBUG_MORE
	ufs_print_super_stuff (usb1, usb2, usb3, swab);
#endif
	/*
	 * Check file system type
	 */
	flags |= UFS_VANILLA;
	/* XXX more consistency check */
	UFSD(("ufs_read_super: maxsymlinklen 0x%8.8x\n", usb3->fs_u.fs_44.fs_maxsymlinklen))
	if (usb3->fs_u.fs_44.fs_maxsymlinklen >= 0) {
		if (usb3->fs_u.fs_44.fs_inodefmt >= UFS_44INODEFMT) {
			UFSD(("44BSD\n"))
			flags |= UFS_44BSD;
			sb->s_flags |= MS_RDONLY;
		} else {
			UFSD(("OLD\n"))
			sb->s_flags |= UFS_OLD;       /* 4.2BSD */
		}
	} else if (uspi->s_sbbase > 0) {
		UFSD(("NEXT\n"))
		flags |= UFS_NEXT;
		sb->s_flags |= MS_RDONLY;
	} else {
		UFSD(("SUN\n"))
		flags |= UFS_SUN;
        }

	/*
	 * Check, if file system was correctly unmounted.
	 * If not, make it read only.
	 */
        if (((flags & UFS_ST_MASK) == UFS_ST_44BSD) ||
            ((flags & UFS_ST_MASK) == UFS_ST_OLD) ||
            ((flags & UFS_ST_MASK) == UFS_ST_NEXT) ||
            (((flags & UFS_ST_MASK) == UFS_ST_SUN) &&
            ufs_state(usb3) == UFS_FSOK - usb1->fs_time)) {
		switch(usb1->fs_clean) {
		    case UFS_FSCLEAN:
			UFSD(("fs is clean\n"))
			break;
		    case UFS_FSSTABLE:
			UFSD(("fs is stable\n"))
			break;
		    case UFS_FSACTIVE:
			printk("ufs_read_super: fs is active\n");
			sb->s_flags |= MS_RDONLY;
			break;
		    case UFS_FSBAD:
			printk("ufs_read_super: fs is bad\n");
			sb->s_flags |= MS_RDONLY;
			break;
		    default:
			printk("ufs_read_super: can't grok fs_clean 0x%x\n",
				usb1->fs_clean);
			sb->s_flags |= MS_RDONLY;
			break;
		}
	} else {
		printk("ufs_read_super: fs needs fsck\n");
		sb->s_flags |= MS_RDONLY;
	}

	sb->s_flags &= ~MS_RDONLY;
	/*
	 * Read ufs_super_block into internal data structures
	 */
	sb->s_blocksize =  SWAB32(usb1->fs_fsize);
	sb->s_blocksize_bits = SWAB32(usb1->fs_fshift);
	sb->s_op = &ufs_super_ops;
	sb->dq_op = 0; /* XXX */
	sb->s_magic = SWAB32(usb3->fs_magic);

	uspi->s_sblkno = SWAB32(usb1->fs_sblkno);
	uspi->s_cblkno = SWAB32(usb1->fs_cblkno);
	uspi->s_iblkno = SWAB32(usb1->fs_iblkno);
	uspi->s_dblkno = SWAB32(usb1->fs_dblkno);
	uspi->s_cgoffset = SWAB32(usb1->fs_cgoffset);
	uspi->s_cgmask = SWAB32(usb1->fs_cgmask);
	uspi->s_size = SWAB32(usb1->fs_size);
	uspi->s_dsize = SWAB32(usb1->fs_dsize);
	uspi->s_ncg = SWAB32(usb1->fs_ncg);
	/* s_bsize already set */
	/* s_fsize already set */
	uspi->s_fpb = SWAB32(usb1->fs_frag);
        uspi->s_minfree = SWAB32(usb1->fs_minfree);
	uspi->s_bmask = SWAB32(usb1->fs_bmask);
	uspi->s_fmask = SWAB32(usb1->fs_fmask);
	uspi->s_bshift = SWAB32(usb1->fs_bshift);
	uspi->s_fshift = SWAB32(usb1->fs_fshift);
	uspi->s_fpbshift = SWAB32(usb1->fs_fragshift);
	uspi->s_fsbtodb = SWAB32(usb1->fs_fsbtodb);
	/* s_sbsize already set */
	uspi->s_csmask = SWAB32(usb1->fs_csmask);
	uspi->s_csshift = SWAB32(usb1->fs_csshift);
        uspi->s_nindir = SWAB32(usb1->fs_nindir);
	uspi->s_inopb = SWAB32(usb1->fs_inopb);
	uspi->s_nspf = SWAB32(usb1->fs_nspf);
	uspi->s_npsect = SWAB32(usb1->fs_npsect);
	uspi->s_interleave = SWAB32(usb1->fs_interleave);
	uspi->s_trackskew = SWAB32(usb1->fs_trackskew);
	uspi->s_csaddr = SWAB32(usb1->fs_csaddr);
	uspi->s_cssize = SWAB32(usb1->fs_cssize);
	uspi->s_cgsize = SWAB32(usb1->fs_cgsize);
	uspi->s_ntrak = SWAB32(usb1->fs_ntrak);
	uspi->s_nsect = SWAB32(usb1->fs_nsect);
	uspi->s_spc = SWAB32(usb1->fs_spc);
	uspi->s_ipg = SWAB32(usb1->fs_ipg);
	uspi->s_fpg = SWAB32(usb1->fs_fpg);
	uspi->s_cpc = SWAB32(usb2->fs_cpc);
        ((u32 *)&tmp)[0] = usb3->fs_u.fs_sun.fs_qbmask[0];
        ((u32 *)&tmp)[1] = usb3->fs_u.fs_sun.fs_qbmask[1];
	uspi->s_qbmask = SWAB64(tmp);
        ((u32 *)&tmp)[0] = usb3->fs_u.fs_sun.fs_qfmask[0];
        ((u32 *)&tmp)[1] = usb3->fs_u.fs_sun.fs_qfmask[1];
	uspi->s_qfmask = SWAB64(tmp);
	uspi->s_postblformat = SWAB32(usb3->fs_postblformat);
	uspi->s_nrpos = SWAB32(usb3->fs_nrpos);
	uspi->s_postbloff = SWAB32(usb3->fs_postbloff);
	uspi->s_rotbloff = SWAB32(usb3->fs_rotbloff);

	/*
	 * Compute another fraquently used values
	 */
	uspi->s_fpbmask = uspi->s_fpb - 1;
	uspi->s_apbshift = uspi->s_bshift - 2;
	uspi->s_2apbshift = uspi->s_apbshift * 2;
	uspi->s_3apbshift = uspi->s_apbshift * 3;
	uspi->s_apb = 1 << uspi->s_apbshift;
	uspi->s_2apb = 1 << uspi->s_2apbshift;
	uspi->s_3apb = 1 << uspi->s_3apbshift;
	uspi->s_apbmask = uspi->s_apb - 1;
 	uspi->s_nspfshift = uspi->s_fshift - SECTOR_BITS;
	uspi->s_nspb = uspi->s_nspf << uspi->s_fpbshift;
	uspi->s_inopf = uspi->s_inopb >> uspi->s_fpbshift;
		
	sb->u.ufs_sb.s_flags = flags;
	sb->u.ufs_sb.s_swab = swab;
	sb->u.ufs_sb.s_rename_lock = 0;
	sb->u.ufs_sb.s_rename_wait = NULL;
         	                                                          
	sb->s_root = d_alloc_root(iget(sb, UFS_ROOTINO), NULL);

	/*
	 * Read cs structures from (usually) first data block
	 * on the device. 
	 */
	size = uspi->s_cssize;
	blks = howmany(size, uspi->s_fsize);
	base = space = kmalloc(size, GFP_KERNEL);
	if (!base)
		goto failed; 
	for (i = 0; i < blks; i += uspi->s_fpb) {
		size = uspi->s_bsize;
		if (i + uspi->s_fpb > blks)
			size = (blks - i) * uspi->s_fsize;
		ubh = ubh_bread(sb->s_dev, uspi->s_csaddr + i, size);
		if (!ubh)
			goto failed;
		ubh_ubhcpymem (space, ubh, size);
		sb->u.ufs_sb.s_csp[ufs_fragstoblks(i)] = (struct ufs_csum *)space;
		space += size;
		ubh_brelse (ubh);
		ubh = NULL;
	}

	/*
	 * Read cylinder group (we read only first fragment from block
	 * at this time) and prepare internal data structures for cg caching.
	 */
	if (!(sb->u.ufs_sb.s_ucg = kmalloc (sizeof(struct buffer_head *) * uspi->s_ncg, GFP_KERNEL)))
		goto failed;
	for (i = 0; i < uspi->s_ncg; i++) 
		sb->u.ufs_sb.s_ucg[i] = NULL;
	for (i = 0; i < UFS_MAX_GROUP_LOADED; i++) {
		sb->u.ufs_sb.s_ucpi[i] = NULL;
		sb->u.ufs_sb.s_cgno[i] = UFS_CGNO_EMPTY;
	}
	for (i = 0; i < uspi->s_ncg; i++) {
		UFSD(("read cg %u\n", i))
		if (!(sb->u.ufs_sb.s_ucg[i] = bread (sb->s_dev, ufs_cgcmin(i), sb->s_blocksize)))
			goto failed;
		if (!ufs_cg_chkmagic ((struct ufs_cylinder_group *) sb->u.ufs_sb.s_ucg[i]->b_data))
			goto failed;
#ifdef UFS_SUPER_DEBUG_MORE
		ufs_print_cylinder_stuff((struct ufs_cylinder_group *) sb->u.ufs_sb.s_ucg[i]->b_data, swab);
#endif
	}
	for (i = 0; i < UFS_MAX_GROUP_LOADED; i++) {
		if (!(sb->u.ufs_sb.s_ucpi[i] = kmalloc (sizeof(struct ufs_cg_private_info), GFP_KERNEL)))
			goto failed;
		sb->u.ufs_sb.s_cgno[i] = UFS_CGNO_EMPTY;
	}
	sb->u.ufs_sb.s_cg_loaded = 0;

	unlock_super(sb);
	UFSD(("EXIT\n"))
	return(sb);

failed:
	if (ubh) ubh_brelse2 (ubh);
	if (uspi) kfree (uspi);
	if (base) kfree (base);

	if (sb->u.ufs_sb.s_ucg) {
		for (i = 0; i < uspi->s_ncg; i++)
			if (sb->u.ufs_sb.s_ucg[i]) brelse (sb->u.ufs_sb.s_ucg[i]);
		kfree (sb->u.ufs_sb.s_ucg);
		for (i = 0; i < UFS_MAX_GROUP_LOADED; i++)
			if (sb->u.ufs_sb.s_ucpi[i]) kfree (sb->u.ufs_sb.s_ucpi[i]);
	}
	sb->s_dev = 0;
	unlock_super (sb);
	MOD_DEC_USE_COUNT;
	UFSD(("EXIT (FAILED)\n"))
	return(NULL);
}

/*
 * Put super block, release internal structures
 */
void ufs_put_super (struct super_block * sb)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_buffer_head * ubh;
	unsigned char * base, * space;
	unsigned size, blks, i;
	
	UFSD(("ENTER\n"))
	
	uspi = sb->u.ufs_sb.s_uspi;
	size = uspi->s_cssize;
	blks = howmany(size, uspi->s_fsize);
	base = space = (char*) sb->u.ufs_sb.s_csp[0];
	for (i = 0; i < blks; i += uspi->s_fpb) {
		size = uspi->s_bsize;
		if (i + uspi->s_fpb > blks)
			size = (blks - i) * uspi->s_fsize;
		ubh = ubh_bread (sb->s_dev, uspi->s_csaddr + i, size);
		if (!ubh)
			goto go_on;
		ubh_memcpyubh (ubh, space, size);
		space += size;
		ubh_mark_buffer_uptodate (ubh, 1);
		ubh_mark_buffer_dirty (ubh, 0);
		ubh_brelse (ubh);
	}

go_on:
	for (i = 0; i < UFS_MAX_GROUP_LOADED; i++) {
		ufs_put_cylinder (sb, i);
		kfree (sb->u.ufs_sb.s_ucpi[i]);
	}
	for (i = 0; i < uspi->s_ncg; i++) 
		brelse (sb->u.ufs_sb.s_ucg[i]);
	kfree (sb->u.ufs_sb.s_ucg);
	kfree (base);
	ubh_brelse2 (USPI_UBH);
	kfree (sb->u.ufs_sb.s_uspi);
	sb->s_dev = 0;
	MOD_DEC_USE_COUNT;
	return;
}

/*
 * Write super block to device
 */
void ufs_write_super (struct super_block * sb) {
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_super_block_third * usb3;
	unsigned swab;
	
	UFSD(("ENTER\n"))
	swab = sb->u.ufs_sb.s_swab;
	uspi = sb->u.ufs_sb.s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);
	usb3 = ubh_get_usb_third(USPI_UBH);
	
	if (!(sb->s_flags & MS_RDONLY)) {
		if (SWAB16(usb3->fs_u.fs_sun.fs_state) & UFS_FSOK)
			usb3->fs_u.fs_sun.fs_state = SWAB16(SWAB16(usb3->fs_u.fs_sun.fs_state) & ~UFS_FSOK);
		usb1->fs_time = SWAB32(CURRENT_TIME);
		usb3->fs_u.fs_sun.fs_state = SWAB32(UFS_FSOK - SWAB32(usb1->fs_time));
		ubh_mark_buffer_dirty (USPI_UBH, 1);
	}
	sb->s_dirt = 0;
	UFSD(("EXIT\n"))
}

/*
 * Copy some info about file system to user
 */
int ufs_statfs(struct super_block * sb, struct statfs * buf, int bufsiz)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct statfs tmp;
	struct statfs *sp = &tmp;
	unsigned long used, avail;
	unsigned swab;
	
	UFSD(("ENTER\n"))
	
	swab = sb->u.ufs_sb.s_swab;
	uspi = sb->u.ufs_sb.s_uspi;
	usb1 = ubh_get_usb_first (USPI_UBH);

	sp->f_type = UFS_MAGIC;
	sp->f_bsize = sb->s_blocksize;
	sp->f_blocks = uspi->s_dsize;
	sp->f_bfree = (SWAB32(usb1->fs_cstotal.cs_nbfree) << uspi->s_fpbshift )+
		SWAB32(usb1->fs_cstotal.cs_nffree);

	avail = sp->f_blocks - (sp->f_blocks / 100) * uspi->s_minfree;
	used = sp->f_blocks - sp->f_bfree;
	if (avail > used)
		sp->f_bavail = avail - used;
	else
		sp->f_bavail = 0;
	sp->f_files = uspi->s_ncg * uspi->s_ipg;
	sp->f_ffree = SWAB32(usb1->fs_cstotal.cs_nifree);
	sp->f_fsid.val[0] = SWAB32(usb1->fs_id[0]);
	sp->f_fsid.val[1] = SWAB32(usb1->fs_id[1]);
	sp->f_namelen = UFS_MAXNAMLEN;
	
	UFSD(("EXIT\n"))

	return copy_to_user(buf, sp, bufsiz) ? -EFAULT : 0;
}


static char error_buf[1024];

void ufs_warning (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	va_list args;

	va_start (args, fmt);
	vsprintf (error_buf, fmt, args);
	va_end (args);
	printk (KERN_WARNING "UFS-fs warning (device %s): %s: %s\n",
		kdevname(sb->s_dev), function, error_buf);
}

void ufs_error (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	va_list args;

	uspi = sb->u.ufs_sb.s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);
	
	if (!(sb->s_flags & MS_RDONLY)) {
		usb1->fs_clean = UFS_FSBAD;
		ubh_mark_buffer_dirty(USPI_UBH, 1);
		sb->s_dirt = 1;
		sb->s_flags |= MS_RDONLY;
	}
	va_start (args, fmt);
	vsprintf (error_buf, fmt, args);
	va_end (args);
	printk (KERN_CRIT "UFS-fs error (device %s): %s: %s\n",
		kdevname(sb->s_dev), function, error_buf);
}

void ufs_panic (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	va_list args;
	
	uspi = sb->u.ufs_sb.s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);
	
	if (!(sb->s_flags & MS_RDONLY)) {
		usb1->fs_clean = UFS_FSBAD;
		ubh_mark_buffer_dirty(USPI_UBH, 1);
		sb->s_dirt = 1;
	}
	va_start (args, fmt);
	vsprintf (error_buf, fmt, args);
	va_end (args);
	/* this is to prevent panic from syncing this filesystem */
	if (sb->s_lock)
		sb->s_lock = 0;
	sb->s_flags |= MS_RDONLY;
	printk (KERN_CRIT "UFS-fs panic (device %s): %s: %s\n",
		kdevname(sb->s_dev), function, error_buf);
/*	panic ("UFS-fs panic (device %s): %s: %s\n", 
		kdevname(sb->s_dev), function, error_buf);
*/
}


static struct super_operations ufs_super_ops = {
	ufs_read_inode,
	ufs_write_inode,
	ufs_put_inode,
	ufs_delete_inode,
	NULL,			/* notify_change() */
	ufs_put_super,
	ufs_write_super,
	ufs_statfs,
	NULL,			/* XXX - ufs_remount() */
};

static struct file_system_type ufs_fs_type = {
	"ufs",
	FS_REQUIRES_DEV,
	ufs_read_super,
	NULL
};


int init_ufs_fs(void)
{
	return(register_filesystem(&ufs_fs_type));
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	return init_ufs_fs();
}

void cleanup_module(void)
{
	unregister_filesystem(&ufs_fs_type);
}
#endif


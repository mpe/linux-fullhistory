/* $Id: fb.h,v 1.27 1997/06/06 10:56:28 jj Exp $
 * fb.h: contains the definitions of the structures that various sun
 *       frame buffer can use to do console driver stuff.
 *
 * (C) 1996 Dave Redman     (djhr@tadpole.co.uk)
 * (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * (C) 1996 David Miller    (davem@rutgers.edu)
 * (C) 1996 Peter Zaitcev   (zaitcev@lab.ipmce.su)
 * (C) 1996 Eddie C. Dost   (ecd@skynet.be)
 * (C) 1996 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 */

#ifndef __SPARC_FB_H_
#define __SPARC_FB_H_

#include <linux/init.h>

#define FRAME_BUFFERS    8
#define CHAR_WIDTH	 8
#define CHAR_HEIGHT	 16

/* Change this if we run into problems if the kernel want's to free or
 * use our frame buffer pages, never seen it though.
 */
#define FB_MMAP_VM_FLAGS (VM_SHM| VM_LOCKED)

#undef color

/* cursor status, kernel tracked copy */
struct cg_cursor {
        short   enable;	        /* cursor is enabled */
        struct  fbcurpos cpos;	/* position */
        struct  fbcurpos chot;	/* hot-spot */
        struct  fbcurpos size;	/* size of mask & image fields */
        struct	fbcurpos hwsize; /* hw max size */
        int     bits[2][32];	/* space for mask & image bits */
	char    color [6];	/* cursor colors */
};

struct cg6_info {
	struct bt_regs *bt;	/* color control */
	struct cg6_fbc *fbc;
	unsigned int   *fhc;
	struct cg6_tec *tec;
	struct cg6_thc *thc;
	void           *dhc;
	unsigned char  *rom;
};

struct tcx_info {
	struct bt_regs *bt;	/* color control */
	struct tcx_tec *tec;
	struct tcx_thc *thc;
	void *tcx_cplane;
	int tcx_sizes[13];
	long tcx_offsets[13];
	int lowdepth;
};

struct leo_info {
	struct leo_cursor *cursor;
	struct leo_lc_ss0_krn *lc_ss0_krn;
	struct leo_lc_ss0_usr *lc_ss0_usr;
	struct leo_lc_ss1_krn *lc_ss1_krn;
	struct leo_lc_ss1_usr *lc_ss1_usr;
	struct leo_ld_ss0 *ld_ss0;
	struct leo_ld_ss1 *ld_ss1;
	struct leo_ld_gbl *ld_gbl;
	struct leo_lx_krn *lx_krn;
	u32	*cluts[3];
	u8	*xlut;
	unsigned long offset;
};

struct bwtwo_info {
        struct bwtwo_regs *regs;
};

struct cg3_info {
	struct cg3_regs *regs;	/* brooktree (color) registers, and more */
	int cgrdi;		/* 1 if this is a cgRDI */
};

struct cg14_info {
    struct cg14_regs   *regs;
    struct cg14_cursor *cursor_regs;
    struct cg14_dac    *dac;
    struct cg14_xlut   *xlut;
    struct cg14_clut   *clut;
    int    ramsize;
    int    video_mode;
};

typedef union
{
	unsigned int	bt[8];
	unsigned char	ibm[8];
} dacptr;
    
struct weitek_info
{
	int		p9000;		/* p9000? or p9100 */
	dacptr		*dac;		/* dac structures */
	unsigned int	fbsize;		/* size of frame buffer */
};

/* Array holding the information for the frame buffers */
typedef struct fbinfo {
	union {
		struct bwtwo_info bwtwo;
		struct cg3_info   cg3;
		struct cg6_info   cg6;
		struct cg14_info  cg14;
		struct tcx_info   tcx;
		struct leo_info	  leo;
	} info;		        /* per frame information */
	int    space;           /* I/O space this card resides in */
	int    blanked;		/* true if video blanked */
	int    open;		/* is this fb open? */
	int    mmaped;		/* has this fb been mmapped? */
	int    vtconsole;	/* virtual console where it is opened */
	long   base;		/* frame buffer base    */
	struct fbtype type;	/* frame buffer type    */
	int    real_type;	/* real frame buffer FBTYPE* */
	int    emulations[4];   /* possible emulations (-1 N/A) */
	int    prom_node;	/* node of the device in prom tree */
	int    base_depth;	/* depth of fb->base piece */
	struct cg_cursor cursor;	/* kernel state of hw cursor */
	int    (*mmap)(struct inode *, struct file *, struct vm_area_struct *,
		       long fb_base, struct fbinfo *);
	void   (*loadcmap)(struct fbinfo *fb, int index, int count);
	void   (*blank)(struct fbinfo *fb);
	void   (*unblank)(struct fbinfo *fb);
	int    (*ioctl)(struct inode *, struct file *, uint, unsigned long,
			struct fbinfo *);
	void   (*reset)(struct fbinfo *fb);
	void   (*switch_from_graph)(void);
	void   (*setcursor)(struct fbinfo *);
	void   (*setcurshape)(struct fbinfo *);
	void   (*setcursormap)(struct fbinfo *, unsigned char *, 
			unsigned char *, unsigned char *);
	unsigned long (*postsetup)(struct fbinfo *, unsigned long);
	void	(*blitc)(unsigned short, int, int);
	void	(*setw)(int, int, unsigned short, int);
	void	(*cpyw)(int, int, unsigned short *, int);
	void	(*fill)(int, int, int *);
	unsigned char *color_map;
	struct openpromfs_dev proc_entry;
} fbinfo_t;

#define CM(i, j) [3*(i)+(j)]

extern unsigned char reverse_color_table[];

#define CHARATTR_TO_SUNCOLOR(attr) \
	((reverse_color_table[(attr) >> 12] << 4) | \
	  reverse_color_table[((attr) >> 8) & 0x0f])

extern fbinfo_t *fbinfo;
extern int fbinfos;

struct {
	char *name;		/* prom name */
	int  width, height;	/* prefered w,h match */
	void (*fbtype)(fbinfo_t *); /* generic device type */
	/* device specific init routine  */
	unsigned long (*fbinit)(fbinfo_t *fbinfo, unsigned int addr);
} fb_entry;

extern int fb_init(void);

extern void (*fb_restore_palette)(fbinfo_t *fbinfo);
extern void (*fb_hide_cursor)(int cursor_pos);
extern void (*fb_set_cursor)(int oldpos,  int idx);
extern void (*fb_clear_screen)( void );
extern void (*fb_blitc)(unsigned char *, int, unsigned int *, unsigned int);
extern void (*fb_font_init)(unsigned char *font);
/* All framebuffers are likely to require this info */

/* Screen dimensions and color depth. */
extern int con_depth, con_width;
extern int con_height, con_linebytes;
extern int ints_per_line;

/* used in the mmap routines */
extern unsigned long get_phys (unsigned long addr);
extern int get_iospace (unsigned long addr);
extern void render_screen(void);

extern void sun_hw_hide_cursor(void);
extern void sun_hw_set_cursor(int, int);
extern int sun_hw_scursor(struct fbcursor *,fbinfo_t *);
extern int sun_hw_cursor_shown;
extern int sun_prom_console_id;

extern unsigned long sun_cg_postsetup(fbinfo_t *, unsigned long);

#define FB_DEV(x) (MINOR(x) / 32)

extern void cg3_setup (fbinfo_t *, int, u32, int, struct linux_sbus_device *);
extern void cg6_setup (fbinfo_t *, int, u32, int);
extern void cg14_setup (fbinfo_t *, int, int, u32, int);
extern void bwtwo_setup (fbinfo_t *, int, u32, int,
			 struct linux_sbus_device *);
extern void leo_setup (fbinfo_t *, int, u32, int);
extern void tcx_setup (fbinfo_t *, int, int, u32, struct linux_sbus_device *);
extern void creator_setup (fbinfo_t *, int, int, unsigned long, int);
extern int io_remap_page_range(unsigned long from, unsigned long offset, unsigned long size, pgprot_t prot, int space);

#endif __SPARC_FB_H_

#include <asm/sbus.h>
#include <asm/oplib.h>
#include <asm/fbio.h>

#include "fbcon.h"

struct bt_regs {
	volatile unsigned int addr;           /* address register */
	volatile unsigned int color_map;      /* color map */
	volatile unsigned int control;        /* control register */
	volatile unsigned int cursor;         /* cursor map register */
};

struct fb_info_creator {
	struct ffb_fbc *fbc;
	struct ffb_dac *dac;
	int dac_rev;
	int xy_margin;
};
struct fb_info_cgsix {
	struct bt_regs *bt;
	struct cg6_fbc *fbc;
	struct cg6_thc *thc;
	struct cg6_tec *tec;
	volatile u32 *fhc;
};
struct fb_info_bwtwo {
	struct bw2_regs *regs;
};
struct fb_info_cgthree {
	struct cg3_regs *regs;
};
struct fb_info_tcx {
	struct bt_regs *bt;
	struct tcx_thc *thc;
	struct tcx_tec *tec;
	u32 *cplane;
};

struct cg_cursor {
	short	enable;         /* cursor is enabled */
	struct	fbcurpos cpos;  /* position */
	struct	fbcurpos chot;  /* hot-spot */
	struct	fbcurpos size;  /* size of mask & image fields */
	struct	fbcurpos hwsize; /* hw max size */
	int	bits[2][128];   /* space for mask & image bits */
	char	color [6];      /* cursor colors */
};

struct sbus_mmap_map {
	unsigned long voff;
	unsigned long poff;
	unsigned long size;
};

#define SBUS_MMAP_FBSIZE(n) (-n)
#define SBUS_MMAP_EMPTY	0x80000000

struct fb_info_sbusfb {
	struct fb_info info;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	struct display disp;
	struct display_switch dispsw;
	struct fbtype type;
	struct linux_sbus_device *sbdp;
	int prom_node, prom_parent;
	union {
		struct fb_info_creator ffb;
		struct fb_info_cgsix cg6;
		struct fb_info_bwtwo bw2;
		struct fb_info_cgthree cg3;
		struct fb_info_tcx tcx;
	} s;
	unsigned char *color_map;
	struct cg_cursor cursor;
	unsigned char hw_cursor_shown;
	unsigned char open;
	unsigned char mmaped;
	unsigned char blanked;
	int x_margin;
	int y_margin;
	int vtconsole;
	int consolecnt;
	int emulations[4];
	struct sbus_mmap_map *mmap_map;
	unsigned long physbase;
	int iospace;
	/* Methods */
	void (*setup)(struct display *);
	void (*setcursor)(struct fb_info_sbusfb *);
	void (*setcurshape)(struct fb_info_sbusfb *);
	void (*setcursormap)(struct fb_info_sbusfb *, unsigned char *, unsigned char *, unsigned char *);
	void (*loadcmap)(struct fb_info_sbusfb *, int, int);
	void (*blank)(struct fb_info_sbusfb *);
	void (*unblank)(struct fb_info_sbusfb *);
	void (*margins)(struct fb_info_sbusfb *, struct display *, int, int);
	void (*reset)(struct fb_info_sbusfb *);
	void (*fill)(struct fb_info_sbusfb *, struct display *, int, int, unsigned short *);
	void (*switch_from_graph)(struct fb_info_sbusfb *);
	void (*restore_palette)(struct fb_info_sbusfb *);
};

extern char *creatorfb_init(struct fb_info_sbusfb *);
extern char *cgsixfb_init(struct fb_info_sbusfb *);
extern char *cgthreefb_init(struct fb_info_sbusfb *);
extern char *tcxfb_init(struct fb_info_sbusfb *);
extern char *leofb_init(struct fb_info_sbusfb *);
extern char *bwtwofb_init(struct fb_info_sbusfb *);
extern char *cgfourteenfb_init(struct fb_info_sbusfb *);

#define sbusfbinfod(disp) ((struct fb_info_sbusfb *)(disp->fb_info))
#define sbusfbinfo(info) ((struct fb_info_sbusfb *)(info))
#define CM(i, j) [3*(i)+(j)]

#define SBUSFBINIT_SIZECHANGE ((char *)-1)

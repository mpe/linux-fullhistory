#ifndef __LINUX_FBIO_H
#define __LINUX_FBIO_H

/* Constants used for fbio SunOS compatibility -miguel */

/* Frame buffer types */
#define FBTYPE_NOTYPE           -1
#define FBTYPE_SUN1BW           0   /* mono */
#define FBTYPE_SUN1COLOR        1 
#define FBTYPE_SUN2BW           2 
#define FBTYPE_SUN2COLOR        3 
#define FBTYPE_SUN2GP           4 
#define FBTYPE_SUN5COLOR        5 
#define FBTYPE_SUN3COLOR        6 
#define FBTYPE_MEMCOLOR         7 
#define FBTYPE_SUN4COLOR        8 
 
#define FBTYPE_NOTSUN1          9 
#define FBTYPE_NOTSUN2          10
#define FBTYPE_NOTSUN3          11
 
#define FBTYPE_SUNFAST_COLOR    12  /* cg6 */
#define FBTYPE_SUNROP_COLOR     13
#define FBTYPE_SUNFB_VIDEO      14
#define FBTYPE_SUNGIFB          15
#define FBTYPE_SUNGPLAS         16
#define FBTYPE_SUNGP3           17
#define FBTYPE_SUNGT            18
#define FBTYPE_SUNLEO           19      /* zx Leo card */
#define FBTYPE_MDICOLOR         20      /* cg14 */
#define FBTYPE_LASTPLUSONE      21

/* fbio ioctls */
/* Returned by FBIOGTYPE */
struct  fbtype {
        int     fb_type;        /* fb type, see above */
        int     fb_height;      /* pixels */
        int     fb_width;       /* pixels */
        int     fb_depth;
        int     fb_cmsize;      /* color map entries */
        int     fb_size;        /* fb size in bytes */
};
#define FBIOGTYPE _IOR('F', 0, struct fbtype)

/* Used by FBIOPUTCMAP */
struct  fbcmap {
        int             index;          /* first element (0 origin) */
        int             count;
        unsigned char   *red;
        unsigned char   *green;
        unsigned char   *blue;
};

#define FBIOPUTCMAP _IOW('F', 3, struct fbcmap)

/* # of device specific values */
#define FB_ATTR_NDEVSPECIFIC    8
/* # of possible emulations */
#define FB_ATTR_NEMUTYPES       4
 
struct fbsattr {
        int     flags;
        int     emu_type;	/* -1 if none */
        int     dev_specific[FB_ATTR_NDEVSPECIFIC];
};
 
struct fbgattr {
        int     real_type;	/* real frame buffer type */
        int     owner;		/* unknown */
        struct fbtype fbtype;	/* real frame buffer fbtype */
        struct fbsattr sattr;   
        int     emu_types[FB_ATTR_NEMUTYPES]; /* supported emulations */
};
#define FBIOSATTR  _IOW('F', 5, struct fbgattr) /* Unsupported: */
#define FBIOGATTR  _IOR('F', 6, struct fbgattr)	/* supported */

#define FBIOSVIDEO _IOW('F', 7, int)
#define FBIOGVIDEO _IOR('F', 8, int)

/* Cursor position */
struct fbcurpos {
#ifdef __KERNEL__
	short fbx, fby;
#else
        short x, y;
#endif
};

/* Cursor operations */
#define FB_CUR_SETCUR   0x01	/* Enable/disable cursor display */
#define FB_CUR_SETPOS   0x02	/* set cursor position */
#define FB_CUR_SETHOT   0x04	/* set cursor hotspot */
#define FB_CUR_SETCMAP  0x08	/* set color map for the cursor */
#define FB_CUR_SETSHAPE 0x10	/* set shape */
#define FB_CUR_SETALL   0x1F	/* all of the above */

struct fbcursor {
        short set;              /* what to set, choose from the list above */
        short enable;           /* cursor on/off */
        struct fbcurpos pos;    /* cursor position */
        struct fbcurpos hot;    /* cursor hot spot */
        struct fbcmap cmap;     /* color map info */
        struct fbcurpos size;   /* cursor bit map size */
        char *image;            /* cursor image bits */
        char *mask;             /* cursor mask bits */
};

/* set/get cursor attributes/shape */
#define FBIOSCURSOR     _IOW('F', 24, struct fbcursor)
#define FBIOGCURSOR     _IOWR('F', 25, struct fbcursor)
 
/* set/get cursor position */
#define FBIOSCURPOS     _IOW('F', 26, struct fbcurpos)
#define FBIOGCURPOS     _IOW('F', 27, struct fbcurpos)
 
/* get max cursor size */
#define FBIOGCURMAX     _IOR('F', 28, struct fbcurpos)
 
#ifdef __KERNEL__
/* Addresses on the fd of a cgsix that are mappable */
#define CG6_FBC    0x70000000
#define CG6_TEC    0x70001000
#define CG6_BTREGS 0x70002000
#define CG6_FHC    0x70004000
#define CG6_THC    0x70005000
#define CG6_ROM    0x70006000
#define CG6_RAM    0x70016000
#define CG6_DHC    0x80000000

#define CG3_MMAP_OFFSET 0x4000000
#endif /* KERNEL */

#endif /* __LINUX_FBIO_H */

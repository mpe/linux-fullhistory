/*
 * linux/drivers/video/cyber2000fb.h
 *
 * Integraphics Cyber2000 frame buffer device
 */

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))

#define cyber2000_outb(dat,reg)	writeb(dat, CyberRegs + reg)
#define cyber2000_outw(dat,reg)	writew(dat, CyberRegs + reg)
#define cyber2000_outl(dat,reg)	writel(dat, CyberRegs + reg)

#define cyber2000_inb(reg)	readb(CyberRegs + reg)
#define cyber2000_inw(reg)	readw(CyberRegs + reg)
#define cyber2000_inl(reg)	readl(CyberRegs + reg)

static inline void cyber2000_crtcw(int reg, int val)
{
	cyber2000_outb(reg, 0x3d4);
	cyber2000_outb(val, 0x3d5);
}

static inline void cyber2000_grphw(int reg, int val)
{
	cyber2000_outb(reg, 0x3ce);
	cyber2000_outb(val, 0x3cf);
}

static inline void cyber2000_attrw(int reg, int val)
{
	cyber2000_inb(0x3da);
	cyber2000_outb(reg, 0x3c0);
	cyber2000_inb(0x3c1);
	cyber2000_outb(val, 0x3c0);
}

static inline void cyber2000_seqw(int reg, int val)
{
	cyber2000_outb(reg, 0x3c4);
	cyber2000_outb(val, 0x3c5);
}

struct cyber2000fb_par {
	char *		screen_base;
	unsigned long	screen_base_p;
	unsigned long	regs_base;
	unsigned long	regs_base_p;
	unsigned long	screen_end;
	unsigned long	screen_size;
	unsigned int	palette_size;
	  signed int	currcon;
	char		dev_name[32];
	unsigned int	initialised;

	/*
	 * palette
	 */
	struct {
		u8			red;
		u8			green;
		u8			blue;
	} palette[256];
	/*
	 * colour mapping table
	 */
	union {
#ifdef FBCON_HAS_CFB16
		u16			cfb16[16];
#endif
#ifdef FBCON_HAS_CFB24
		u32			cfb24[16];
#endif
	} c_table;
};

#define PIXFORMAT_8BPP		0
#define PIXFORMAT_16BPP		1
#define PIXFORMAT_24BPP		2

#define VISUALID_256		1
#define VISUALID_64K		2
#define VISUALID_16M		4
#define VISUALID_32K		6

#define CO_CMD_L_PATTERN_FGCOL	0x8000
#define CO_CMD_L_INC_LEFT	0x0004
#define CO_CMD_L_INC_UP		0x0002

#define CO_CMD_H_SRC_PIXMAP	0x2000
#define CO_CMD_H_BLITTER	0x0800

#define CO_REG_CONTROL		0xbf011
#define CO_REG_SRC_WIDTH	0xbf018
#define CO_REG_PIX_FORMAT	0xbf01c
#define CO_REG_FORE_MIX		0xbf048
#define CO_REG_FOREGROUND	0xbf058
#define CO_REG_WIDTH		0xbf060
#define CO_REG_HEIGHT		0xbf062
#define CO_REG_X_PHASE		0xbf078
#define CO_REG_CMD_L		0xbf07c
#define CO_REG_CMD_H		0xbf07e
#define CO_REG_SRC_PTR		0xbf170
#define CO_REG_DEST_PTR		0xbf178
#define CO_REG_DEST_WIDTH	0xbf218

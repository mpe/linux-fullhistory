/*
 * linux/drivers/video/cyber2000fb.h
 *
 * Integraphics Cyber2000 frame buffer device
 */

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))
#define cyber2000_outb(dat,reg)	(CyberRegs[reg] = dat)
#define cyber2000_outw(dat,reg)	(*(unsigned short *)&CyberRegs[reg] = dat)
#define cyber2000_outl(dat,reg)	(*(unsigned long *)&CyberRegs[reg] = dat)

#define cyber2000_inb(reg)	(CyberRegs[reg])
#define cyber2000_inw(reg)	(*(unsigned short *)&CyberRegs[reg])
#define cyber2000_inl(reg)	(*(unsigned long *)&CyberRegs[reg])

static inline void cyber2000_crtcw(int val, int reg)
{
	cyber2000_outb(reg, 0x3d4);
	cyber2000_outb(val, 0x3d5);
}

static inline void cyber2000_grphw(int val, int reg)
{
	cyber2000_outb(reg, 0x3ce);
	cyber2000_outb(val, 0x3cf);
}

static inline void cyber2000_attrw(int val, int reg)
{
	cyber2000_inb(0x3da);
	cyber2000_outb(reg, 0x3c0);
	cyber2000_inb(0x3c1);
	cyber2000_outb(val, 0x3c0);
}

static inline void cyber2000_seqw(int val, int reg)
{
	cyber2000_outb(reg, 0x3c4);
	cyber2000_outb(val, 0x3c5);
}

struct cyber2000fb_par {
	unsigned long	screen_base;
	unsigned long	screen_base_p;
	unsigned long	regs_base;
	unsigned long	regs_base_p;
	unsigned long	screen_end;
	unsigned long	screen_size;
	unsigned int	palette_size;
	  signed int	currcon;
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

struct res {
	int	xres;
	int	yres;
	unsigned char crtc_regs[18];
	unsigned char crtc_ofl;
	unsigned char clk_regs[4];
};

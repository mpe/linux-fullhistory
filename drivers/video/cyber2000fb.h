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
	unsigned int	dev_id;

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

#define K_CAP_X2_CTL1		0x49

#define CAP_X_START		0x60
#define CAP_X_END		0x62
#define CAP_Y_START		0x64
#define CAP_Y_END		0x66
#define CAP_DDA_X_INIT		0x68
#define CAP_DDA_X_INC		0x6a
#define CAP_DDA_Y_INIT		0x6c
#define CAP_DDA_Y_INC		0x6e

#define EXT_FIFO_CTL		0x74

#define CAP_PIP_X_START		0x80
#define CAP_PIP_X_END		0x82
#define CAP_PIP_Y_START		0x84
#define CAP_PIP_Y_END		0x86

#define CAP_NEW_CTL1		0x88

#define CAP_NEW_CTL2		0x89

#define CAP_MODE1		0xa4
#define CAP_MODE1_8BIT			0x01	/* enable 8bit capture mode	*/
#define CAP_MODE1_CCIR656		0x02	/* CCIR656 mode			*/
#define CAP_MODE1_IGNOREVGT		0x04	/* ignore VGT			*/
#define CAP_MODE1_ALTFIFO		0x10	/* use alternate FIFO for capture */
#define CAP_MODE1_SWAPUV		0x20	/* swap UV bytes		*/
#define CAP_MODE1_MIRRORY		0x40	/* mirror vertically		*/
#define CAP_MODE1_MIRRORX		0x80	/* mirror horizontally		*/

#define CAP_MODE2		0xa5

#define Y_TV_CTL		0xae

#define EXT_MEM_START		0xc0		/* ext start address 21 bits */
#define HOR_PHASE_SHIFT		0xc2		/* high 3 bits */
#define EXT_SRC_WIDTH		0xc3		/* ext offset phase  10 bits */
#define EXT_SRC_HEIGHT		0xc4		/* high 6 bits */
#define EXT_X_START		0xc5		/* ext->screen, 16 bits */
#define EXT_X_END		0xc7		/* ext->screen, 16 bits */
#define EXT_Y_START		0xc9		/* ext->screen, 16 bits */
#define EXT_Y_END		0xcb		/* ext->screen, 16 bits */
#define EXT_SRC_WIN_WIDTH	0xcd		/* 8 bits */
#define EXT_COLOUR_COMPARE	0xce		/* 24 bits */
#define EXT_DDA_X_INIT		0xd1		/* ext->screen 16 bits */
#define EXT_DDA_X_INC		0xd3		/* ext->screen 16 bits */
#define EXT_DDA_Y_INIT		0xd5		/* ext->screen 16 bits */
#define EXT_DDA_Y_INC		0xd7		/* ext->screen 16 bits */

#define VID_FIFO_CTL		0xd9

#define VID_CAP_VFC		0xdb
#define VID_CAP_VFC_YUV422		0x00	/* formats - does this cause conversion? */
#define VID_CAP_VFC_RGB555		0x01
#define VID_CAP_VFC_RGB565		0x02
#define VID_CAP_VFC_RGB888_24		0x03
#define VID_CAP_VFC_RGB888_32		0x04
#define VID_CAP_VFC_DUP_PIX_ZOON	0x08	/* duplicate pixel zoom			*/
#define VID_CAP_VFC_MOD_3RD_PIX		0x20	/* modify 3rd duplicated pixel		*/
#define VID_CAP_VFC_DBL_H_PIX		0x40	/* double horiz pixels			*/
#define VID_CAP_VFC_UV128		0x80	/* UV data offset by 128		*/

#define VID_DISP_CTL1		0xdc
#define VID_DISP_CTL1_INTRAM		0x01	/* video pixels go to internal RAM	*/
#define VID_DISP_CTL1_IGNORE_CCOMP	0x02	/* ignore colour compare registers	*/
#define VID_DISP_CTL1_NOCLIP		0x04	/* do not clip to 16235,16240		*/
#define VID_DISP_CTL1_UV_AVG		0x08	/* U/V data is averaged			*/
#define VID_DISP_CTL1_Y128		0x10	/* Y data offset by 128			*/
#define VID_DISP_CTL1_VINTERPOL_OFF	0x20	/* vertical interpolation off		*/
#define VID_DISP_CTL1_VID_OUT_WIN_FULL	0x40	/* video out window full		*/
#define VID_DISP_CTL1_ENABLE_VID_WINDOW	0x80	/* enable video window			*/

#define VID_FIFO_CTL1		0xdd

#define VFAC_CTL1		0xe8
#define VFAC_CTL1_CAPTURE		0x01	/* capture enable			*/
#define VFAC_CTL1_VFAC_ENABLE		0x02	/* vfac enable				*/
#define VFAC_CTL1_FREEZE_CAPTURE	0x04	/* freeze capture			*/
#define VFAC_CTL1_FREEZE_CAPTURE_SYNC	0x08	/* sync freeze capture			*/
#define VFAC_CTL1_VALIDFRAME_SRC	0x10	/* select valid frame source		*/
#define VFAC_CTL1_PHILIPS		0x40	/* select Philips mode			*/
#define VFAC_CTL1_MODVINTERPOLCLK	0x80	/* modify vertical interpolation clocl	*/

#define VFAC_CTL2		0xe9
#define VFAC_CTL2_INVERT_VIDDATAVALID	0x01	/* invert video data valid		*/
#define VFAC_CTL2_INVERT_GRAPHREADY	0x02	/* invert graphic ready output sig	*/
#define VFAC_CTL2_INVERT_DATACLK	0x04	/* invert data clock signal		*/
#define VFAC_CTL2_INVERT_HSYNC		0x08	/* invert hsync input			*/
#define VFAC_CTL2_INVERT_VSYNC		0x10	/* invert vsync input			*/
#define VFAC_CTL2_INVERT_FRAME		0x20	/* invert frame odd/even input		*/
#define VFAC_CTL2_INVERT_BLANK		0x40	/* invert blank output			*/
#define VFAC_CTL2_INVERT_OVSYNC		0x80	/* invert other vsync input		*/

#define VFAC_CTL3		0xea

#define CAP_MEM_START		0xeb		/* 18 bits */
#define CAP_MAP_WIDTH		0xed		/* high 6 bits */
#define CAP_PITCH		0xee		/* 8 bits */

#define CAP_CTL_MISC		0xef
#define CAP_CTL_MISC_HDIV		0x01
#define CAP_CTL_MISC_HDIV4		0x02
#define CAP_CTL_MISC_ODDEVEN		0x04
#define CAP_CTL_MISC_HSYNCDIV2		0x08
#define CAP_CTL_MISC_SYNCTZHIGH		0x10
#define CAP_CTL_MISC_SYNCTZOR		0x20
#define CAP_CTL_MISC_DISPUSED		0x80

#define REG_BANK		0xfa
#define REG_BANK_Y			0x01
#define REG_BANK_K			0x05


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

struct cyberpro_info {
	unsigned char	*regs;
	char		*fb;
	char		dev_name[32];
	unsigned int	fb_size;
};

/*
 * Note! Writing to the Cyber20x0 registers from an interrupt
 * routine is definitely a bad idea atm.
 */
int cyber2000fb_attach(struct cyberpro_info *info);
void cyber2000fb_detach(void);


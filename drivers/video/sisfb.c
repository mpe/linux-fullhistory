/*
 * SiS 300/630/540 frame buffer device For Kernal 2.4.x
 *
 * This driver is partly based on the VBE 2.0 compliant graphic 
 * boards framebuffer driver, which is 
 * 
 * (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#define EXPORT_SYMTAB
#undef  SISFBDEBUG
#undef CONFIG_FB_SIS_LINUXBIOS

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vt_kern.h>
#include <linux/capability.h>
#include <linux/sisfb.h>

#include <asm/io.h>
#include <asm/mtrr.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

/* ------------------- Constant Definitions ------------------------- */

#define FALSE   0
#define TRUE    1

/* Draw Function 
#define FBIOGET_GLYPH        0x4620
#define FBIOGET_HWCINFO      0x4621
*/
#define BR(x)   (0x8200 | (x) << 2)

#define BITBLT               0x00000000
#define COLOREXP             0x00000001
#define ENCOLOREXP           0x00000002
#define MULTIPLE_SCANLINE    0x00000003
#define LINE                 0x00000004
#define TRAPAZOID_FILL       0x00000005
#define TRANSPARENT_BITBLT   0x00000006

#define SRCVIDEO             0x00000000
#define SRCSYSTEM            0x00000010
#define SRCAGP               0x00000020

#define PATFG                0x00000000
#define PATPATREG            0x00000040
#define PATMONO              0x00000080

#define X_INC                0x00010000
#define X_DEC                0x00000000
#define Y_INC                0x00020000
#define Y_DEC                0x00000000

#define NOCLIP               0x00000000
#define NOMERGECLIP          0x04000000
#define CLIPENABLE           0x00040000
#define CLIPWITHOUTMERGE     0x04040000

#define OPAQUE               0x00000000
#define TRANSPARENT          0x00100000

#define DSTAGP               0x02000000
#define DSTVIDEO             0x02000000

#define LINE_STYLE           0x00800000
#define NO_RESET_COUNTER     0x00400000
#define NO_LAST_PIXEL        0x00200000

/* capabilities */
#define TURBO_QUEUE_CAP      0x80
#define HW_CURSOR_CAP        0x40

/* VGA register Offsets */
#define SEQ_ADR                   (0x14)
#define SEQ_DATA                  (0x15)
#define DAC_ADR                   (0x18)
#define DAC_DATA                  (0x19)
#define CRTC_ADR                  (0x24)
#define CRTC_DATA                 (0x25)

/* SiS indexed register indexes */
#define IND_SIS_PASSWORD          (0x05)
#define IND_SIS_DRAM_SIZE         (0x14)
#define IND_SIS_MODULE_ENABLE     (0x1E)
#define IND_SIS_PCI_ADDRESS_SET   (0x20)
#define IND_SIS_TURBOQUEUE_ADR    (0x26)
#define IND_SIS_TURBOQUEUE_SET    (0x27)

/* Sis register value */
#define SIS_PASSWORD              (0x86)

#define SIS_2D_ENABLE             (0x40)

#define SIS_MEM_MAP_IO_ENABLE     (0x01)
#define SIS_PCI_ADDR_ENABLE       (0x80)

#define MMIO_SIZE                 0x20000	/* 128K MMIO capability */
#define MAX_ROM_SCAN              0x10000

#define RESERVED_MEM_SIZE_4M      0x400000	/* 4M */
#define RESERVED_MEM_SIZE_8M      0x800000	/* 8M */

/* Mode set stuff */
#define DEFAULT_MODE      0

#define ModeInfoFlag      0x07
#define MemoryInfoFlag    0x1E0
#define MemorySizeShift   0x05
#define ModeVGA           0x03
#define ModeEGA           0x02
#define CRT1Len           17
#define DoubleScanMode    0x8000
#define HalfDCLK          0x1000

#define InterlaceMode     0x80
#define LineCompareOff    0x400
#define DACInfoFlag       0x18

#define VCLKStartFreq      25

#define SIS_Glamour       0x0300
#define SIS_Trojan        0x6300
#define SIS_Spartan       0x5300

/* heap stuff */
#define OH_ALLOC_SIZE         4000
#define SENTINEL              0x7fffffff

#define TURBO_QUEUE_AREA_SIZE 0x80000	/* 512K */
#define HW_CURSOR_AREA_SIZE   0x1000	/* 4K */

/* video connection status */
#define VB_COMPOSITE 0x01
#define VB_SVIDEO    0x02
#define VB_SCART     0x04
#define VB_LCD       0x08
#define VB_CRT2      0x10
#define CRT1         0x20
#define VB_HDTV      0x40

/* ------------------- Global Variables ----------------------------- */

struct video_info ivideo;

struct GlyInfo {
	unsigned char ch;
	int fontwidth;
	int fontheight;
	u8 gmask[72];
	int ngmask;
};

/* Supported SiS Chips list */
static struct board {
	u16 vendor, device;
	const char *name;
} dev_list[] = {
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_300,     "SIS 300"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_540_VGA, "SIS 540"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_630_VGA, "SIS 630"},
	{0, 0, NULL}
};

/* card parameters */
unsigned long rom_base;
unsigned long rom_vbase;

/* mode */
static int video_type = FB_TYPE_PACKED_PIXELS;
static int video_linelength;
static int video_cmap_len;
static int sisfb_off = 0;

static struct fb_var_screeninfo default_var = {
	0, 0, 0, 0,
	0, 0,
	0,
	0,
	{0, 8, 0},
	{0, 8, 0},
	{0, 8, 0},
	{0, 0, 0},
	0,
	FB_ACTIVATE_NOW, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0,
	0,
	FB_VMODE_NONINTERLACED,
	{0, 0, 0, 0, 0, 0}
};

static struct display disp;
static struct fb_info fb_info;

static struct {
	u16 blue, green, red, pad;
} palette[256];

static union {
#ifdef FBCON_HAS_CFB16
	u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB24
	u32 cfb24[16];
#endif
#ifdef FBCON_HAS_CFB32
	u32 cfb32[16];
#endif
} fbcon_cmap;

static int inverse = 0;
static int currcon = 0;

static struct display_switch sisfb_sw;

u8 caps = 0;

/* ModeSet stuff */

u16 P3c4, P3d4, P3c0, P3ce, P3c2, P3ca, P3c6, P3c7, P3c8, P3c9, P3da;
u16 CRT1VCLKLen;
u16 flag_clearbuffer;
u16 CRT1VCLKLen;
int ModeIDOffset, StandTable, CRT1Table, ScreenOffset;
int REFIndex, ModeType;
int VCLKData;
int RAMType;

int mode_idx = -1;
u8 mode_no = 0;
u8 rate_idx = 0;

static const struct _sisbios_mode {
	char name[15];
	u8 mode_no;
	u16 xres;
	u16 yres;
	u16 bpp;
	u16 rate_idx;
	u16 cols;
	u16 rows;
} sisbios_mode[] = {
	{"640x480x8",    0x2E,  640,  480,  8, 1,  80, 30},
	{"640x480x16",   0x44,  640,  480, 16, 1,  80, 30},
	{"640x480x32",   0x62,  640,  480, 32, 1,  80, 30},
	{"800x600x8",    0x30,  800,  600,  8, 2, 100, 37},
	{"800x600x16",   0x47,  800,  600, 16, 2, 100, 37},
	{"800x600x32",   0x63,  800,  600, 32, 2, 100, 37}, 
	{"1024x768x8",   0x38, 1024,  768,  8, 2, 128, 48},
	{"1024x768x16",  0x4A, 1024,  768, 16, 2, 128, 48},
	{"1024x768x32",  0x64, 1024,  768, 32, 2, 128, 48},
	{"1280x1024x8",  0x3A, 1280, 1024,  8, 2, 160, 64},
	{"1280x1024x16", 0x4D, 1280, 1024, 16, 2, 160, 64},
	{"1280x1024x32", 0x65, 1280, 1024, 32, 2, 160, 64},
	{"1600x1200x8",  0x3C, 1600, 1200,  8, 1, 200, 75},
	{"1600x1200x16", 0x3D, 1600, 1200, 16, 1, 200, 75},
	{"1600x1200x32", 0x66, 1600, 1200, 32, 1, 200, 75},
	{"1920x1440x8",  0x68, 1920, 1440,  8, 1, 240, 75},
	{"1920x1440x16", 0x69, 1920, 1440, 16, 1, 240, 75},
	{"1920x1440x32", 0x6B, 1920, 1440, 32, 1, 240, 75},
	{"\0", 0x00, 0, 0, 0, 0, 0, 0}
};

static struct _vrate {
	u16 idx;
	u16 xres;
	u16 yres;
	u16 refresh;
} vrate[] = {
	{1,  640,  480,  60}, {2,  640, 480,  72}, {3,  640, 480,  75}, {4,  640, 480,  85},
	{5,  640,  480, 100}, {6,  640, 480, 120}, {7,  640, 480, 160}, {8,  640, 480, 200},
	{1,  800,  600,  56}, {2,  800, 600,  60}, {3,  800, 600,  72}, {4,  800, 600,  75},
	{5,  800,  600,  85}, {6,  800, 600, 100}, {7,  800, 600, 120}, {8,  800, 600, 160},
	{1, 1024,  768,  43}, {2, 1024, 768,  60}, {3, 1024, 768,  70}, {4, 1024, 768,  75},
	{5, 1024,  768,  85}, {6, 1024, 768, 100}, {7, 1024, 768, 120},
	{1, 1280, 1024,  43}, {2, 1280, 1024, 60}, {3, 1280, 1024, 75}, {4, 1280, 1024, 85},
	{1, 1600, 1200,  60}, {2, 1600, 1200, 65}, {3, 1600, 1200, 70}, {4, 1600, 1200, 75},
	{5, 1600, 1200,  85},
	{1, 1920, 1440,  60},
	{0, 0, 0, 0}
};

u16 DRAMType[17][5] = {
	{0x0C, 0x0A, 0x02, 0x40, 0x39}, {0x0D, 0x0A, 0x01, 0x40, 0x48},
	{0x0C, 0x09, 0x02, 0x20, 0x35}, {0x0D, 0x09, 0x01, 0x20, 0x44},
	{0x0C, 0x08, 0x02, 0x10, 0x31}, {0x0D, 0x08, 0x01, 0x10, 0x40},
	{0x0C, 0x0A, 0x01, 0x20, 0x34}, {0x0C, 0x09, 0x01, 0x08, 0x32},
	{0x0B, 0x08, 0x02, 0x08, 0x21}, {0x0C, 0x08, 0x01, 0x08, 0x30},
	{0x0A, 0x08, 0x02, 0x04, 0x11}, {0x0B, 0x0A, 0x01, 0x10, 0x28},
	{0x09, 0x08, 0x02, 0x02, 0x01}, {0x0B, 0x09, 0x01, 0x08, 0x24},
	{0x0B, 0x08, 0x01, 0x04, 0x20}, {0x0A, 0x08, 0x01, 0x02, 0x10},
	{0x09, 0x08, 0x01, 0x01, 0x00}
};

u16 MDA_DAC[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F
};

u16 CGA_DAC[] = {
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F,
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F
};

u16 EGA_DAC[] = {
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x05, 0x15,
	0x20, 0x30, 0x24, 0x34, 0x21, 0x31, 0x25, 0x35,
	0x08, 0x18, 0x0C, 0x1C, 0x09, 0x19, 0x0D, 0x1D,
	0x28, 0x38, 0x2C, 0x3C, 0x29, 0x39, 0x2D, 0x3D,
	0x02, 0x12, 0x06, 0x16, 0x03, 0x13, 0x07, 0x17,
	0x22, 0x32, 0x26, 0x36, 0x23, 0x33, 0x27, 0x37,
	0x0A, 0x1A, 0x0E, 0x1E, 0x0B, 0x1B, 0x0F, 0x1F,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F
};

u16 VGA_DAC[] = {
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F,
	0x00, 0x05, 0x08, 0x0B, 0x0E, 0x11, 0x14, 0x18,
	0x1C, 0x20, 0x24, 0x28, 0x2D, 0x32, 0x38, 0x3F,
	0x00, 0x10, 0x1F, 0x2F, 0x3F, 0x1F, 0x27, 0x2F,
	0x37, 0x3F, 0x2D, 0x31, 0x36, 0x3A, 0x3F, 0x00,
	0x07, 0x0E, 0x15, 0x1C, 0x0E, 0x11, 0x15, 0x18,
	0x1C, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x00, 0x04,
	0x08, 0x0C, 0x10, 0x08, 0x0A, 0x0C, 0x0E, 0x10,
	0x0B, 0x0C, 0x0D, 0x0F, 0x10
};

#ifdef CONFIG_FB_SIS_LINUXBIOS

#define Monitor1Sense 0x20

unsigned char SRegsInit[] = { 
 	0x03, 0x00, 0x03, 0x00, 0x02, 0xa1, 0x00, 0x13,
	0x2f, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
 	0x00, 0x0f, 0x00, 0x00, 0x4f, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 
 	0xa1, 0x76, 0xb2, 0xf6, 0x0d, 0x00, 0x00, 0x00,
	0x37, 0x61, 0x80, 0x1b, 0xe1, 0x01, 0x55, 0x43, 
 	0x80, 0x00, 0x01, 0xff, 0x00, 0x00, 0x00, 0xff,
	0x8e, 0x40, 0x00, 0x00, 0x08, 0x00, 0xff, 0xff
};

unsigned char SRegs[] = { 
 	0x03, 0x01, 0x0F, 0x00, 0x0E, 0xA1, 0x02, 0x13,
	0x3F, 0x86, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
 	0x0B, 0x0F, 0x00, 0x00, 0x4F, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x40, 0x00,
 	0xA1, 0xB6, 0xB2, 0xF6, 0x0D, 0x00, 0xF8, 0xF0,
	0x37, 0x61, 0x80, 0x1B, 0xE1, 0x80, 0x55, 0x43,
 	0x80, 0x00, 0x11, 0xFF, 0x00, 0x00, 0x00, 0xFF,
	0x8E, 0x40, 0x00, 0x00, 0x08, 0x00, 0xFF, 0xFF
};

unsigned char CRegs[] = { 
	0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0x0b, 0x3e,
	0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  
	0xe9, 0x0b, 0xdf, 0x50, 0x40, 0xe7, 0x04, 0xa3,
	0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff
};	// clear CR11[7]

unsigned char GRegs[] = { 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f, 0xff, 0x00
};

unsigned char ARegs[] = { 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

unsigned char MReg = 0x6f;

#endif


/* HEAP stuff */

struct OH {
	struct OH *pohNext;
	struct OH *pohPrev;
	unsigned long ulOffset;
	unsigned long ulSize;
};

struct OHALLOC {
	struct OHALLOC *pohaNext;
	struct OH aoh[1];
};

struct HEAP {
	struct OH ohFree;
	struct OH ohUsed;
	struct OH *pohFreeList;
	struct OHALLOC *pohaChain;

	unsigned long ulMaxFreeSize;
};

struct HEAP heap;
unsigned long heap_start;
unsigned long heap_end;
unsigned long heap_size;

unsigned int tqueue_pos;
unsigned long hwcursor_vbase;

/* Draw Function stuff */
u32 command_reg;

/* -------------------- Macro definitions --------------------------- */

#ifdef SISFBDEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define vgawb(reg,data) \
           (outb(data, ivideo.vga_base+reg))
#define vgaww(reg,data) \
           (outw(data, ivideo.vga_base+reg))
#define vgawl(reg,data) \
           (outl(data, ivideo.vga_base+reg))
#define vgarb(reg)      \
           (inb(ivideo.vga_base+reg))

/* ---------------------- Routine Prototype ------------------------- */

/* Interface used by the world */
int sisfb_setup(char *options);
static int sisfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info);
static int sisfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int sisfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int sisfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int sisfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int sisfb_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg, int con,
		       struct fb_info *info);

/* Interface to the low level console driver */
int sisfb_init(void);
static int sisfb_update_var(int con, struct fb_info *info);
static int sisfb_switch(int con, struct fb_info *info);
static void sisfb_blank(int blank, struct fb_info *info);

/* Internal routines */
static void crtc_to_var(struct fb_var_screeninfo *var);
static void sisfb_set_disp(int con, struct fb_var_screeninfo *var);
static int sis_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			 unsigned *blue, unsigned *transp,
			 struct fb_info *fb_info);
static int sis_setcolreg(unsigned regno, unsigned red, unsigned green,
			 unsigned blue, unsigned transp,
			 struct fb_info *fb_info);
static void do_install_cmap(int con, struct fb_info *info);
static int do_set_var(struct fb_var_screeninfo *var, int isactive,
		      struct fb_info *info);

/* set-mode routines */
static void set_reg1(u16 port, u16 index, u16 data);
static void set_reg3(u16 port, u16 data);
static void set_reg4(u16 port, unsigned long data);
static u8 get_reg1(u16 port, u16 index);
static u8 get_reg2(u16 port);
//#ifndef CONFIG_FB_SIS_LINUXBIOS
static u32 get_reg3(u16 port);
static u16 get_modeID_length(unsigned long ROMAddr, u16 ModeNo);
static int search_modeID(unsigned long ROMAddr, u16 ModeNo);
static int check_memory_size(unsigned long ROMAddr);
static void get_mode_ptr(unsigned long ROMAddr, u16 ModeNo);
static void set_seq_regs(unsigned long ROMAddr);
static void set_misc_regs(unsigned long ROMAddr);
static void set_crtc_regs(unsigned long ROMAddr);
static void set_attregs(unsigned long ROMAddr);
static void set_grc_regs(unsigned long ROMAddr);
static void ClearExt1Regs(void);
static u16 GetRefindexLength(unsigned long ROMAddr, u16 ModeNo);
static int get_rate_ptr(unsigned long ROMAddr, u16 ModeNo);
static void set_sync(unsigned long ROMAddr);
static void set_crt1_crtc(unsigned long ROMAddr);
static void set_crt1_offset(unsigned long ROMAddr);
static u16 get_vclk_len(unsigned long ROMAddr);
static void set_crt1_vclk(unsigned long ROMAddr);
static void set_vclk_state(unsigned long ROMAddr, u16 ModeNo);
static u16 calc_delay2(unsigned long ROMAddr, u16 key);
static u16 calc_delay(unsigned long ROMAddr, u16 key);
static void set_crt1_FIFO(unsigned long ROMAddr);
static void set_crt1_FIFO2(unsigned long ROMAddr);
static void set_crt1_mode_regs(unsigned long ROMAddr, u16 ModeNo);
static void set_interlace(unsigned long ROMAddr, u16 ModeNo);
//#endif
static void write_DAC(u16 dl, u16 ah, u16 al, u16 dh);
static void load_DAC(unsigned long ROMAddr);
static void display_on(void);

static int SiSSetMode(u16 ModeNo);
static void pre_setmode(void);
static void post_setmode(void);
static void search_mode(const char *name);
static u8 search_refresh_rate(unsigned int rate);

/* heap routines */
static int sisfb_heap_init(void);
static struct OH *poh_new_node(void);
static struct OH *poh_allocate(unsigned long size);
static struct OH *poh_free(unsigned long base);
static void delete_node(struct OH *poh);
static void insert_node(struct OH *pohList, struct OH *poh);
static void free_node(struct OH *poh);

/* ---------------------- Internal Routines ------------------------- */

inline static u32 RD32(unsigned char *base, s32 off)
{
	return readl(base + off);
}

inline static void WR32(unsigned char *base, s32 off, u32 v)
{
	writel(v, base + off);
}

inline static void WR16(unsigned char *base, s32 off, u16 v)
{
	writew(v, base + off);
}

inline static void WR8(unsigned char *base, s32 off, u8 v)
{
	writeb(v, base + off);
}

inline static u32 regrl(s32 off)
{
	return RD32(ivideo.mmio_vbase, off);
}

inline static void regwl(s32 off, u32 v)
{
	WR32(ivideo.mmio_vbase, off, v);
}

inline static void regww(s32 off, u16 v)
{
	WR16(ivideo.mmio_vbase, off, v);
}

inline static void regwb(s32 off, u8 v)
{
	WR8(ivideo.mmio_vbase, off, v);
}

/* 
 *    Get CRTC registers to set var 
 */
static void crtc_to_var(struct fb_var_screeninfo *var)
{
	u16 VRE, VBE, VRS, VBS, VDE, VT;
	u16 HRE, HBE, HRS, HBS, HDE, HT;
	u8 uSRdata, uCRdata, uCRdata2, uCRdata3, uMRdata;
	int A, B, C, D, E, F, temp;
	double hrate, drate;

	vgawb(SEQ_ADR, 0x6);
	uSRdata = vgarb(SEQ_DATA);

	if (uSRdata & 0x20)
		var->vmode = FB_VMODE_INTERLACED;
	else
		var->vmode = FB_VMODE_NONINTERLACED;

	switch ((uSRdata & 0x1c) >> 2) {
	case 0:
		var->bits_per_pixel = 8;
		break;
	case 2:
		var->bits_per_pixel = 16;
		break;
	case 4:
		var->bits_per_pixel = 32;
		break;
	}

	switch (var->bits_per_pixel) {
	case 8:
		var->red.length = 6;
		var->green.length = 6;
		var->blue.length = 6;
		video_cmap_len = 256;
		break;
	case 16:		/* RGB 565 */
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		video_cmap_len = 16;

		break;
	case 24:		/* RGB 888 */
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		video_cmap_len = 16;
		break;
	case 32:
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		video_cmap_len = 16;
		break;
	}

	vgawb(SEQ_ADR, 0xa);
	uSRdata = vgarb(SEQ_DATA);

	vgawb(CRTC_ADR, 0x6);
	uCRdata = vgarb(CRTC_DATA);
	vgawb(CRTC_ADR, 0x7);
	uCRdata2 = vgarb(CRTC_DATA);
	VT =
	    (uCRdata & 0xff) | ((u16) (uCRdata2 & 0x01) << 8) |
	    ((u16) (uCRdata2 & 0x20) << 4) | ((u16) (uSRdata & 0x01) <<
					      10);
	A = VT + 2;

	vgawb(CRTC_ADR, 0x12);
	uCRdata = vgarb(CRTC_DATA);
	VDE =
	    (uCRdata & 0xff) | ((u16) (uCRdata2 & 0x02) << 7) |
	    ((u16) (uCRdata2 & 0x40) << 3) | ((u16) (uSRdata & 0x02) << 9);
	E = VDE + 1;

	vgawb(CRTC_ADR, 0x10);
	uCRdata = vgarb(CRTC_DATA);
	VRS =
	    (uCRdata & 0xff) | ((u16) (uCRdata2 & 0x04) << 6) |
	    ((u16) (uCRdata2 & 0x80) << 2) | ((u16) (uSRdata & 0x08) << 7);
	F = VRS + 1 - E;

	vgawb(CRTC_ADR, 0x15);
	uCRdata = vgarb(CRTC_DATA);
	vgawb(CRTC_ADR, 0x9);
	uCRdata3 = vgarb(CRTC_DATA);
	VBS =
	    (uCRdata & 0xff) | ((u16) (uCRdata2 & 0x08) << 5) |
	    ((u16) (uCRdata3 & 0x20) << 4) | ((u16) (uSRdata & 0x04) << 8);

	vgawb(CRTC_ADR, 0x16);
	uCRdata = vgarb(CRTC_DATA);
	VBE = (uCRdata & 0xff) | ((u16) (uSRdata & 0x10) << 4);
	temp = VBE - ((E - 1) & 511);
	B = (temp > 0) ? temp : (temp + 512);

	vgawb(CRTC_ADR, 0x11);
	uCRdata = vgarb(CRTC_DATA);
	VRE = (uCRdata & 0x0f) | ((uSRdata & 0x20) >> 1);
	temp = VRE - ((E + F - 1) & 31);
	C = (temp > 0) ? temp : (temp + 32);

	D = B - F - C;

	var->yres = var->yres_virtual = E;
	var->upper_margin = D;
	var->lower_margin = F;
	var->vsync_len = C;

	vgawb(SEQ_ADR, 0xb);
	uSRdata = vgarb(SEQ_DATA);

	vgawb(CRTC_ADR, 0x0);
	uCRdata = vgarb(CRTC_DATA);
	HT = (uCRdata & 0xff) | ((u16) (uSRdata & 0x03) << 8);
	A = HT + 5;

	vgawb(CRTC_ADR, 0x1);
	uCRdata = vgarb(CRTC_DATA);
	HDE = (uCRdata & 0xff) | ((u16) (uSRdata & 0x0C) << 6);
	E = HDE + 1;

	vgawb(CRTC_ADR, 0x4);
	uCRdata = vgarb(CRTC_DATA);
	HRS = (uCRdata & 0xff) | ((u16) (uSRdata & 0xC0) << 2);
	F = HRS - E - 3;

	vgawb(CRTC_ADR, 0x2);
	uCRdata = vgarb(CRTC_DATA);
	HBS = (uCRdata & 0xff) | ((u16) (uSRdata & 0x30) << 4);

	vgawb(SEQ_ADR, 0xc);
	uSRdata = vgarb(SEQ_DATA);
	vgawb(CRTC_ADR, 0x3);
	uCRdata = vgarb(CRTC_DATA);
	vgawb(CRTC_ADR, 0x5);
	uCRdata2 = vgarb(CRTC_DATA);
	HBE =
	    (uCRdata & 0x1f) | ((u16) (uCRdata2 & 0x80) >> 2) |
	    ((u16) (uSRdata & 0x03) << 6);
	HRE = (uCRdata2 & 0x1f) | ((uSRdata & 0x04) << 3);

	temp = HBE - ((E - 1) & 255);
	B = (temp > 0) ? temp : (temp + 256);

	temp = HRE - ((E + F + 3) & 63);
	C = (temp > 0) ? temp : (temp + 64);

	D = B - F - C;

	var->xres = var->xres_virtual = E * 8;
	var->left_margin = D * 8;
	var->right_margin = F * 8;
	var->hsync_len = C * 8;

	var->activate = FB_ACTIVATE_NOW;

	var->sync = 0;

	uMRdata = vgarb(0x1C);
	if (uMRdata & 0x80)
		var->sync &= ~FB_SYNC_VERT_HIGH_ACT;
	else
		var->sync |= FB_SYNC_VERT_HIGH_ACT;

	if (uMRdata & 0x40)
		var->sync &= ~FB_SYNC_HOR_HIGH_ACT;
	else
		var->sync |= FB_SYNC_HOR_HIGH_ACT;

	VT += 2;
	VT <<= 1;
	HT = (HT + 5) * 8;

	hrate = (double) ivideo.refresh_rate * (double) VT / 2;
	drate = hrate * HT;
	var->pixclock = (u32) (1E12 / drate);
}

static void sisfb_set_disp(int con, struct fb_var_screeninfo *var)
{
	struct fb_fix_screeninfo fix;
	struct display *display;
	struct display_switch *sw;
	u32 flags;

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	sisfb_get_fix(&fix, con, 0);

	display->screen_base = ivideo.video_vbase;
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->line_length = fix.line_length;
	display->next_line = fix.line_length;
	/*display->can_soft_blank = 1; */
	display->can_soft_blank = 0;
	display->inverse = inverse;
	display->var = *var;

	save_flags(flags);
	switch (ivideo.video_bpp) {
#ifdef FBCON_HAS_CFB8
	case 8:
		sw = &fbcon_cfb8;
		break;
#endif

#ifdef FBCON_HAS_CFB16
	case 15:
	case 16:
		sw = &fbcon_cfb16;
		display->dispsw_data = fbcon_cmap.cfb16;
		break;
#endif

#ifdef FBCON_HAS_CFB24
	case 24:
		sw = &fbcon_cfb24;
		display->dispsw_data = fbcon_cmap.cfb24;
		break;
#endif

#ifdef FBCON_HAS_CFB32
	case 32:
		sw = &fbcon_cfb32;
		display->dispsw_data = fbcon_cmap.cfb32;
		break;
#endif

	default:
		sw = &fbcon_dummy;
		return;
	}
	memcpy(&sisfb_sw, sw, sizeof(*sw));
	display->dispsw = &sisfb_sw;
	restore_flags(flags);

	display->scrollmode = SCROLL_YREDRAW;
	sisfb_sw.bmove = fbcon_redraw_bmove;
}

/*
 *    Read a single color register and split it into colors/transparent. 
 *    Return != 0 for invalid regno.
 */

static int sis_getcolreg(unsigned regno, unsigned *red, unsigned *green, unsigned *blue,
			 unsigned *transp, struct fb_info *fb_info)
{
	if (regno >= video_cmap_len)
		return 1;

	*red = palette[regno].red;
	*green = palette[regno].green;
	*blue = palette[regno].blue;
	*transp = 0;
	return 0;
}

/*
 *    Set a single color register. The values supplied are already
 *    rounded down to the hardware's capabilities (according to the
 *    entries in the var structure). Return != 0 for invalid regno.
 */

static int sis_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
			 unsigned transp, struct fb_info *fb_info)
{

	if (regno >= video_cmap_len)
		return 1;

	palette[regno].red = red;
	palette[regno].green = green;
	palette[regno].blue = blue;

	switch (ivideo.video_bpp) {
#ifdef FBCON_HAS_CFB8
	case 8:
		vgawb(DAC_ADR, regno);
		vgawb(DAC_DATA, red >> 10);
		vgawb(DAC_DATA, green >> 10);
		vgawb(DAC_DATA, blue >> 10);
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 15:
	case 16:
		fbcon_cmap.cfb16[regno] =
		    ((red & 0xf800)) |
		    ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:
		red >>= 8;
		green >>= 8;
		blue >>= 8;
		fbcon_cmap.cfb24[regno] =
		    (red << 16) | (green << 8) | (blue);
		break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
		red >>= 8;
		green >>= 8;
		blue >>= 8;
		fbcon_cmap.cfb32[regno] =
		    (red << 16) | (green << 8) | (blue);
		break;
#endif
	}
	return 0;
}

static void do_install_cmap(int con, struct fb_info *info)
{
	if (con != currcon)
		return;

	if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, 1, sis_setcolreg, info);
	else
		fb_set_cmap(fb_default_cmap(video_cmap_len), 1,
			    sis_setcolreg, info);
}

static int do_set_var(struct fb_var_screeninfo *var, int isactive,
		      struct fb_info *info)
{
	unsigned int htotal =
	    var->left_margin + var->xres + var->right_margin +
	    var->hsync_len;
	unsigned int vtotal =
	    var->upper_margin + var->yres + var->lower_margin +
	    var->vsync_len;
	double drate = 0, hrate = 0;
	int found_mode = 0;

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_NONINTERLACED)
		vtotal <<= 1;
	else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE)
		vtotal <<= 2;
	else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED)
		var->yres <<= 1;


	if (!htotal || !vtotal) {
		DPRINTK("Invalid 'var' Information!\n");
		return 1;
	}

	drate = 1E12 / var->pixclock;
	hrate = drate / htotal;
	ivideo.refresh_rate = (unsigned int) (hrate / vtotal * 2 + 0.5);

	mode_idx = 0;
	while ((sisbios_mode[mode_idx].mode_no != 0)
	       && (sisbios_mode[mode_idx].xres <= var->xres)) {
		if ((sisbios_mode[mode_idx].xres == var->xres)
		    && (sisbios_mode[mode_idx].yres == var->yres)
		    && (sisbios_mode[mode_idx].bpp == var->bits_per_pixel)) {
			mode_no = sisbios_mode[mode_idx].mode_no;
			found_mode = 1;
			break;
		}
		mode_idx++;
	}

	if (!found_mode) {
		printk("sisfb does not support mode %dx%d-%d\n", var->xres,
		       var->yres, var->bits_per_pixel);
		return 1;
	}

	if (search_refresh_rate(ivideo.refresh_rate) == 0) {
		/* not supported rate */
		rate_idx = sisbios_mode[mode_idx].rate_idx;
		ivideo.refresh_rate = 60;
	}

	if (((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) && isactive) {
		pre_setmode();

		if (SiSSetMode(mode_no)) {
			DPRINTK("sisfb: set mode[0x%x]: failed\n",
				mode_no);
			return 1;
		}

		post_setmode();

		ivideo.video_bpp = sisbios_mode[mode_idx].bpp;
		ivideo.video_width = sisbios_mode[mode_idx].xres;
		ivideo.video_height = sisbios_mode[mode_idx].yres;
		video_linelength =
		    ivideo.video_width * (ivideo.video_bpp >> 3);

		DPRINTK("Current Mode: %dx%d-%d line_length=%d\n",
			ivideo.video_width, ivideo.video_height,
			ivideo.video_bpp, video_linelength);
	}

	return 0;
}

/* ---------------------- Draw Funtions ----------------------------- */

static void sis_get_glyph(struct GlyInfo *gly)
{
	struct display *p = &fb_display[currcon];
	u16 c;
	u8 *cdat;
	int widthb;
	u8 *gbuf = gly->gmask;
	int size;


	gly->fontheight = fontheight(p);
	gly->fontwidth = fontwidth(p);
	widthb = (fontwidth(p) + 7) / 8;

	c = gly->ch & p->charmask;
	if (fontwidth(p) <= 8)
		cdat = p->fontdata + c * fontheight(p);
	else
		cdat = p->fontdata + (c * fontheight(p) << 1);

	size = fontheight(p) * widthb;
	memcpy(gbuf, cdat, size);
	gly->ngmask = size;
}


/* ---------------------- HEAP Routines ----------------------------- */

/* 
 *  Heap Initialization
 */

static int sisfb_heap_init(void)
{
	struct OH *poh;
	u8 jTemp, tq_state;

	if (ivideo.video_size > 0x800000)
		/* video ram is large than 8M */
		heap_start = (unsigned long) ivideo.video_vbase + RESERVED_MEM_SIZE_8M;
	else
		heap_start = (unsigned long) ivideo.video_vbase + RESERVED_MEM_SIZE_4M;

	heap_end = (unsigned long) ivideo.video_vbase + ivideo.video_size;
	heap_size = heap_end - heap_start;


	/* Setting for Turbo Queue */
	if (heap_size >= TURBO_QUEUE_AREA_SIZE) {
		tqueue_pos =
		    (ivideo.video_size -
		     TURBO_QUEUE_AREA_SIZE) / (64 * 1024);
		jTemp = (u8) (tqueue_pos & 0xff);
		vgawb(SEQ_ADR, IND_SIS_TURBOQUEUE_SET);
		tq_state = vgarb(SEQ_DATA);
		tq_state |= 0xf0;
		tq_state &= 0xfc;
		tq_state |= (u8) (tqueue_pos >> 8);
		vgawb(SEQ_DATA, tq_state);
		vgawb(SEQ_ADR, IND_SIS_TURBOQUEUE_ADR);
		vgawb(SEQ_DATA, jTemp);

		caps |= TURBO_QUEUE_CAP;

		heap_end -= TURBO_QUEUE_AREA_SIZE;
		heap_size -= TURBO_QUEUE_AREA_SIZE;
	}

	/* Setting for HW cursor(4K) */
	if (heap_size >= HW_CURSOR_AREA_SIZE) {
		heap_end -= HW_CURSOR_AREA_SIZE;
		heap_size -= HW_CURSOR_AREA_SIZE;
		hwcursor_vbase = heap_end;

		caps |= HW_CURSOR_CAP;
	}

	heap.pohaChain = NULL;
	heap.pohFreeList = NULL;

	poh = poh_new_node();

	if (poh == NULL)
		return 1;

	/* The first node describles the entire heap size */
	poh->pohNext = &heap.ohFree;
	poh->pohPrev = &heap.ohFree;
	poh->ulSize = heap_end - heap_start + 1;
	poh->ulOffset = heap_start - (unsigned long) ivideo.video_vbase;

	DPRINTK("sisfb:Heap start:0x%p, end:0x%p, len=%dk\n",
		(char *) heap_start, (char *) heap_end,
		(unsigned int) poh->ulSize / 1024);

	DPRINTK("sisfb:First Node offset:0x%x, size:%dk\n",
		(unsigned int) poh->ulOffset, (unsigned int) poh->ulSize / 1024);

	/* The second node in our free list sentinel */
	heap.ohFree.pohNext = poh;
	heap.ohFree.pohPrev = poh;
	heap.ohFree.ulSize = 0;
	heap.ulMaxFreeSize = poh->ulSize;

	/* Initialize the discardable list */
	heap.ohUsed.pohNext = &heap.ohUsed;
	heap.ohUsed.pohPrev = &heap.ohUsed;
	heap.ohUsed.ulSize = SENTINEL;

	return 0;
}

/*
 *  Allocates a basic memory unit in which we'll pack our data structures.
 */

static struct OH *poh_new_node(void)
{
	int i;
	unsigned long cOhs;
	struct OHALLOC *poha;
	struct OH *poh;

	if (heap.pohFreeList == NULL) {
		poha = kmalloc(OH_ALLOC_SIZE, GFP_KERNEL);

		poha->pohaNext = heap.pohaChain;
		heap.pohaChain = poha;

		cOhs =
		    (OH_ALLOC_SIZE -
		     sizeof(struct OHALLOC)) / sizeof(struct OH) + 1;

		poh = &poha->aoh[0];
		for (i = cOhs - 1; i != 0; i--) {
			poh->pohNext = poh + 1;
			poh = poh + 1;
		}

		poh->pohNext = NULL;
		heap.pohFreeList = &poha->aoh[0];
	}

	poh = heap.pohFreeList;
	heap.pohFreeList = poh->pohNext;

	return (poh);
}

/* 
 *  Allocates space, return NULL when failed
 */

static struct OH *poh_allocate(unsigned long size)
{
	struct OH *pohThis;
	struct OH *pohRoot;
	int bAllocated = 0;

	if (size > heap.ulMaxFreeSize) {
		DPRINTK("sisfb: Can't allocate %dk size on offscreen\n",
			(unsigned int) size / 1024);
		return (NULL);
	}

	pohThis = heap.ohFree.pohNext;

	while (pohThis != &heap.ohFree) {
		if (size <= pohThis->ulSize) {
			bAllocated = 1;
			break;
		}
		pohThis = pohThis->pohNext;
	}

	if (!bAllocated) {
		DPRINTK("sisfb: Can't allocate %dk size on offscreen\n",
			(unsigned int) size / 1024);
		return (NULL);
	}

	if (size == pohThis->ulSize) {
		pohRoot = pohThis;
		delete_node(pohThis);
	} else {
		pohRoot = poh_new_node();

		if (pohRoot == NULL) {
			return (NULL);
		}

		pohRoot->ulOffset = pohThis->ulOffset;
		pohRoot->ulSize = size;

		pohThis->ulOffset += size;
		pohThis->ulSize -= size;
	}

	heap.ulMaxFreeSize -= size;

	pohThis = &heap.ohUsed;
	insert_node(pohThis, pohRoot);

	return (pohRoot);
}

/* 
 *  To remove a node from a list.
 */

static void delete_node(struct OH *poh)
{
	struct OH *pohPrev;
	struct OH *pohNext;


	pohPrev = poh->pohPrev;
	pohNext = poh->pohNext;

	pohPrev->pohNext = pohNext;
	pohNext->pohPrev = pohPrev;

	return;
}

/* 
 *  To insert a node into a list.
 */

static void insert_node(struct OH *pohList, struct OH *poh)
{
	struct OH *pohTemp;

	pohTemp = pohList->pohNext;

	pohList->pohNext = poh;
	pohTemp->pohPrev = poh;

	poh->pohPrev = pohList;
	poh->pohNext = pohTemp;
}

/*
 *  Frees an off-screen heap allocation.
 */

static struct OH *poh_free(unsigned long base)
{

	struct OH *pohThis;
	struct OH *pohFreed;
	struct OH *pohPrev;
	struct OH *pohNext;
	unsigned long ulUpper;
	unsigned long ulLower;
	int foundNode = 0;

	pohFreed = heap.ohUsed.pohNext;

	while (pohFreed != &heap.ohUsed) {
		if (pohFreed->ulOffset == base) {
			foundNode = 1;
			break;
		}

		pohFreed = pohFreed->pohNext;
	}

	if (!foundNode)
		return (NULL);

	heap.ulMaxFreeSize += pohFreed->ulSize;

	pohPrev = pohNext = NULL;
	ulUpper = pohFreed->ulOffset + pohFreed->ulSize;
	ulLower = pohFreed->ulOffset;

	pohThis = heap.ohFree.pohNext;

	while (pohThis != &heap.ohFree) {
		if (pohThis->ulOffset == ulUpper) {
			pohNext = pohThis;
		}
			else if ((pohThis->ulOffset + pohThis->ulSize) ==
				 ulLower) {
			pohPrev = pohThis;
		}
		pohThis = pohThis->pohNext;
	}

	delete_node(pohFreed);

	if (pohPrev && pohNext) {
		pohPrev->ulSize += (pohFreed->ulSize + pohNext->ulSize);
		delete_node(pohNext);
		free_node(pohFreed);
		free_node(pohNext);
		return (pohPrev);
	}

	if (pohPrev) {
		pohPrev->ulSize += pohFreed->ulSize;
		free_node(pohFreed);
		return (pohPrev);
	}

	if (pohNext) {
		pohNext->ulSize += pohFreed->ulSize;
		pohNext->ulOffset = pohFreed->ulOffset;
		free_node(pohFreed);
		return (pohNext);
	}

	insert_node(&heap.ohFree, pohFreed);

	return (pohFreed);
}

/*
 *  Frees our basic data structure allocation unit by adding it to a free
 *  list.
 */

static void free_node(struct OH *poh)
{
	if (poh == NULL) {
		return;
	}

	poh->pohNext = heap.pohFreeList;
	heap.pohFreeList = poh;

	return;
}

void sis_malloc(struct sis_memreq *req)
{
	struct OH *poh;

	poh = poh_allocate(req->size);

	if (poh == NULL) {
		req->offset = 0;
		req->size = 0;
		DPRINTK("sisfb: VMEM Allocation Failed\n");
	} else {
		DPRINTK("sisfb: VMEM Allocation Successed : 0x%p\n",
			(char *) (poh->ulOffset +
				  (unsigned long) ivideo.video_vbase));

		req->offset = poh->ulOffset;
		req->size = poh->ulSize;
	}

}

void sis_free(unsigned long base)
{
	struct OH *poh;

	poh = poh_free(base);

	if (poh == NULL) {
		DPRINTK("sisfb: poh_free() failed at base 0x%x\n",
			(unsigned int) base);
	}

}



/* ---------------------- SetMode Routines -------------------------- */

static void set_reg1(u16 port, u16 index, u16 data)
{
	outb((u8) (index & 0xff), port);
	port++;
	outb((u8) (data & 0xff), port);
}

static void set_reg3(u16 port, u16 data)
{
	outb((u8) (data & 0xff), port);
}

static void set_reg4(u16 port, unsigned long data)
{
	outl((u32) (data & 0xffffffff), port);
}

static u8 get_reg1(u16 port, u16 index)
{
	u8 data;

	outb((u8) (index & 0xff), port);
	port += 1;
	data = inb(port);
	return (data);
}

static u8 get_reg2(u16 port)
{
	u8 data;

	data = inb(port);

	return (data);
}

static u32 get_reg3(u16 port)
{
	u32 data;

	data = inl(port);
	return (data);
}

static u16 get_modeID_length(unsigned long ROMAddr, u16 ModeNo)
{
	unsigned char ModeID;
	u16 modeidlength;
	u16 usModeIDOffset;
	unsigned short PreviousWord,CurrentWord;

	return(10);
   
	modeidlength=0;
   	usModeIDOffset=*((unsigned short *)(ROMAddr+0x20A));      // Get EModeIDTable

   	CurrentWord=*((unsigned short *)(ROMAddr+usModeIDOffset));     // Offset 0x20A
   	PreviousWord=*((unsigned short *)(ROMAddr+usModeIDOffset-2));     // Offset 0x20A
   	while((CurrentWord!=0x2E07)||(PreviousWord!=0x0801)) 
	{
      	modeidlength++;
      	usModeIDOffset=usModeIDOffset+1;               // 10 <= ExtStructSize
      	CurrentWord=*((unsigned short *)(ROMAddr+usModeIDOffset));
      	PreviousWord=*((unsigned short *)(ROMAddr+usModeIDOffset-2)); 
   	}
   	modeidlength++;

   	return(modeidlength);
}

static int search_modeID(unsigned long ROMAddr, u16 ModeNo)
{
	unsigned char ModeID;
	u16 usIDLength;
	unsigned int count = 0;

	ModeIDOffset = *((u16 *) (ROMAddr + 0x20A));
	ModeID = *((unsigned char *) (ROMAddr + ModeIDOffset));
	usIDLength = get_modeID_length(ROMAddr, ModeNo);
	while (ModeID != 0xff && ModeID != ModeNo) {
		ModeIDOffset = ModeIDOffset + usIDLength;
		ModeID = *((unsigned char *) (ROMAddr + ModeIDOffset));
		if (count++ >= 0xff)
			break;
	}
	if (ModeID == 0xff)
		return (FALSE);
	else
		return (TRUE);
}

static int check_memory_size(unsigned long ROMAddr)
{
	u16 memorysize;
	u16 modeflag;
	u16 temp;

	modeflag = *((u16 *) (ROMAddr + ModeIDOffset + 0x01));
	ModeType = modeflag & ModeInfoFlag;

	memorysize = modeflag & MemoryInfoFlag;
	memorysize = memorysize >> MemorySizeShift;
	memorysize++;

	temp = get_reg1(P3c4, 0x14);
	temp = temp & 0x3F;
	temp++;

	if (temp < memorysize)
		return (FALSE);
	else
		return (TRUE);
}

static void get_mode_ptr(unsigned long ROMAddr, u16 ModeNo)
{
	unsigned char index;

	StandTable = *((u16 *) (ROMAddr + 0x202));

	if (ModeNo <= 13)
		index = *((unsigned char *) (ROMAddr + ModeIDOffset + 0x03));
	else {
		if (ModeType <= 0x02)
			index = 0x1B;
		else
			index = 0x0F;
	}

	StandTable = StandTable + 64 * index;

}

static void set_seq_regs(unsigned long ROMAddr)
{
	unsigned char SRdata;
	u16 i;

#ifdef CONFIG_FB_SIS_LINUXBIOS
	SRdata = SRegs[0x01];
#else
	set_reg1(P3c4, 0x00, 0x03);
	StandTable = StandTable + 0x05;
	SRdata = *((unsigned char *) (ROMAddr + StandTable));
#endif

	SRdata = SRdata | 0x20;
	set_reg1(P3c4, 0x01, SRdata);


	for (i = 02; i <= 04; i++) {
#ifdef CONFIG_FB_SIS_LINUXBIOS
		SRdata = SRegs[i];
#else
		StandTable++;
		SRdata = *((unsigned char *) (ROMAddr + StandTable));
#endif
		set_reg1(P3c4, i, SRdata);
	}
}

static void set_misc_regs(unsigned long ROMAddr)
{
#ifdef CONFIG_FB_SIS_LINUXBIOS
	set_reg3(P3c2, 0x23);
#else
	unsigned char Miscdata;

	StandTable++;
	Miscdata = *((unsigned char *) (ROMAddr + StandTable));
	set_reg3(P3c2, Miscdata);
#endif
}

static void set_crtc_regs(unsigned long ROMAddr)
{
	unsigned char CRTCdata;
	u16 i;

	CRTCdata = (unsigned char) get_reg1(P3d4, 0x11);
#ifndef CONFIG_FB_SIS_LINUXBIOS
	CRTCdata = CRTCdata & 0x7f;
#endif
	set_reg1(P3d4, 0x11, CRTCdata);

	for (i = 0; i <= 0x18; i++) {
#ifdef CONFIG_FB_SIS_LINUXBIOS
		set_reg1(P3d4, i, CRegs[i]);
#else
		StandTable++;
		CRTCdata = *((unsigned char *) (ROMAddr + StandTable));
		set_reg1(P3d4, i, CRTCdata);
#endif
	}
}

static void set_attregs(unsigned long ROMAddr)
{
	unsigned char ARdata;
	u16 i;

	for (i = 0; i <= 0x13; i++) {
#ifdef CONFIG_FB_SIS_LINUXBIOS
		get_reg2(P3da);
		set_reg3(P3c0, i);
		set_reg3(P3c0, ARegs[i]);
#else
		StandTable++;
		ARdata = *((unsigned char *) (ROMAddr + StandTable));

		get_reg2(P3da);
		set_reg3(P3c0, i);
		set_reg3(P3c0, ARdata);
#endif
	}

	get_reg2(P3da);
	set_reg3(P3c0, 0x14);
	set_reg3(P3c0, 0x00);
	get_reg2(P3da);
	set_reg3(P3c0, 0x20);
}

static void set_grc_regs(unsigned long ROMAddr)
{
	unsigned char GRdata;
	u16 i;

	for (i = 0; i <= 0x08; i++) {
#ifdef CONFIG_FB_SIS_LINUXBIOS
		set_reg1(P3ce, i, GRegs[i]);
#else
		StandTable++;
		GRdata = *((unsigned char *) (ROMAddr + StandTable));
		set_reg1(P3ce, i, GRdata);
#endif
	}

#ifndef CONFIG_FB_SIS_LINUXBIOS
	if (ModeType > ModeVGA) {
		GRdata = (unsigned char) get_reg1(P3ce, 0x05);
		GRdata = GRdata & 0xBF;
		set_reg1(P3ce, 0x05, GRdata);
	}
#endif
}

static void ClearExt1Regs(void)
{
	u16 i;

	for (i = 0x0A; i <= 0x0E; i++)
		set_reg1(P3c4, i, 0x00);
}

static u16 GetRefindexLength(unsigned long ROMAddr, u16 ModeNo)
{
	unsigned char ModeID;
	unsigned char temp;
	u16 refindexlength;
	u16 usModeIDOffset;
	u16 usREFIndex;
	u16 usIDLength;

	usModeIDOffset = *((u16 *) (ROMAddr + 0x20A));
	ModeID = *((unsigned char *) (ROMAddr + usModeIDOffset));
	usIDLength = get_modeID_length(ROMAddr, ModeNo);
	while (ModeID != 0x40) {
		usModeIDOffset = usModeIDOffset + usIDLength;
		ModeID = *((unsigned char *) (ROMAddr + usModeIDOffset));
	}

	refindexlength = 1;
	usREFIndex = *((u16 *) (ROMAddr + usModeIDOffset + 0x04));
	usREFIndex++;
	temp = *((unsigned char *) (ROMAddr + usREFIndex));
	while (temp != 0xFF) {
		refindexlength++;
		usREFIndex++;
		temp = *((unsigned char *) (ROMAddr + usREFIndex));
	}
	return (refindexlength);
}

static int get_rate_ptr(unsigned long ROMAddr, u16 ModeNo)
{
	short index;
	u16 temp;
	u16 ulRefIndexLength;

	if (ModeNo < 0x14)
		return (FALSE);

	index = get_reg1(P3d4, 0x33);
	index = index & 0x0F;
	if (index != 0)
		index--;

	REFIndex = *((u16 *) (ROMAddr + ModeIDOffset + 0x04));

	ulRefIndexLength = GetRefindexLength(ROMAddr, ModeNo);

	do {
		temp = *((u16 *) (ROMAddr + REFIndex));
		if (temp == 0xFFFF)
			break;
		temp = temp & ModeInfoFlag;
		if (temp < ModeType)
			break;

		REFIndex = REFIndex + ulRefIndexLength;
		index--;
	} while (index >= 0);

	REFIndex = REFIndex - ulRefIndexLength;
	return (TRUE);
}

static void set_sync(unsigned long ROMAddr)
{
#ifdef CONFIG_FB_SIS_LINUXBIOS
	set_reg3(P3c2, MReg);
#else
	u16 sync;
	u16 temp;

	sync = *((u16 *) (ROMAddr + REFIndex));
	sync = sync & 0xC0;
	temp = 0x2F;
	temp = temp | sync;
	set_reg3(P3c2, temp);
#endif
}

static void set_crt1_crtc(unsigned long ROMAddr)
{
#ifdef CONFIG_FB_SIS_LINUXBIOS
	unsigned char data;
	u16 i;

	data = (unsigned char) get_reg1(P3d4, 0x11);
	data = data & 0x7F;
	set_reg1(P3d4, 0x11, data);

	for (i = 0; i <= 0x07; i++)
		set_reg1(P3d4, i, CRegs[i]);
	for (i = 0x10; i <= 0x12; i++)
		set_reg1(P3d4, i, CRegs[i]);
	for (i = 0x15; i <= 0x16; i++)
		set_reg1(P3d4, i, CRegs[i]);
	for (i = 0x0A; i <= 0x0C; i++)
		set_reg1(P3c4, i, SRegs[i]);

	data = SRegs[0x0E] & 0xE0;
	set_reg1(P3c4, 0x0E, data);

	set_reg1(P3d4, 0x09, CRegs[0x09]);
#else
	unsigned char index;
	unsigned char data;
	u16 i;

	index = *((unsigned char *) (ROMAddr + REFIndex + 0x02));
	CRT1Table = *((u16 *) (ROMAddr + 0x204));
	CRT1Table = CRT1Table + index * CRT1Len;

	data = (unsigned char) get_reg1(P3d4, 0x11);
	data = data & 0x7F;
	set_reg1(P3d4, 0x11, data);

	CRT1Table--;
	for (i = 0; i <= 0x05; i++) {
		CRT1Table++;
		data = *((unsigned char *) (ROMAddr + CRT1Table));
		set_reg1(P3d4, i, data);
	}
	for (i = 0x06; i <= 0x07; i++) {
		CRT1Table++;
		data = *((unsigned char *) (ROMAddr + CRT1Table));
		set_reg1(P3d4, i, data);
	}
	for (i = 0x10; i <= 0x12; i++) {
		CRT1Table++;
		data = *((unsigned char *) (ROMAddr + CRT1Table));
		set_reg1(P3d4, i, data);
	}
	for (i = 0x15; i <= 0x16; i++) {
		CRT1Table++;
		data = *((unsigned char *) (ROMAddr + CRT1Table));
		set_reg1(P3d4, i, data);
	}
	for (i = 0x0A; i <= 0x0C; i++) {
		CRT1Table++;
		data = *((unsigned char *) (ROMAddr + CRT1Table));
		set_reg1(P3c4, i, data);
	}

	CRT1Table++;
	data = *((unsigned char *) (ROMAddr + CRT1Table));
	data = data & 0xE0;
	set_reg1(P3c4, 0x0E, data);

	data = (unsigned char) get_reg1(P3d4, 0x09);
	data = data & 0xDF;
	i = *((unsigned char *) (ROMAddr + CRT1Table));
	i = i & 0x01;
	i = i << 5;
	data = data | i;
	i = *((u16 *) (ROMAddr + ModeIDOffset + 0x01));
	i = i & DoubleScanMode;
	if (i)
		data = data | 0x80;
	set_reg1(P3d4, 0x09, data);

	if (ModeType > 0x03)
		set_reg1(P3d4, 0x14, 0x4F);
#endif
}


static void set_crt1_offset(unsigned long ROMAddr)
{
#ifdef CONFIG_FB_SIS_LINUXBIOS
	set_reg1(P3c4, 0x0E, SRegs[0x0E]);
	set_reg1(P3c4, 0x10, SRegs[0x10]);
#else
	u16 temp, ah, al;
	u16 temp2, i;
	u16 DisplayUnit;

	temp = *((unsigned char *) (ROMAddr + ModeIDOffset + 0x03));
	temp = temp >> 4;
	ScreenOffset = *((u16 *) (ROMAddr + 0x206));
	temp = *((unsigned char *) (ROMAddr + ScreenOffset + temp));

	temp2 = *((u16 *) (ROMAddr + REFIndex + 0x00));
	temp2 = temp2 & InterlaceMode;
	if (temp2)
		temp = temp << 1;
	temp2 = ModeType - ModeEGA;
	switch (temp2) {
	case 0:
		temp2 = 1;
		break;
	case 1:
		temp2 = 2;
		break;
	case 2:
		temp2 = 4;
		break;
	case 3:
		temp2 = 4;
		break;
	case 4:
		temp2 = 6;
		break;
	case 5:
		temp2 = 8;
		break;
	}
	temp = temp * temp2;
	DisplayUnit = temp;

	temp2 = temp;
	temp = temp >> 8;
	temp = temp & 0x0F;
	i = get_reg1(P3c4, 0x0E);
	i = i & 0xF0;
	i = i | temp;
	set_reg1(P3c4, 0x0E, i);

	temp = (unsigned char) temp2;
	temp = temp & 0xFF;
	set_reg1(P3d4, 0x13, temp);

	temp2 = *((u16 *) (ROMAddr + REFIndex + 0x00));
	temp2 = temp2 & InterlaceMode;
	if (temp2)
		DisplayUnit >>= 1;

	DisplayUnit = DisplayUnit << 5;
	ah = (DisplayUnit & 0xff00) >> 8;
	al = DisplayUnit & 0x00ff;
	if (al == 0)
		ah = ah + 1;
	else
		ah = ah + 2;
	set_reg1(P3c4, 0x10, ah);
#endif
}

static u16 get_vclk_len(unsigned long ROMAddr)
{
	u16 VCLKDataStart, vclklabel, temp;
	VCLKDataStart = *((u16 *) (ROMAddr + 0x208));
	for (temp = 0;; temp++) {
		vclklabel = *((u16 *) (ROMAddr + VCLKDataStart + temp));
		if (vclklabel == VCLKStartFreq) {
			temp = temp + 2;
			return (temp);
		}
	}
	return (0);
}

static void set_crt1_vclk(unsigned long ROMAddr)
{
	u16 i;

#ifndef CONFIG_FB_SIS_LINUXBIOS
	unsigned char index, data;

	index = *((unsigned char *) (ROMAddr + REFIndex + 0x03));
	index &= 0x03F;
	CRT1VCLKLen = get_vclk_len(ROMAddr);
	data = index * CRT1VCLKLen;
	VCLKData = *((u16 *) (ROMAddr + 0x208));
	VCLKData = VCLKData + data;
#endif

	set_reg1(P3c4, 0x31, 0);

	for (i = 0x2B; i <= 0x2C; i++) {
#ifdef CONFIG_FB_SIS_LINUXBIOS
		set_reg1(P3c4, i, SRegs[i]);
#else
		data = *((unsigned char *) (ROMAddr + VCLKData));
		set_reg1(P3c4, i, data);
		VCLKData++;
#endif
	}
	set_reg1(P3c4, 0x2D, 0x80);
}
static void set_vclk_state(unsigned long ROMAddr, u16 ModeNo)
{
#ifdef CONFIG_FB_SIS_LINUXBIOS
	set_reg1(P3c4, 0x32, SRegs[0x32]);
	set_reg1(P3c4, 0x07, SRegs[0x07]);
#else

	u16 data, data2;
	u16 VCLK;
	unsigned char index;

	index = *((unsigned char *) (ROMAddr + REFIndex + 0x03));
	index &= 0x3F;
	CRT1VCLKLen = get_vclk_len(ROMAddr);
	data = index * CRT1VCLKLen;
	VCLKData = *((u16 *) (ROMAddr + 0x208));
	VCLKData = VCLKData + data + (CRT1VCLKLen - 2);

	VCLK = *((u16 *) (ROMAddr + VCLKData));
	if (ModeNo <= 0x13)
		VCLK = 0;

	data = get_reg1(P3c4, 0x07);
	data = data & 0x7B;
	if (VCLK >= 150)
		data = data | 0x80;
	set_reg1(P3c4, 0x07, data);

	data = get_reg1(P3c4, 0x32);
	data = data & 0xD7;
	if (VCLK >= 150)
		data = data | 0x08;
	set_reg1(P3c4, 0x32, data);

	data2 = 0x03;
	if (VCLK > 135)
		data2 = 0x02;
	if (VCLK > 160)
		data2 = 0x01;
	if (VCLK > 260)
		data2 = 0x00;
	data = get_reg1(P3c4, 0x07);
	data = data & 0xFC;
	data = data | data2;
	set_reg1(P3c4, 0x07, data);
#endif
}

static u16 calc_delay2(unsigned long ROMAddr, u16 key)
{
	u16 data, index;
	unsigned char LatencyFactor[] = { 
		88, 80, 78, 72, 70, 00,
		00, 79, 77, 71, 69, 49,
		88, 80, 78, 72, 70, 00,
		00, 72, 70, 64, 62, 44
	};
	index = 0;
	data = get_reg1(P3c4, 0x14);
	if (data & 0x80)
		index = index + 12;

	data = get_reg1(P3c4, 0x15);
	data = (data & 0xf0) >> 4;
	if (data & 0x01)
		index = index + 6;

	data = data >> 1;
	index = index + data;
	data = LatencyFactor[index];

	return (data);
}

static u16 calc_delay(unsigned long ROMAddr, u16 key)
{
	u16 data, data2, temp0, temp1;
	unsigned char ThLowA[] = { 
		61, 3, 52, 5, 68, 7, 100, 11,
		43, 3, 42, 5, 54, 7, 78, 11,
		34, 3, 37, 5, 47, 7, 67, 11
	};
	unsigned char ThLowB[] = { 
		81, 4, 72, 6, 88, 8, 120, 12,
		55, 4, 54, 6, 66, 8, 90, 12,
		42, 4, 45, 6, 55, 8, 75, 12
	};
	unsigned char ThTiming[] = { 1, 2, 2, 3, 0, 1, 1, 2 };

	data = get_reg1(P3c4, 0x16);
	data = data >> 6;
	data2 = get_reg1(P3c4, 0x14);
	data2 = (data2 >> 4) & 0x0C;
	data = data | data2;
	data = data < 1;
	if (key == 0) {
		temp0 = (u16) ThLowA[data];
		temp1 = (u16) ThLowA[data + 1];
	} else {
		temp0 = (u16) ThLowB[data];
		temp1 = (u16) ThLowB[data + 1];
	}

	data2 = 0;
	data = get_reg1(P3c4, 0x18);
	if (data & 0x02)
		data2 = data2 | 0x01;
	if (data & 0x20)
		data2 = data2 | 0x02;
	if (data & 0x40)
		data2 = data2 | 0x04;

	data = temp1 * ThTiming[data2] + temp0;
	return (data);
}


static void set_crt1_FIFO(unsigned long ROMAddr)
{
	u16 index, data, VCLK, data2, MCLKOffset, MCLK, colorth = 1;
	u16 ah, bl, A, B;

	index = *((unsigned char *) (ROMAddr + REFIndex + 0x03));
	index &= 0x3F;
	CRT1VCLKLen = get_vclk_len(ROMAddr);
	data = index * CRT1VCLKLen;
	VCLKData = *((u16 *) (ROMAddr + 0x208));
	VCLKData = VCLKData + data + (CRT1VCLKLen - 2);
	VCLK = *((u16 *) (ROMAddr + VCLKData));

	MCLKOffset = *((u16 *) (ROMAddr + 0x20C));
	index = get_reg1(P3c4, 0x3A);
	index = index & 07;
	MCLKOffset = MCLKOffset + index * 5;
	MCLK = *((unsigned char *) (ROMAddr + MCLKOffset + 0x03));

	data2 = ModeType - 0x02;
	switch (data2) {
	case 0:
		colorth = 1;
		break;
	case 1:
		colorth = 2;
		break;
	case 2:
		colorth = 4;
		break;
	case 3:
		colorth = 4;
		break;
	case 4:
		colorth = 6;
		break;
	case 5:
		colorth = 8;
		break;
	}

	do {
		B = (calc_delay(ROMAddr, 0) * VCLK * colorth);
		B = B / (16 * MCLK);
		B++;

		A = (calc_delay(ROMAddr, 1) * VCLK * colorth);
		A = A / (16 * MCLK);
		A++;

		if (A < 4)
			A = 0;
		else
			A = A - 4;

		if (A > B)
			bl = A;
		else
			bl = B;

		bl++;
		if (bl > 0x13) {
			data = get_reg1(P3c4, 0x16);
			data = data >> 6;
			if (data != 0) {
				data--;
				data = data << 6;
				data2 = get_reg1(P3c4, 0x16);
				data2 = (data2 & 0x3f) | data;
				set_reg1(P3c4, 0x16, data2);
			} else
				bl = 0x13;
		}
	} while (bl > 0x13);

	ah = bl;
	ah = ah << 4;
	ah = ah | 0x0f;
	set_reg1(P3c4, 0x08, ah);

	data = bl;
	data = data & 0x10;
	data = data << 1;
	data2 = get_reg1(P3c4, 0x0F);
	data2 = data2 & 0x9f;
	data2 = data2 | data;
	set_reg1(P3c4, 0x0F, data2);

	data = bl + 3;
	if (data > 0x0f)
		data = 0x0f;
	set_reg1(P3c4, 0x3b, 0x00);
	data2 = get_reg1(P3c4, 0x09);
	data2 = data2 & 0xF0;
	data2 = data2 | data;
	set_reg1(P3c4, 0x09, data2);
}

static void set_crt1_FIFO2(unsigned long ROMAddr)
{
#ifdef CONFIG_FB_SIS_LINUXBIOS
	set_reg1(P3c4, 0x15, SRegs[0x15]);

	set_reg4(0xcf8, 0x80000050);
	set_reg4(0xcfc, 0xc5041e04);

	set_reg1(P3c4, 0x08, SRegs[0x08]);
	set_reg1(P3c4, 0x0F, SRegs[0x0F]);
	set_reg1(P3c4, 0x3b, 0x00);
	set_reg1(P3c4, 0x09, SRegs[0x09]);
#else

	u16 index, data, VCLK, data2, MCLKOffset, MCLK, colorth = 1;
	u16 ah, bl, B;
	unsigned long eax;

	index = *((unsigned char *) (ROMAddr + REFIndex + 0x03));
	index &= 0x3F;
	CRT1VCLKLen = get_vclk_len(ROMAddr);
	data = index * CRT1VCLKLen;
	VCLKData = *((u16 *) (ROMAddr + 0x208));
	VCLKData = VCLKData + data + (CRT1VCLKLen - 2);
	VCLK = *((u16 *) (ROMAddr + VCLKData));

	MCLKOffset = *((u16 *) (ROMAddr + 0x20C));
	index = get_reg1(P3c4, 0x1A);
	index = index & 07;
	MCLKOffset = MCLKOffset + index * 5;
	MCLK = *((u16 *) (ROMAddr + MCLKOffset + 0x03));

	data2 = ModeType - 0x02;
	switch (data2) {
	case 0:
		colorth = 1;
		break;
	case 1:
		colorth = 1;
		break;
	case 2:
		colorth = 2;
		break;
	case 3:
		colorth = 2;
		break;
	case 4:
		colorth = 3;
		break;
	case 5:
		colorth = 4;
		break;
	}

	do {
		B = (calc_delay2(ROMAddr, 0) * VCLK * colorth);
		if (B % (16 * MCLK) == 0) {
			B = B / (16 * MCLK);
			bl = B + 1;
		} else {
			B = B / (16 * MCLK);
			bl = B + 2;
		}

		if (bl > 0x13) {
			data = get_reg1(P3c4, 0x15);
			data = data & 0xf0;
			if (data != 0xb0) {
				data = data + 0x20;
				if (data == 0xa0)
					data = 0x30;
				data2 = get_reg1(P3c4, 0x15);
				data2 = (data2 & 0x0f) | data;
				set_reg1(P3c4, 0x15, data2);
			} else
				bl = 0x13;
		}
	} while (bl > 0x13);

	data2 = get_reg1(P3c4, 0x15);
	data2 = (data2 & 0xf0) >> 4;
	data2 = data2 << 24;

	set_reg4(0xcf8, 0x80000050);
	eax = get_reg3(0xcfc);
	eax = eax & 0x0f0ffffff;
	eax = eax | data2;
	set_reg4(0xcfc, eax);

	ah = bl;
	ah = ah << 4;
	ah = ah | 0x0f;
	set_reg1(P3c4, 0x08, ah);

	data = bl;
	data = data & 0x10;
	data = data << 1;
	data2 = get_reg1(P3c4, 0x0F);
	data2 = data2 & 0x9f;
	data2 = data2 | data;
	set_reg1(P3c4, 0x0F, data2);

	data = bl + 3;
	if (data > 0x0f)
		data = 0x0f;
	set_reg1(P3c4, 0x3b, 0x00);
	data2 = get_reg1(P3c4, 0x09);
	data2 = data2 & 0xF0;
	data2 = data2 | data;
	set_reg1(P3c4, 0x09, data2);
#endif
}

static void set_crt1_mode_regs(unsigned long ROMAddr, u16 ModeNo)
{
#ifdef CONFIG_FB_SIS_LINUXBIOS
	set_reg1(P3c4, 0x06, SRegs[0x06]);
	set_reg1(P3c4, 0x01, SRegs[0x01]);
	set_reg1(P3c4, 0x0F, SRegs[0x0F]);
	set_reg1(P3c4, 0x21, SRegs[0x21]);
#else

	u16 data, data2, data3;

	if (ModeNo > 0x13)
		data = *((u16 *) (ROMAddr + REFIndex + 0x00));
	else
		data = 0;

	data2 = 0;
	if (ModeNo > 0x13)
		if (ModeType > 0x02) {
			data2 = data2 | 0x02;
			data3 = ModeType - ModeVGA;
			data3 = data3 << 2;
			data2 = data2 | data3;
		}

	data = data & InterlaceMode;
	if (data)
		data2 = data2 | 0x20;
	set_reg1(P3c4, 0x06, data2);

	data = get_reg1(P3c4, 0x01);
	data = data & 0xF7;
	data2 = *((u16 *) (ROMAddr + ModeIDOffset + 0x01));
	data2 = data2 & HalfDCLK;
	if (data2)
		data = data | 0x08;
	set_reg1(P3c4, 0x01, data);

	data = get_reg1(P3c4, 0x0F);
	data = data & 0xF7;
	data2 = *((u16 *) (ROMAddr + ModeIDOffset + 0x01));
	data2 = data2 & LineCompareOff;
	if (data2)
		data = data | 0x08;
	set_reg1(P3c4, 0x0F, data);

	data = get_reg1(P3c4, 0x21);
	data = data & 0x1F;
	if (ModeType == 0x00)
		data = data | 0x60;
	else if (ModeType <= 0x02)
		data = data | 0x00;
	else
		data = data | 0xA0;
	set_reg1(P3c4, 0x21, data);
#endif
}

static void set_interlace(unsigned long ROMAddr, u16 ModeNo)
{
#ifdef CONFIG_FB_SIS_LINUXBIOS
	set_reg1(P3d4, 0x19, CRegs[0x19]);
	set_reg1(P3d4, 0x1A, CRegs[0x1A]);
#else

	unsigned long Temp;
	u16 data, Temp2;

	Temp = (unsigned long) get_reg1(P3d4, 0x01);
	Temp++;
	Temp = Temp * 8;

	if (Temp == 1024)
		data = 0x0035;
	else if (Temp == 1280)
		data = 0x0048;
	else
		data = 0x0000;

	Temp2 = *((u16 *) (ROMAddr + REFIndex + 0x00));
	Temp2 &= InterlaceMode;
	if (Temp2 == 0)
		data = 0x0000;

	set_reg1(P3d4, 0x19, data);

	Temp = (unsigned long) get_reg1(P3d4, 0x1A);
	Temp2 = (u16) (Temp & 0xFC);
	set_reg1(P3d4, 0x1A, (u16) Temp);

	Temp = (unsigned long) get_reg1(P3c4, 0x0f);
	Temp2 = (u16) Temp & 0xBF;
	if (ModeNo == 0x37)
		Temp2 = Temp2 | 0x40;
	set_reg1(P3d4, 0x1A, (u16) Temp2);
#endif
}

static void write_DAC(u16 dl, u16 ah, u16 al, u16 dh)
{
	u16 temp;
	u16 bh, bl;

	bh = ah;
	bl = al;
	if (dl != 0) {
		temp = bh;
		bh = dh;
		dh = temp;
		if (dl == 1) {
			temp = bl;
			bl = dh;
			dh = temp;
		} else {
			temp = bl;
			bl = bh;
			bh = temp;
		}
	}
	set_reg3(P3c9, (u16) dh);
	set_reg3(P3c9, (u16) bh);
	set_reg3(P3c9, (u16) bl);
}


static void load_DAC(unsigned long ROMAddr)
{
	u16 data, data2;
	u16 time, i, j, k;
	u16 m, n, o;
	u16 si, di, bx, dl;
	u16 al, ah, dh;
	u16 *table = VGA_DAC;

#ifndef CONFIG_FB_SIS_LINUXBIOS
	data = *((u16 *) (ROMAddr + ModeIDOffset + 0x01));
	data = data & DACInfoFlag;
	time = 64;
	if (data == 0x00)
		table = MDA_DAC;
	if (data == 0x08)
		table = CGA_DAC;
	if (data == 0x10)
		table = EGA_DAC;
	if (data == 0x18) {
		time = 256;
		table = VGA_DAC;
	}
#else
	time = 256;
	table = VGA_DAC;
#endif

	if (time == 256)
		j = 16;
	else
		j = time;

	set_reg3(P3c6, 0xFF);
	set_reg3(P3c8, 0x00);

	for (i = 0; i < j; i++) {
		data = table[i];
		for (k = 0; k < 3; k++) {
			data2 = 0;
			if (data & 0x01)
				data2 = 0x2A;
			if (data & 0x02)
				data2 = data2 + 0x15;
			set_reg3(P3c9, data2);
			data = data >> 2;
		}
	}

	if (time == 256) {
		for (i = 16; i < 32; i++) {
			data = table[i];
			for (k = 0; k < 3; k++)
				set_reg3(P3c9, data);
		}
		si = 32;
		for (m = 0; m < 9; m++) {
			di = si;
			bx = si + 0x04;
			dl = 0;
			for (n = 0; n < 3; n++) {
				for (o = 0; o < 5; o++) {
					dh = table[si];
					ah = table[di];
					al = table[bx];
					si++;
					write_DAC(dl, ah, al, dh);
				}
				si = si - 2;
				for (o = 0; o < 3; o++) {
					dh = table[bx];
					ah = table[di];
					al = table[si];
					si--;
					write_DAC(dl, ah, al, dh);
				}
				dl++;
			}
			si = si + 5;
		}
	}
}

static void display_on(void)
{
	u16 data;

	data = get_reg1(P3c4, 0x01);
	data = data & 0xDF;
	set_reg1(P3c4, 0x01, data);
}

void SetMemoryClock(void)
{
	unsigned char i;
	int idx;

	u8 MCLK[] = {
		0x5A, 0x64, 0x80, 0x66, 0x00,	// SDRAM
		0xB3, 0x45, 0x80, 0x83, 0x00,	// SGRAM
		0x37, 0x61, 0x80, 0x00, 0x01,	// ESDRAM
		0x37, 0x22, 0x80, 0x33, 0x01,
		0x37, 0x61, 0x80, 0x00, 0x01,
		0x37, 0x61, 0x80, 0x00, 0x01,
		0x37, 0x61, 0x80, 0x00, 0x01,
		0x37, 0x61, 0x80, 0x00, 0x01
	};

	u8 ECLK[] = {
		0x54, 0x43, 0x80, 0x00, 0x01,
		0x53, 0x43, 0x80, 0x00, 0x01,
		0x55, 0x43, 0x80, 0x00, 0x01,
		0x52, 0x43, 0x80, 0x00, 0x01,
		0x3f, 0x42, 0x80, 0x00, 0x01,
		0x54, 0x43, 0x80, 0x00, 0x01,
		0x54, 0x43, 0x80, 0x00, 0x01,
		0x54, 0x43, 0x80, 0x00, 0x01
	};

	idx = RAMType * 5;

	for (i = 0x28; i <= 0x2A; i++) {	// Set MCLK
		set_reg1(P3c4, i, MCLK[idx]);
		idx++;
	}

	idx = RAMType * 5;
	for (i = 0x2E; i <= 0x30; i++) {	// Set ECLK
		set_reg1(P3c4, i, ECLK[idx]);
		idx++;
	}
}

void ClearDAC(u16 port)
{
	int i;

	set_reg3(P3c8, 0x00);
	for (i = 0; i < (256 * 3); i++)
		set_reg3(P3c9, 0x00);
}

void ClearALLBuffer(void)
{
	unsigned long AdapterMemorySize;

	AdapterMemorySize = get_reg1(P3c4, 0x14);
	AdapterMemorySize = AdapterMemorySize & 0x3F;
	AdapterMemorySize++;

	memset((char *) ivideo.video_vbase, 0, AdapterMemorySize);
}

void LongWait(void)
{
	unsigned long temp;

	for (temp = 1; temp > 0;) {
		temp = get_reg2(P3da);
		temp = temp & 0x08;
	}
	for (; temp == 0;) {
		temp = get_reg2(P3da);
		temp = temp & 0x08;
	}
}

void WaitDisplay(void)
{
	unsigned short temp;

	for (temp = 0; temp == 0;) {
		temp = get_reg2(P3da);
		temp = temp & 0x01;
	}
	for (; temp == 1;) {
		temp = get_reg2(P3da);
		temp = temp & 0x01;
	}
}

int TestMonitorType(unsigned short d1, unsigned short d2, unsigned short d3)
{
	unsigned short temp;
	set_reg3(P3c6, 0xFF);
	set_reg3(P3c8, 0x00);
	set_reg3(P3c9, d1);
	set_reg3(P3c9, d2);
	set_reg3(P3c9, d3);
	WaitDisplay();		//wait horizontal retrace
	temp = get_reg2(P3c2);
	if (temp & 0x10)
		return 1;
	else
		return 0;
}

void SetRegANDOR(unsigned short Port, unsigned short Index,
		 unsigned short DataAND, unsigned short DataOR)
{
	unsigned short temp1;
	temp1 = get_reg1(Port, Index);	//part1port index 02
	temp1 = (temp1 & (DataAND)) | DataOR;
	set_reg1(Port, Index, temp1);
}


int DetectMonitor(void)
{
	unsigned short flag1;
	unsigned short DAC_TEST_PARMS[3] = { 0x0F, 0x0F, 0x0F };
	unsigned short DAC_CLR_PARMS[3] = { 0x00, 0x00, 0x00 };

	flag1 = get_reg1(P3c4, 0x38);	//call BridgeisOn
	if ((flag1 & 0x20)) {
		set_reg1(P3d4, 0x30, 0x41);
	}

	SiSSetMode(0x2E);	//set mode to 0x2E instead of 0x3

	ClearDAC(P3c8);
	ClearALLBuffer();

	LongWait();
	LongWait();

	flag1 = TestMonitorType(DAC_TEST_PARMS[0], DAC_TEST_PARMS[1],
				DAC_TEST_PARMS[2]);
	if (flag1 == 0) {
		flag1 = TestMonitorType(DAC_TEST_PARMS[0], DAC_TEST_PARMS[1],
					DAC_TEST_PARMS[2]);
	}

	if (flag1 == 1) {
		SetRegANDOR(P3d4, 0x32, ~Monitor1Sense, Monitor1Sense);
	} else {
		SetRegANDOR(P3d4, 0x32, ~Monitor1Sense, 0x0);
	}

	TestMonitorType(DAC_CLR_PARMS[0], DAC_CLR_PARMS[1],
			DAC_CLR_PARMS[2]);

	set_reg1(P3d4, 0x34, 0x4A);

	return 1;
}

int SiSInit300(void)
{
	//unsigned long ROMAddr = rom_vbase;
	u16 BaseAddr = (u16) ivideo.vga_base;
	unsigned char i, temp, AGP;
	unsigned long j, k, ulTemp;
	unsigned char SR11, SR19, SR1A, SR21, SR22;
	unsigned char SR14;
	unsigned long Temp;

	P3c4 = BaseAddr + 0x14;
	P3d4 = BaseAddr + 0x24;
	P3c0 = BaseAddr + 0x10;
	P3ce = BaseAddr + 0x1e;
	P3c2 = BaseAddr + 0x12;
	P3ca = BaseAddr + 0x1a;
	P3c6 = BaseAddr + 0x16;
	P3c7 = BaseAddr + 0x17;
	P3c8 = BaseAddr + 0x18;
	P3c9 = BaseAddr + 0x19;
	P3da = BaseAddr + 0x2A;

	set_reg1(P3c4, 0x05, 0x86);	// 1.Openkey

	SR14 = (unsigned char) get_reg1(P3c4, 0x14);
	SR19 = (unsigned char) get_reg1(P3c4, 0x19);
	SR1A = (unsigned char) get_reg1(P3c4, 0x1A);

	for (i = 0x06; i < 0x20; i++)
		set_reg1(P3c4, i, 0);	// 2.Reset Extended register
	for (i = 0x21; i <= 0x27; i++)
		set_reg1(P3c4, i, 0);	//   Reset Extended register
	for (i = 0x31; i <= 0x3D; i++)
		set_reg1(P3c4, i, 0);
	for (i = 0x30; i <= 0x37; i++)
		set_reg1(P3d4, i, 0);

#if 0
	if ((ivideo.chip_id == SIS_Trojan) || (ivideo.chip_id == SIS_Spartan))
		// 3.Set Define Extended register
		temp = (unsigned char) SR1A;
	else {
		temp = *((unsigned char *) (ROMAddr + SoftSettingAddr));
		if ((temp & SoftDRAMType) == 0) {
			// 3.Set Define Extended register
			temp = (unsigned char) get_reg1(P3c4, 0x3A);
		}
	}
#endif

	// 3.Set Define Extended register
	temp = (unsigned char) SR1A;

	RAMType = temp & 0x07;
	SetMemoryClock();
	for (k = 0; k < 5; k++)
		for (j = 0; j < 0xffff; j++)
			ulTemp = (unsigned long) get_reg1(P3c4, 0x05);

	Temp = (unsigned long) get_reg1(P3c4, 0x3C);
	Temp = Temp | 0x01;
	set_reg1(P3c4, 0x3C, (unsigned short) Temp);
	for (k = 0; k < 5; k++)
		for (j = 0; j < 0xffff; j++)
			Temp = (unsigned long) get_reg1(P3c4, 0x05);

	Temp = (unsigned long) get_reg1(P3c4, 0x3C);
	Temp = Temp & 0xFE;
	set_reg1(P3c4, 0x3C, (unsigned short) Temp);
	for (k = 0; k < 5; k++)
		for (j = 0; j < 0xffff; j++)
			Temp = (unsigned long) get_reg1(P3c4, 0x05);

	//SR07=*((unsigned char *)(ROMAddr+0xA4));  // todo
	set_reg1(P3c4, 0x07, SRegsInit[0x07]);
#if 0
	if (HwDeviceExtension->jChipID == SIS_Glamour)
		for (i = 0x15; i <= 0x1C; i++) {
			temp = *((unsigned char *) (ROMAddr + 0xA5 + ((i - 0x15) * 8) + RAMType));
			set_reg1(P3c4, i, temp);
		}
#endif

	//SR1F=*((unsigned char *)(ROMAddr+0xE5));  
	set_reg1(P3c4, 0x1F, SRegsInit[0x1F]);

	// Get AGP
	AGP = 1;
	temp = (unsigned char) get_reg1(P3c4, 0x3A);
	temp = temp & 0x30;
	if (temp == 0x30)
		// PCI
		AGP = 0;

	//SR21=*((unsigned char *)(ROMAddr+0xE6));
	SR21 = SRegsInit[0x21];
	if (AGP == 0)
		SR21 = SR21 & 0xEF;	// PCI
	set_reg1(P3c4, 0x21, SR21);

	//SR22=*((unsigned char *)(ROMAddr+0xE7));
	SR22 = SRegsInit[0x22];
	if (AGP == 1)
		SR22 = SR22 & 0x20;	// AGP
	set_reg1(P3c4, 0x22, SR22);

	//SR23=*((unsigned char *)(ROMAddr+0xE8));
	set_reg1(P3c4, 0x23, SRegsInit[0x23]);

	//SR24=*((unsigned char *)(ROMAddr+0xE9));
	set_reg1(P3c4, 0x24, SRegsInit[0x24]);

	//SR25=*((unsigned char *)(ROMAddr+0xEA));
	set_reg1(P3c4, 0x25, SRegsInit[0x25]);

	//SR32=*((unsigned char *)(ROMAddr+0xEB));
	set_reg1(P3c4, 0x32, SRegsInit[0x32]);

	SR11 = 0x0F;
	set_reg1(P3c4, 0x11, SR11);

#if 0
	if (IF_DEF_LVDS == 1) {
		//LVDS
		temp = ExtChipLVDS;
	} else if (IF_DEF_TRUMPION == 1) {
		//Trumpion
		temp = ExtChipTrumpion;
	} else {
		//301
		temp = ExtChip301;
	}
#endif

	// 301;
	temp = 0x02;
	set_reg1(P3d4, 0x37, temp);

#if 0
	//09/07/99 modify by domao for 630/540 MM
	if (HwDeviceExtension->jChipID == SIS_Glamour) {
		//For SiS 300 Chip
		SetDRAMSize(HwDeviceExtension);
		SetDRAMSize(HwDeviceExtension);
	} else {
		//For SiS 630/540 Chip
		//Restore SR14, SR19 and SR1A
		set_reg1(P3c4, 0x14, SR14);
		set_reg1(P3c4, 0x19, SR19);
		set_reg1(P3c4, 0x1A, SR1A);
	}
#endif

	set_reg1(P3c4, 0x14, SR14);
	set_reg1(P3c4, 0x19, SR19);
	set_reg1(P3c4, 0x1A, SR1A);
	set_reg3(P3c6, 0xff);
	ClearDAC(P3c8);
	DetectMonitor();

#if 0
	//sense CRT2
	GetSenseStatus(HwDeviceExtension, BaseAddr, ROMAddr);      
#endif

	return (TRUE);
}

static int SiSSetMode(u16 ModeNo)
{
	//#ifndef CONFIG_FB_SIS_LINUXBIOS
	unsigned long temp;
	//#endif

	u16 cr30flag, cr31flag;
	unsigned long ROMAddr = rom_vbase;
	u16 BaseAddr = (u16) ivideo.vga_base;
	u_short i;

	P3c4 = BaseAddr + 0x14;
	P3d4 = BaseAddr + 0x24;
	P3c0 = BaseAddr + 0x10;
	P3ce = BaseAddr + 0x1e;
	P3c2 = BaseAddr + 0x12;
	P3ca = BaseAddr + 0x1a;
	P3c6 = BaseAddr + 0x16;
	P3c7 = BaseAddr + 0x17;
	P3c8 = BaseAddr + 0x18;
	P3c9 = BaseAddr + 0x19;
	P3da = BaseAddr + 0x2A;

#ifndef CONFIG_FB_SIS_LINUXBIOS
	temp = search_modeID(ROMAddr, ModeNo);

	if (temp == 0)
		return (0);

	temp = check_memory_size(ROMAddr);
	if (temp == 0)
		return (0);
#endif

#if 1
	cr30flag = (unsigned char) get_reg1(P3d4, 0x30);
	if (((cr30flag & 0x01) == 1) || ((cr30flag & 0x02) == 0)) {
#ifndef CONFIG_FB_SIS_LINUXBIOS
		get_mode_ptr(ROMAddr, ModeNo);
#endif
		set_seq_regs(ROMAddr);
		set_misc_regs(ROMAddr);
		set_crtc_regs(ROMAddr);
		set_attregs(ROMAddr);
		set_grc_regs(ROMAddr);
		ClearExt1Regs();

#ifndef CONFIG_FB_SIS_LINUXBIOS
		temp = get_rate_ptr(ROMAddr, ModeNo);
		if (temp) {
#endif
			set_sync(ROMAddr);
			set_crt1_crtc(ROMAddr);
			set_crt1_offset(ROMAddr);
			set_crt1_vclk(ROMAddr);
			set_vclk_state(ROMAddr, ModeNo);

			if ((ivideo.chip_id == SIS_Trojan) || (ivideo.chip_id == SIS_Spartan))
				set_crt1_FIFO2(ROMAddr);
			else	/* SiS 300 */
				set_crt1_FIFO(ROMAddr);
#ifndef CONFIG_FB_SIS_LINUXBIOS
		}
#endif
		set_crt1_mode_regs(ROMAddr, ModeNo);
		if ((ivideo.chip_id == SIS_Trojan) || (ivideo.chip_id == SIS_Spartan))
			set_interlace(ROMAddr, ModeNo);
		load_DAC(ROMAddr);

		/* clear OnScreen */
		memset((char *) ivideo.video_vbase, 0,
		       video_linelength * ivideo.video_height);
	}
#else
	cr30flag = (unsigned char) get_reg1(P3d4, 0x30);
	if (((cr30flag & 0x01) == 1) || ((cr30flag & 0x02) == 0)) {
		//set_seq_regs(ROMAddr);
		{
			unsigned char SRdata;
			SRdata = SRegs[0x01] | 0x20;
			set_reg1(P3c4, 0x01, SRdata);

			for (i = 02; i <= 04; i++)
				set_reg1(P3c4, i, SRegs[i]);
		}

		//set_misc_regs(ROMAddr);
		{
			set_reg3(P3c2, 0x23);
		}

		//set_crtc_regs(ROMAddr);
		{
			unsigned char CRTCdata;

			CRTCdata = (unsigned char) get_reg1(P3d4, 0x11);
			set_reg1(P3d4, 0x11, CRTCdata);

			for (i = 0; i <= 0x18; i++)
				set_reg1(P3d4, i, CRegs[i]);
		}

		//set_attregs(ROMAddr);
		{
			for (i = 0; i <= 0x13; i++) {
				get_reg2(P3da);
				set_reg3(P3c0, i);
				set_reg3(P3c0, ARegs[i]);
			}
			get_reg2(P3da);
			set_reg3(P3c0, 0x14);
			set_reg3(P3c0, 0x00);
			get_reg2(P3da);
			set_reg3(P3c0, 0x20);
		}

		//set_grc_regs(ROMAddr);
		{
			for (i = 0; i <= 0x08; i++)
				set_reg1(P3ce, i, GRegs[i]);
		}

		//ClearExt1Regs();
		{
			for (i = 0x0A; i <= 0x0E; i++)
				set_reg1(P3c4, i, 0x00);
		}

		//set_sync(ROMAddr);
		{
			set_reg3(P3c2, MReg);
		}

		//set_crt1_crtc(ROMAddr);
		{
			unsigned char data;

			data = (unsigned char) get_reg1(P3d4, 0x11);
			data = data & 0x7F;
			set_reg1(P3d4, 0x11, data);

			for (i = 0; i <= 0x07; i++)
				set_reg1(P3d4, i, CRegs[i]);
			for (i = 0x10; i <= 0x12; i++)
				set_reg1(P3d4, i, CRegs[i]);
			for (i = 0x15; i <= 0x16; i++)
				set_reg1(P3d4, i, CRegs[i]);
			for (i = 0x0A; i <= 0x0C; i++)
				set_reg1(P3c4, i, SRegs[i]);

			data = SRegs[0x0E] & 0xE0;
			set_reg1(P3c4, 0x0E, data);

			set_reg1(P3d4, 0x09, CRegs[0x09]);

		}

		//set_crt1_offset(ROMAddr);
		{
			set_reg1(P3c4, 0x0E, SRegs[0x0E]);
			set_reg1(P3c4, 0x10, SRegs[0x10]);
		}

		//set_crt1_vclk(ROMAddr);
		{
			set_reg1(P3c4, 0x31, 0);

			for (i = 0x2B; i <= 0x2C; i++)
				set_reg1(P3c4, i, SRegs[i]);
			set_reg1(P3c4, 0x2D, 0x80);
		}

		//set_vclk_state(ROMAddr, ModeNo);
		{
			set_reg1(P3c4, 0x32, SRegs[0x32]);
			set_reg1(P3c4, 0x07, SRegs[0x07]);
		}

		if ((ivideo.chip_id == SIS_Trojan)
		    || (ivideo.chip_id == SIS_Spartan)) {
			//set_crt1_FIFO2(ROMAddr);
			set_reg1(P3c4, 0x15, SRegs[0x15]);

			set_reg4(0xcf8, 0x80000050);
			set_reg4(0xcfc, 0xc5041e04);

			set_reg1(P3c4, 0x08, SRegs[0x08]);
			set_reg1(P3c4, 0x0F, SRegs[0x0F]);
			set_reg1(P3c4, 0x3b, 0x00);
			set_reg1(P3c4, 0x09, SRegs[0x09]);
		}

		//set_crt1_mode_regs(ROMAddr, ModeNo);
		{
			set_reg1(P3c4, 0x06, SRegs[0x06]);
			set_reg1(P3c4, 0x01, SRegs[0x01]);
			set_reg1(P3c4, 0x0F, SRegs[0x0F]);
			set_reg1(P3c4, 0x21, SRegs[0x21]);
		}

		if ((ivideo.chip_id == SIS_Trojan) || (ivideo.chip_id == SIS_Spartan)) {
			//set_interlace(ROMAddr, ModeNo);
			set_reg1(P3d4, 0x19, CRegs[0x19]);
			set_reg1(P3d4, 0x1A, CRegs[0x1A]);
		}
		load_DAC(ROMAddr);

		/* clear OnScreen */
		memset((char *) ivideo.video_vbase, 0,
		       video_linelength * ivideo.video_height);
	}
#endif
	cr31flag = (unsigned char) get_reg1(P3d4, 0x31);

	display_on();

	return (0);
}

static void pre_setmode(void)
{
	vgawb(CRTC_ADR, 0x30);
	vgawb(CRTC_DATA, 0x00);

	vgawb(CRTC_ADR, 0x31);
	vgawb(CRTC_DATA, 0x60);

	DPRINTK("Setting CR33 = 0x%x\n", rate_idx & 0x0f);

	/* set CRT1 refresh rate */
	vgawb(CRTC_ADR, 0x33);
	vgawb(CRTC_DATA, rate_idx & 0x0f);
}

static void post_setmode(void)
{
	u8 uTemp;

	/* turn on CRT1 */
	vgawb(CRTC_ADR, 0x17);
	uTemp = vgarb(CRTC_DATA);
	uTemp |= 0x80;
	vgawb(CRTC_DATA, uTemp);

	/* disable 24-bit palette RAM and Gamma correction */
	vgawb(SEQ_ADR, 0x07);
	uTemp = vgarb(SEQ_DATA);
	uTemp &= ~0x04;
	vgawb(SEQ_DATA, uTemp);
}

static void search_mode(const char *name)
{
	int i = 0;

	if (name == NULL)
		return;

	while (sisbios_mode[i].mode_no != 0) {
		if (!strcmp(name, sisbios_mode[i].name)) {
			mode_idx = i;
			break;
		}
		i++;
	}
	if (mode_idx < 0)
		DPRINTK("Invalid user mode : %s\n", name);
}

static u8 search_refresh_rate(unsigned int rate)
{
	u16 xres, yres;
	int i = 0;

	xres = sisbios_mode[mode_idx].xres;
	yres = sisbios_mode[mode_idx].yres;

	while ((vrate[i].idx != 0) && (vrate[i].xres <= xres)) {
		if ((vrate[i].xres == xres) && (vrate[i].yres == yres)
		    && (vrate[i].refresh == rate)) {
			rate_idx = vrate[i].idx;
			return rate_idx;
		}
		i++;
	}

	DPRINTK("sisfb: Unsupported rate %d in %dx%d mode\n", rate, xres,
		yres);

	return 0;
}



/* ------------------ Public Routines ------------------------------- */

/*
 *    Get the Fixed Part of the Display
 */

static int sisfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	DPRINTK("sisfb: sisfb_get_fix:[%d]\n", con);

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, fb_info.modename);

	fix->smem_start = ivideo.video_base;
	if(ivideo.video_size > 0x800000)
		fix->smem_len = RESERVED_MEM_SIZE_8M;	/* reserved for Xserver */
	else
		fix->smem_len = RESERVED_MEM_SIZE_4M;	/* reserved for Xserver */

	fix->type = video_type;
	fix->type_aux = 0;
	if (ivideo.video_bpp == 8)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_TRUECOLOR;
	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 0;
	fix->line_length = video_linelength;
	fix->mmio_start = ivideo.mmio_base;
	fix->mmio_len = MMIO_SIZE;
	fix->accel = FB_ACCEL_SIS_GLAMOUR;
	fix->reserved[0] = ivideo.video_size & 0xFFFF;
	fix->reserved[1] = (ivideo.video_size >> 16) & 0xFFFF;
	fix->reserved[2] = caps;	/* capabilities */

	return 0;
}

/*
 *    Get the User Defined Part of the Display
 */

static int sisfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	DPRINTK("sisfb: sisfb_get_var:[%d]\n", con);

	if (con == -1)
		memcpy(var, &default_var, sizeof(struct fb_var_screeninfo));
	else
		*var = fb_display[con].var;
	return 0;
}

/*
 *    Set the User Defined Part of the Display
 */

static int sisfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	int err;
	unsigned int cols, rows;

	fb_display[con].var.activate = FB_ACTIVATE_NOW;

	/* Set mode */
	if (do_set_var(var, con == currcon, info)) {
		crtc_to_var(var);	/* return current mode to user */
		return -EINVAL;
	}

	/* get actual setting value */
	crtc_to_var(var);

	/* update display of current console */
	sisfb_set_disp(con, var);

	if (info->changevar)
		(*info->changevar) (con);

	if ((err = fb_alloc_cmap(&fb_display[con].cmap, 0, 0)))
		return err;

	do_install_cmap(con, info);

	/* inform console to update struct display */
	cols = sisbios_mode[mode_idx].cols;
	rows = sisbios_mode[mode_idx].rows;
	vc_resize_con(rows, cols, fb_display[con].conp->vc_num);

	return 0;
}


/*
 *    Get the Colormap
 */

static int sisfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	DPRINTK("sisfb: sisfb_get_cmap:[%d]\n", con);

	if (con == currcon)
		return fb_get_cmap(cmap, kspc, sis_getcolreg, info);
	else if (fb_display[con].cmap.len)	/* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(video_cmap_len), cmap, kspc ? 0 : 2);

	return 0;
}

/*
 *    Set the Colormap
 */

static int sisfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {	/* no colormap allocated */
		err = fb_alloc_cmap(&fb_display[con].cmap, video_cmap_len, 0);
		if (err)
			return err;
	}
	if (con == currcon)	/* current console */
		return fb_set_cmap(cmap, kspc, sis_setcolreg, info);
	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	return 0;
}

static int sisfb_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg, int con,
		       struct fb_info *info)
{
	switch (cmd) {
	case FBIO_ALLOC:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		sis_malloc((struct sis_memreq *) arg);
		break;
	case FBIO_FREE:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		sis_free(*(unsigned long *) arg);
		break;
	case FBIOGET_GLYPH:
		sis_get_glyph((struct GlyInfo *) arg);
		break;
	case FBIOGET_HWCINFO:
		{
			unsigned long *hwc_offset = (unsigned long *) arg;

			if (caps | HW_CURSOR_CAP)
				*hwc_offset = hwcursor_vbase -
				    (unsigned long) ivideo.video_vbase;
			else
				*hwc_offset = 0;

			break;
		}
	default:
		return -EINVAL;
	}
	return 0;
}

static int sisfb_mmap(struct fb_info *info, struct file *file,
		      struct vm_area_struct *vma)
{
	struct fb_var_screeninfo var;
	unsigned long start;
	unsigned long off;
	u32 len;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;
	off = vma->vm_pgoff << PAGE_SHIFT;

	/* frame buffer memory */
	start = (unsigned long) ivideo.video_base;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + ivideo.video_size);

	if (off >= len) {
		/* memory mapped io */
		off -= len;
		sisfb_get_var(&var, currcon, info);
		if (var.accel_flags)
			return -EINVAL;
		start = (unsigned long) ivideo.mmio_base;
		len = PAGE_ALIGN((start & ~PAGE_MASK) + MMIO_SIZE);
	}

	start &= PAGE_MASK;
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

#if defined(__i386__)
	if (boot_cpu_data.x86 > 3)
		pgprot_val(vma->vm_page_prot) |= _PAGE_PCD;
#endif
	if (io_remap_page_range(vma->vm_start, off, vma->vm_end - vma->vm_start,
				vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static struct fb_ops sisfb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	sisfb_get_fix,
	fb_get_var:	sisfb_get_var,
	fb_set_var:	sisfb_set_var,
	fb_get_cmap:	sisfb_get_cmap,
	fb_set_cmap:	sisfb_set_cmap,
	fb_ioctl:	sisfb_ioctl,
	fb_mmap:	sisfb_mmap,
};

int sisfb_setup(char *options)
{
	char *this_opt;

	fb_info.fontname[0] = '\0';
	ivideo.refresh_rate = 0;

	if (!options || !*options)
		return 0;

	for (this_opt = strtok(options, ","); this_opt; 
	     this_opt = strtok(NULL, ",")) {
		if (!*this_opt)
			continue;

		if (!strcmp(this_opt, "inverse")) {
			inverse = 1;
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "font:", 5)) {
			strcpy(fb_info.fontname, this_opt + 5);
		} else if (!strncmp(this_opt, "mode:", 5)) {
			search_mode(this_opt + 5);
		} else if (!strncmp(this_opt, "vrate:", 6)) {
			ivideo.refresh_rate =
			    simple_strtoul(this_opt + 6, NULL, 0);
		} else if (!strncmp(this_opt, "off", 3)) {
			sisfb_off = 1;
		} else
			DPRINTK("invalid parameter %s\n", this_opt);
	}
	return 0;
}

static int sisfb_update_var(int con, struct fb_info *info)
{
	return 0;
}

/*
 *    Switch Console (called by fbcon.c)
 */

static int sisfb_switch(int con, struct fb_info *info)
{
	int cols, rows;

	DPRINTK("sisfb: switch console from [%d] to [%d]\n", currcon, con);

	/* update colormap of current console */
	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap, 1, sis_getcolreg, info);

	fb_display[con].var.activate = FB_ACTIVATE_NOW;

	/* same mode, needn't change mode actually */

	if (!memcmp(&fb_display[con].var, &fb_display[currcon].var, sizeof(struct fb_var_screeninfo))) 
	{
		currcon = con;
		return 1;
	}

	currcon = con;

	do_set_var(&fb_display[con].var, 1, info);

	sisfb_set_disp(con, &fb_display[con].var);

	/* Install new colormap */
	do_install_cmap(con, info);

	cols = sisbios_mode[mode_idx].cols;
	rows = sisbios_mode[mode_idx].rows;
	vc_resize_con(rows, cols, fb_display[con].conp->vc_num);

	sisfb_update_var(con, info);

	return 1;

}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */

static void sisfb_blank(int blank, struct fb_info *info)
{
	u8 CRData;

	vgawb(CRTC_ADR, 0x17);
	CRData = vgarb(CRTC_DATA);

	if (blank > 0)		/* turn off CRT1 */
		CRData &= 0x7f;
	else			/* turn on CRT1 */
		CRData |= 0x80;

	vgawb(CRTC_ADR, 0x17);
	vgawb(CRTC_DATA, CRData);
}

int __init sisfb_init(void)
{
	struct pci_dev *pdev = NULL;
	struct board *b;
	int pdev_valid = 0;
	unsigned char jTemp;
	u32 cmd;

	outb(0x77, 0x80);

	if (sisfb_off)
		return -ENXIO;

	pci_for_each_dev(pdev) {
		for (b = dev_list; b->vendor; b++) 
		{
			if ((b->vendor == pdev->vendor)
			    && (b->device == pdev->device)) 
			{
				pdev_valid = 1;
				strcpy(fb_info.modename, b->name);
				ivideo.chip_id = pdev->device;
				break;
			}
		}

		if (pdev_valid)
			break;
	}

	if (!pdev_valid)
		return -1;

#ifdef CONFIG_FB_SIS_LINUXBIOS
	pci_read_config_dword(pdev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_IO;
	cmd |= PCI_COMMAND_MEMORY;
	pci_write_config_dword(pdev, PCI_COMMAND, cmd); 
#endif

	ivideo.video_base = pci_resource_start(pdev, 0);
	if (!request_mem_region(ivideo.video_base, pci_resource_len(pdev, 0),
				"sisfb FB")) {
		printk(KERN_ERR "sisfb: cannot reserve frame buffer memory\n");
		return -ENODEV;
	}
	ivideo.mmio_base = pci_resource_start(pdev, 1);
	if (!request_mem_region(ivideo.mmio_base, pci_resource_len(pdev, 1),
				"sisfb MMIO")) {
		printk(KERN_ERR "sisfb: cannot reserve MMIO region\n");
		release_mem_region(pci_resource_start(pdev, 1), 
				   pci_resource_len(pdev, 1));
		return -ENODEV;
	}
	ivideo.vga_base = pci_resource_start(pdev, 2);
	if (!request_region(ivideo.vga_base, pci_resource_len(pdev, 2),
			    "sisfb IO")) {
		printk(KERN_ERR "sisfb: cannot reserve I/O ports\n");
		release_mem_region(pci_resource_start(pdev, 1),
				   pci_resource_len(pdev, 1));
		release_mem_region(pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
		return -ENODEV;
	}
	ivideo.vga_base += 0x30;

#ifndef CONFIG_FB_SIS_LINUXBIOS
	rom_base = 0x000C0000;
	request_region(rom_base, 32, "sisfb");
#else
        rom_base = 0x0;
#endif

	/* set passwd */
	vgawb(SEQ_ADR, IND_SIS_PASSWORD);
	vgawb(SEQ_DATA, SIS_PASSWORD);

	/* Enable MMIO & PCI linear address */
	vgawb(SEQ_ADR, IND_SIS_PCI_ADDRESS_SET);
	jTemp = vgarb(SEQ_DATA);
	jTemp |= SIS_PCI_ADDR_ENABLE;
	jTemp |= SIS_MEM_MAP_IO_ENABLE;
	vgawb(SEQ_DATA, jTemp);

	/* get video ram size by SR14 */
	vgawb(SEQ_ADR, IND_SIS_DRAM_SIZE);
	ivideo.video_size = ((int) ((vgarb(SEQ_DATA) & 0x3f) + 1) << 20);

	if (mode_idx < 0)
		mode_idx = DEFAULT_MODE;	/* 0:640x480x8 */

#ifdef CONFIG_FB_SIS_LINUXBIOS
	mode_idx = DEFAULT_MODE;
	rate_idx = sisbios_mode[mode_idx].rate_idx;
	/* set to default refresh rate 60MHz */
	ivideo.refresh_rate = 60;
#endif

	mode_no = sisbios_mode[mode_idx].mode_no;

	if (ivideo.refresh_rate != 0)
		search_refresh_rate(ivideo.refresh_rate);

	if (rate_idx == 0) {
		rate_idx = sisbios_mode[mode_idx].rate_idx;
		/* set to default refresh rate 60MHz */
		ivideo.refresh_rate = 60;
	}

	ivideo.video_bpp = sisbios_mode[mode_idx].bpp;
	ivideo.video_width = sisbios_mode[mode_idx].xres;
	ivideo.video_height = sisbios_mode[mode_idx].yres;
	video_linelength = ivideo.video_width * (ivideo.video_bpp >> 3);

	ivideo.video_vbase = ioremap(ivideo.video_base, ivideo.video_size);
	ivideo.mmio_vbase = ioremap(ivideo.mmio_base, MMIO_SIZE);

#ifndef CONFIG_FB_SIS_LINUXBIOS
	rom_vbase = (unsigned long) ioremap(rom_base, MAX_ROM_SCAN);
#endif

	SiSInit300(); 

	printk(KERN_INFO
	       "sisfb: framebuffer at 0x%lx, mapped to 0x%p, size %dk\n",
	       ivideo.video_base, ivideo.video_vbase,
	       ivideo.video_size / 1024);
	printk(KERN_INFO "sisfb: mode is %dx%dx%d, linelength=%d\n",
	       ivideo.video_width, ivideo.video_height, ivideo.video_bpp,
	       video_linelength);

	/* enable 2D engine */
	vgawb(SEQ_ADR, IND_SIS_MODULE_ENABLE);
	jTemp = vgarb(SEQ_DATA);
	jTemp |= SIS_2D_ENABLE;
	vgawb(SEQ_DATA, jTemp);

	pre_setmode();

	if (SiSSetMode(mode_no)) {
		DPRINTK("sisfb: set mode[0x%x]: failed\n", 0x30);
		return -1;
	}

	post_setmode();

	crtc_to_var(&default_var);

	fb_info.changevar = NULL;
	fb_info.node = -1;
	fb_info.fbops = &sisfb_ops;
	fb_info.disp = &disp;
	fb_info.switch_con = &sisfb_switch;
	fb_info.updatevar = &sisfb_update_var;
	fb_info.blank = &sisfb_blank;
	fb_info.flags = FBINFO_FLAG_DEFAULT;

	sisfb_set_disp(-1, &default_var);

	if (sisfb_heap_init()) {
		DPRINTK("sisfb: Failed to enable offscreen heap\n");
	}

	/* to avoid the inversed bgcolor bug of the initial state */
	vc_resize_con(1, 1, 0);

	if (register_framebuffer(&fb_info) < 0)
		return -EINVAL;

	ivideo.status = CRT1;

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       GET_FB_IDX(fb_info.node), fb_info.modename);

	return 0;
}

#ifdef MODULE

static char *mode = NULL;
static unsigned int rate = 0;

MODULE_PARM(mode, "s");
MODULE_PARM(rate, "i");

int init_module(void)
{
	if (mode)
		search_mode(mode);

	ivideo.refresh_rate = rate;

	sisfb_init();

	return 0;
}

void cleanup_module(void)
{
	unregister_framebuffer(&fb_info);
}
#endif				/* MODULE */


EXPORT_SYMBOL(sis_malloc);
EXPORT_SYMBOL(sis_free);

EXPORT_SYMBOL(ivideo);

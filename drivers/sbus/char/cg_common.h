/* sun_cg_common.h
 * contains the definitions of the structures that various sun
 * frame buffer can use to do console driver stuff.
 * 
 * This is not in sun_framebuffer.h because you may not want cgXX
 * support so you wont require this.
 *
 */

#define BT_D4M3(x) ((((x) >> 2) << 1) + ((x) >> 2))     /* (x / 4) * 3 */
#define BT_D4M4(x) ((x) & ~3)                           /* (x / 4) * 4 */

#define D4M3(x) ((((x)>>2)<<1) + ((x)>>2))      /* (x/4)*3 */
#define D4M4(x) ((x)&~0x3)                      /* (x/4)*4 */

struct bt_regs {
    volatile unsigned int addr;		  /* address register */
    volatile unsigned int color_map;	  /* color map */
    volatile unsigned int control;	  /* control register */
    volatile unsigned int cursor;	  /* cursor map register */
};

/* The cg3 driver, obio space addresses for mapping the cg3 stuff */
/* We put these constants here, because the cg14 driver initially will emulate a cg3 */
#define CG3_REGS 0x400000
#define CG3_RAM	 0x800000



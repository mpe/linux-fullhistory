    /*
     *  24 bpp packed pixel (cfb24)
     */

#include <linux/config.h>

#ifdef MODULE
#if defined(CONFIG_FBCON_CFB24) || defined(CONFIG_FBCON_CFB24_MODULE)
#define FBCON_HAS_CFB24
#endif
#else
#if defined(CONFIG_FBCON_CFB24)
#define FBCON_HAS_CFB24
#endif
#endif

extern struct display_switch fbcon_cfb24;
extern u32 fbcon_cfb24_cmap[16];
extern void fbcon_cfb24_setup(struct display *p);
extern void fbcon_cfb24_bmove(struct display *p, int sy, int sx, int dy,
			      int dx, int height, int width);
extern void fbcon_cfb24_clear(struct vc_data *conp, struct display *p, int sy,
			      int sx, int height, int width);
extern void fbcon_cfb24_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx);
extern void fbcon_cfb24_putcs(struct vc_data *conp, struct display *p,
			      const unsigned short *s, int count, int yy, int xx);
extern void fbcon_cfb24_revc(struct display *p, int xx, int yy);

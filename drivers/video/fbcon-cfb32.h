    /*
     *  32 bpp packed pixel (cfb32)
     */

extern struct display_switch fbcon_cfb32;
extern u32 fbcon_cfb32_cmap[16];
extern void fbcon_cfb32_setup(struct display *p);
extern void fbcon_cfb32_bmove(struct display *p, int sy, int sx, int dy,
			      int dx, int height, int width);
extern void fbcon_cfb32_clear(struct vc_data *conp, struct display *p, int sy,
			      int sx, int height, int width);
extern void fbcon_cfb32_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx);
extern void fbcon_cfb32_putcs(struct vc_data *conp, struct display *p,
			      const char *s, int count, int yy, int xx);
extern void fbcon_cfb32_revc(struct display *p, int xx, int yy);

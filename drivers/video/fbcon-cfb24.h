    /*
     *  24 bpp packed pixel (cfb24)
     */

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
			      const char *s, int count, int yy, int xx);
extern void fbcon_cfb24_revc(struct display *p, int xx, int yy);

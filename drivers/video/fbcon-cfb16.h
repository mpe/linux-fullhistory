    /*
     *  16 bpp packed pixel (cfb16)
     */

extern struct display_switch fbcon_cfb16;
extern u16 fbcon_cfb16_cmap[16];
extern void fbcon_cfb16_setup(struct display *p);
extern void fbcon_cfb16_bmove(struct display *p, int sy, int sx, int dy,
			      int dx, int height, int width);
extern void fbcon_cfb16_clear(struct vc_data *conp, struct display *p, int sy,
			      int sx, int height, int width);
extern void fbcon_cfb16_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx);
extern void fbcon_cfb16_putcs(struct vc_data *conp, struct display *p,
			      const char *s, int count, int yy, int xx);
extern void fbcon_cfb16_revc(struct display *p, int xx, int yy);

    /*
     *  2 bpp packed pixel (cfb2)
     */

extern struct display_switch fbcon_cfb2;
extern void fbcon_cfb2_setup(struct display *p);
extern void fbcon_cfb2_bmove(struct display *p, int sy, int sx, int dy, int dx,
			     int height, int width);
extern void fbcon_cfb2_clear(struct vc_data *conp, struct display *p, int sy,
			     int sx, int height, int width);
extern void fbcon_cfb2_putc(struct vc_data *conp, struct display *p, int c,
			    int yy, int xx);
extern void fbcon_cfb2_putcs(struct vc_data *conp, struct display *p,
			     const char *s, int count, int yy, int xx);
extern void fbcon_cfb2_revc(struct display *p, int xx, int yy);

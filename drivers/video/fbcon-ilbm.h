    /*
     *  Amiga interleaved bitplanes (ilbm)
     */

extern struct display_switch fbcon_ilbm;
extern void fbcon_ilbm_setup(struct display *p);
extern void fbcon_ilbm_bmove(struct display *p, int sy, int sx, int dy, int dx,
			     int height, int width);
extern void fbcon_ilbm_clear(struct vc_data *conp, struct display *p, int sy,
			     int sx, int height, int width);
extern void fbcon_ilbm_putc(struct vc_data *conp, struct display *p, int c,
			    int yy, int xx);
extern void fbcon_ilbm_putcs(struct vc_data *conp, struct display *p,
			     const unsigned short *s, int count, int yy, int xx);
extern void fbcon_ilbm_revc(struct display *p, int xx, int yy);

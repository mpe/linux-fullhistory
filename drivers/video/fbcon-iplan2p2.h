    /*
     *  Atari interleaved bitplanes (2 planes) (iplan2p2)
     */

extern struct display_switch fbcon_iplan2p2;
extern void fbcon_iplan2p2_setup(struct display *p);
extern void fbcon_iplan2p2_bmove(struct display *p, int sy, int sx, int dy,
				 int dx, int height, int width);
extern void fbcon_iplan2p2_clear(struct vc_data *conp, struct display *p,
				 int sy, int sx, int height, int width);
extern void fbcon_iplan2p2_putc(struct vc_data *conp, struct display *p, int c,
				int yy, int xx);
extern void fbcon_iplan2p2_putcs(struct vc_data *conp, struct display *p,
				 const char *s, int count, int yy, int xx);
extern void fbcon_iplan2p2_revc(struct display *p, int xx, int yy);

    /*
     *  Mac variable bpp packed pixels (mac)
     */

extern struct display_switch fbcon_mac;
extern void fbcon_mac_setup(struct display *p);
extern void fbcon_mac_bmove(struct display *p, int sy, int sx, int dy, int dx,
			    int height, int width);
extern void fbcon_mac_clear(struct vc_data *conp, struct display *p, int sy,
			    int sx, int height, int width);
extern void fbcon_mac_putc(struct vc_data *conp, struct display *p, int c,
			   int yy, int xx);
extern void fbcon_mac_putcs(struct vc_data *conp, struct display *p,
			    const char *s, int count, int yy, int xx);
extern void fbcon_mac_revc(struct display *p, int xx, int yy);

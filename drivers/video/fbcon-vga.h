    /*
     *  VGA characters/attributes
     */

extern struct display_switch fbcon_vga;
extern void fbcon_vga_setup(struct display *p);
extern void fbcon_vga_bmove(struct display *p, int sy, int sx, int dy, int dx,
			    int height, int width);
extern void fbcon_vga_clear(struct vc_data *conp, struct display *p, int sy,
			    int sx, int height, int width);
extern void fbcon_vga_putc(struct vc_data *conp, struct display *p, int c,
			   int yy, int xx);
extern void fbcon_vga_putcs(struct vc_data *conp, struct display *p,
			    const char *s, int count, int yy, int xx);
extern void fbcon_vga_revc(struct display *p, int xx, int yy);

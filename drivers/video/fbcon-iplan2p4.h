    /*
     *  Atari interleaved bitplanes (4 planes) (iplan2p4)
     */

#ifdef MODULE
#if defined(CONFIG_FBCON_IPLAN2P4) || defined(CONFIG_FBCON_IPLAN2P4_MODULE)
#define FBCON_HAS_IPLAN2P4
#endif
#else
#if defined(CONFIG_FBCON_IPLAN2P4)
#define FBCON_HAS_IPLAN2P4
#endif
#endif

extern struct display_switch fbcon_iplan2p4;
extern void fbcon_iplan2p4_setup(struct display *p);
extern void fbcon_iplan2p4_bmove(struct display *p, int sy, int sx, int dy,
				 int dx, int height, int width);
extern void fbcon_iplan2p4_clear(struct vc_data *conp, struct display *p,
				 int sy, int sx, int height, int width);
extern void fbcon_iplan2p4_putc(struct vc_data *conp, struct display *p, int c,
				int yy, int xx);
extern void fbcon_iplan2p4_putcs(struct vc_data *conp, struct display *p,
				 const unsigned short *s, int count, int yy, int xx);
extern void fbcon_iplan2p4_revc(struct display *p, int xx, int yy);

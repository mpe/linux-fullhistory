/*
 * SGI/Newport video card ioctl definitions
 *
 */

typedef struct {
        int flags;
        u16 w, h;
        u16 fields_sec;
} ng1_vof_info_t;

struct ng1_info {
	struct gfx_info gfx_info;
	u8 boardrev;
        u8 rex3rev;
        u8 vc2rev;
        u8 monitortype;
        u8 videoinstalled;
        u8 mcrev;
        u8 bitplanes;
        u8 xmap9rev;
        u8 cmaprev;
        ng1_vof_info_t ng1_vof_info;
        u8 bt445rev;
        u8 paneltype;
};

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

#define GFX_NAME_NEWPORT "NG1"

/* ioctls */
#define NG1_SET_CURSOR_HOTSPOT 21001
struct ng1_set_cursor_hotspot {
	unsigned short xhot;
        unsigned short yhot;
};

#define NG1_SETDISPLAYMODE     21006
struct ng1_setdisplaymode_args {
        int wid;
        unsigned int mode;
};

#define NG1_SETGAMMARAMP0      21007
struct ng1_setgammaramp_args {
        unsigned char red   [256];
        unsigned char green [256];
        unsigned char blue  [256];
};



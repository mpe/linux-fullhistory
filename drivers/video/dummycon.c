/*
 *  linux/drivers/video/dummycon.c -- A dummy console driver
 *
 *  To be used if there's no other console driver (e.g. for plain VGA text)
 *  available, usually until fbcon takes console over.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/vt_kern.h>
#include <linux/init.h>

/*
 *  Dummy console driver
 */

#if defined(CONFIG_ARM)
#define DUMMY_COLUMNS	ORIG_VIDEO_COLS
#define DUMMY_ROWS	ORIG_VIDEO_LINES
#else
#define DUMMY_COLUMNS	80
#define DUMMY_ROWS	25
#endif

__initfunc(static const char *dummycon_startup(void))
{
    return "dummy device";
}

static void dummycon_init(struct vc_data *conp, int init)
{
    conp->vc_can_do_color = 1;
    if (init) {
	conp->vc_cols = DUMMY_COLUMNS;
	conp->vc_rows = DUMMY_ROWS;
    } else
	vc_resize_con(DUMMY_ROWS, DUMMY_COLUMNS, conp->vc_num);
}

static int dummycon_dummy(void)
{
    return 0;
}

/*
 *  The console `switch' structure for the dummy console
 *
 *  Most of the operations are dummies.
 */

struct consw dummy_con = {
    dummycon_startup, dummycon_init,
    (void *)dummycon_dummy,	/* con_deinit */
    (void *)dummycon_dummy,	/* con_clear */
    (void *)dummycon_dummy,	/* con_putc */
    (void *)dummycon_dummy,	/* con_putcs */
    (void *)dummycon_dummy,	/* con_cursor */
    (void *)dummycon_dummy,	/* con_scroll */
    (void *)dummycon_dummy,	/* con_bmove */
    (void *)dummycon_dummy,	/* con_switch */
    (void *)dummycon_dummy,	/* con_blank */
    (void *)dummycon_dummy,	/* con_font_op */
    (void *)dummycon_dummy,	/* con_set_palette */
    (void *)dummycon_dummy,	/* con_scrolldelta */
    NULL,			/* con_set_origin */
    NULL,			/* con_save_screen */
};

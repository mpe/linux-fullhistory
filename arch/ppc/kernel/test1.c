
#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>

#include <asm/pgtable.h>

/*pgd_t *swapper_pg_dir;*/



BAT BAT0 =
   {
   	{
   		0x80000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		1,			/* vs */
   		1,			/* vp */
   	},
   	{
   		0x80000000>>17,		/* brpn */
   		1,			/* w */
   		1,			/* i (cache disabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };
BAT BAT1 =
   {
   	{
   		0xC0000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		1,			/* vs */
   		1,			/* vp */
   	},
   	{
   		0xC0000000>>17,		/* brpn */
   		1,			/* w */
   		1,			/* i (cache disabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };
BAT BAT2 =
   {
   	{
   		0x00000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		0,			/* vs */
   		0,			/* vp */
   	},
   	{
   		0x00000000>>17,		/* brpn */
   		1,			/* w */
   		1,			/* i (cache disabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };
BAT BAT3 =
   {
   	{
   		0x00000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		0,			/* vs */
   		0,			/* vp */
   	},
   	{
   		0x00000000>>17,		/* brpn */
   		1,			/* w */
   		1,			/* i (cache disabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };


BAT TMP_BAT2 =
   { /* 0x9XXXXXXX -> 0x0XXXXXXX */
   	{
   		0x90000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		1,			/* vs */
   		1,			/* vp */
   	},
   	{
   		0x00000000>>17,		/* brpn */
   		1,			/* w */
   		0,			/* i (cache enabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };



BAT ZERO_BAT =
   { /* 0x0XXXXXXX -> 0x0XXXXXXX */
   	{
   		0x00000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		1,			/* vs */
   		1,			/* vp */
   	},
   	{
   		0x00000000>>17,		/* brpn */
   		1,			/* w */
   		0,			/* i (cache enabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };


BAT OFF_BAT =
   { /* 0x0XXXXXXX -> 0x0XXXXXXX */
   	{
   		0x00000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		0,			/* vs */
   		0,			/* vp */
   	},
   	{
   		0x00000000>>17,		/* brpn */
   		1,			/* w */
   		0,			/* i (cache enabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };

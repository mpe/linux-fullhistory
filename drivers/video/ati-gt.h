/* the usage for the following structs vary from the gx and vt:
and sdram and sgram gt's
	pll registers (sdram) 6,7,11;
	crtc_h_sync_strt_wid[3];
	dsp1[3] (sdram,sgram,unused)
	dsp2[3] (offset regbase+24, depends on colour mode);
	crtc_h_tot_disp,crtc_v_tot_disp,crtc_v_sync_strt_wid,unused;
	pll registers (sgram) 7,11;
*/

/* Register values for 1280x1024, 75Hz mode (20). no 16/32 */
static struct aty_regvals aty_gt_reg_init_20 = {
	{ 0x41, 0xf9, 0x04 },
	{ 0xe02a7, 0x1401a6, 0 },
	{ 0x260957, 0x2806d6, 0 },
	{ 0x10006b6, 0x20006b6, 0x30006b6 },

	0x9f00d2, 0x03ff0429, 0x30400, 0,
	{ 0xb5, 0x04 }
};

#if 0
/* Register values for 1280x960, 75Hz mode (19) */
static struct aty_regvals aty_gt_reg_init_19 = {
};
#endif

/* Register values for 1152x870, 75Hz mode (18) */
static struct aty_regvals aty_gt_reg_init_18 = {
	{ 0x41, 0xe6, 0x04 },
	{ 0x300295, 0x300194, 0x300593 },
	{ 0x260a1c, 0x380561, 0},
	{ 0x1000744, 0x2000744, 0x3000744 },

	0x8f00b5, 0x3650392, 0x230368, 0,
	{ 0xe6, 0x04 }
};

/* Register values for 1024x768, 75Hz mode (17), 32 bpp untested */
static struct aty_regvals aty_gt_reg_init_17 = {
	{ 0x41, 0xb5, 0x04 },
	{ 0xc0283, 0xc0182, 0xc0581 },
	{ 0x36066d, 0x3806d6, 0},
	{ 0xa0049e, 0x100049e, 0x200049e },

	0x7f00a3, 0x2ff031f, 0x30300, 0,
	{ 0xb8, 0x04 }
};

#if 0
/* Register values for x, Hz mode (16) */
static struct aty_regvals aty_gt_reg_init_16 = {
};
#endif

/* Register values for 1024x768, 70Hz mode (15) */
static struct aty_regvals aty_gt_reg_init_15 = {
	{ 0x41, 0xad, 0x04 },
	{ 0x310284, 0x310183, 0x310582 },
	{ 0x0, 0x380727 },
	{ 0x0 },
	0x7f00a5, 0x2ff0325, 0x260302,
};    

/* Register values for 1024x768, 60Hz mode (14) */
static struct aty_regvals aty_gt_reg_init_14 = {
	{ 0x40, 0xe1, 0x14 },
	{ 0x310284, 0x310183, 0x310582 },
	{ 0x3607c0, 0x380840, 0x0 },
	{ 0xa80592, 0x1000592, 0x0 },

	0x7f00a7, 0x2ff0325, 0x260302, 0,
	{ 0xe1, 0x14 }
};  

/* Register values for 832x624, 75Hz mode (13) */
static struct aty_regvals aty_gt_reg_init_13 = {
	{ 0x40, 0xc6, 0x14 },
	{ 0x28026d, 0x28016c, 0x28056b },
	{ 0x3608cf, 0x380960, 0 },
	{ 0xb00655, 0x1000655, 0x2000655 },

	0x67008f, 0x26f029a, 0x230270, 0,
	{ 0xc6, 0x14 }
};

/* Register values for 800x600, 75Hz mode (12) */
static struct aty_regvals aty_gt_reg_init_12 = {
	{ 0x42, 0xe4, 0x04 },
	{ 0xa0267, 0xa0166, 0x0a0565},
	{ 0x360a33, 0x48056d, 0},
	{ 0xc00755, 0x1000755, 0x02000755},

	0x630083, 0x2570270, 0x30258, 0,
	{ 0xe4, 0x4 }
};

/* Register values for 800x600, 72Hz mode (11) */
static struct aty_regvals aty_gt_reg_init_11 = {
	{ 0x42, 0xe6, 0x04 },
	{ 0xf026c, 0xf016b, 0xf056a },
	{ 0x360a1d, 0x480561, 0},
	{ 0xc00745, 0x1000745, 0x2000745 },
	
	0x630081, 0x02570299, 0x6027c
};

/* Register values for 800x600, 60Hz mode (10) */
static struct aty_regvals aty_gt_reg_init_10 = {
	{ 0x42, 0xb8, 0x04 },
	{ 0x10026a, 0x100169, 0x100568 },
	{ 0x460652, 0x4806ba, 0},
	{ 0x68048b, 0xa0048b, 0x100048b },

	0x630083, 0x02570273, 0x40258, 0,
	{ 0xb8, 0x4 }
};

/* Register values for 800x600, 56Hz mode (9) */
static struct aty_regvals aty_gt_reg_init_9 = {
	{ 0x42, 0xf9, 0x14 },
	{ 0x90268, 0x90167, 0x090566 },
	{ 0x460701, 0x480774, 0},
	{ 0x700509, 0xa80509, 0x1000509 },

	0x63007f, 0x2570270, 0x20258
};

#if 0
/* Register values for 768x576, 50Hz mode (8) */
static struct aty_regvals aty_gt_reg_init_8 = {
};

/* Register values for 640x870, 75Hz Full Page Display (7) */
static struct aty_regvals aty_gt_reg_init_7 = {
};
#endif

/* Register values for 640x480, 67Hz mode (6) */
static struct aty_regvals aty_gt_reg_init_6 = {
	{ 0x42, 0xd1, 0x14 },
	{ 0x280259, 0x280158, 0x280557 },
	{ 0x460858, 0x4808e2, 0},
	{ 0x780600, 0xb00600, 0x1000600 },

	0x4f006b, 0x1df020c, 0x2301e2, 0,
	{ 0x8b, 0x4 }
};

/* Register values for 640x480, 60Hz mode (5) */
static struct aty_regvals aty_gt_reg_init_5 = {
	{ 0x43, 0xe8, 0x04 },
	{ 0x2c0253, 0x2c0152, 0x2c0551 },
	{ 0x460a06, 0x580555, 0},
	{ 0x880734, 0xc00734, 0x1000734 },

	0x4f0063, 0x1df020c, 0x2201e9, 0,
	{ 0xe8, 0x04 }
};

#if 0
/* Register values for x, Hz mode (4) */
static struct aty_regvals aty_gt_reg_init_4 = {
};

/* Register values for x, Hz mode (3) */
static struct aty_regvals aty_gt_reg_init_3 = {
};

/* Register values for x, Hz mode (2) */
static struct aty_regvals aty_gt_reg_init_2 = {
};

/* Register values for x, Hz mode (1) */
static struct aty_regvals aty_gt_reg_init_1 = {
};
#endif

/* yikes, more data structures (dsp2)
 * XXX kludge for sgram
 */
static int sgram_dsp[20][3] = {
	{0,0,0},
	{0,0,0},
	{0,0,0},
	{0,0,0},
	{0x5203d7,0x7803d9,0xb803dd}, //5
	{0x940666,0xe0066a,0x1700672}, //6
	{0,0,0},
	{0,0,0},
	{0x88055f,0xd80563,0x170056b}, //9
	{0x8404d9,0xb804dd,0x17004e5}, //10
	{0x7803e2,0xb803e6,0x17003ee}, //11
	{0x7803eb,0xb803ef,0x17003f7}, //12
	{0xe806c5,0x17006cd,0x2e006dd}, //13
	{0xe005f6,0x17005fe,0x2e0060e}, //14
	{0xd8052c,0x1700534,0x2e00544}, //15
	{0,0,0},
	{0xb804f2,0x17004e5,0x2e0050a}, //17
	{0xb803e6,0x17003ee,0x2e003fe}, //18
	{0,0,0},
	{0,0,0},
};

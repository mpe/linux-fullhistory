/* Register values for 1280x1024, 75Hz (WAS 60) mode (20) */
static struct aty_regvals aty_gx_reg_init_20 = {
   { 0x200, 0x200, 0x200 },

    { 0x1200a5, 0x1200a3, 0x1200a3 },
    { 0x30c0200, 0x30e0300, 0x30e0300 },
    { 0x2, 0x3, 0x3 },

    0x9f00d2, 0x3ff0429, 0x30400, 0x28100040,
    { 0xd4, 0x9 }
};     

/* Register values for 1152x870, 75Hz mode (18) */
static struct aty_regvals aty_gx_reg_init_18 = {
    { 0x200, 0x200, 0x200 },

    { 0x300097, 0x300095, 0x300094 },
    { 0x3090200, 0x30e0300, 0x30e0600 },
    { 0x2, 0x3, 0x6 },

    0x8f00b5, 0x3650392, 0x230368, 0x24100040,
    { 0x53, 0x3 }
};

/* Register values for 1024x768, 75Hz mode (17) */
static struct aty_regvals aty_gx_reg_init_17 = {
    { 0x200, 0x200, 0x200 },

    { 0x2c0087, 0x2c0085, 0x2c0084 },
    { 0x3070200, 0x30e0300, 0x30e0600 },
    { 0x2, 0x3, 0x6 },

    0x7f00a5, 0x2ff0323, 0x230302, 0x20100000,
    { 0x42, 0x3 }
};

/* Register values for 1024x768, 72Hz mode (15) */
static struct aty_regvals aty_gx_reg_init_15 = {
    { 0, 0, 0 },

    { 0x310086, 0x310084, 0x310084 },
    { 0x3070200, 0x30e0300, 0x30e0300 },
    { 0x2002312, 0x3002312, 0x3002312 },

    0x7f00a5, 0x2ff0325, 0x260302, 0x20100000,
    { 0x88, 0x7 }
};    

/* Register values for 1024x768, 60Hz mode (14) */
static struct aty_regvals aty_gx_reg_init_14 = {
	{ 0, 0, 0 },

	{ 0x310086, 0x310084, 0x310084 },
	{ 0x3060200, 0x30d0300, 0x30d0300 },
	{ 0x2002312, 0x3002312, 0x3002312 },

	0x7f00a7, 0x2ff0325, 0x260302, 0x20100000,
	{ 0x6c, 0x6 }
};  

/* Register values for 832x624, 75Hz mode (13) */
static struct aty_regvals aty_gx_reg_init_13 = {
    { 0x200, 0x200, 0x200 },

    { 0x28006f, 0x28006d, 0x28006c },
    { 0x3050200, 0x30b0300, 0x30e0600 },
    { 0x2, 0x3, 0x6 },

    0x67008f, 0x26f029a, 0x230270, 0x1a100040,
    { 0x4f, 0x5 }
};

#if 0		/* not filled in yet */
/* Register values for 800x600, 75Hz mode (12) */
static struct aty_regvals aty_gx_reg_init_12 = {
	{ 0x10, 0x28, 0x50 },
	{ },
	{ }	/* pixel clock = 49.11MHz for V=74.40Hz */
};

/* Register values for 800x600, 72Hz mode (11) */
static struct aty_regvals aty_gx_reg_init_11 = {
	{ 0x10, 0x28, 0x50 },
	{ },
	{ }	/* pixel clock = 49.63MHz for V=71.66Hz */
};

/* Register values for 800x600, 60Hz mode (10) */
static struct aty_regvals aty_gx_reg_init_10 = {
	{ 0x10, 0x28, 0x50 },
	{ },
	{ }	/* pixel clock = 41.41MHz for V=59.78Hz */
};

/* Register values for 640x870, 75Hz Full Page Display (7) */
static struct aty_regvals aty_gx_reg_init_7 = {
	{ 0x10, 0x30, 0x68 },
	{ },
	{ }	/* pixel clock = 57.29MHz for V=75.01Hz */
};
#endif

/* Register values for 640x480, 67Hz mode (6) */
static struct aty_regvals aty_gx_reg_init_6 = {
	{ 0x200, 0x200, 0x200 },

	{ 0x28005b, 0x280059, 0x280058 },
	{ 0x3040200, 0x3060300, 0x30c0600 },
	{ 0x2002312, 0x3002312, 0x6002312 },

	0x4f006b, 0x1df020c, 0x2301e2, 0x14100040,
        { 0x35, 0x07 }
};

#if 0		/* not filled in yet */
/* Register values for 640x480, 60Hz mode (5) */
static struct aty_regvals aty_gx_reg_init_5 = {
	{ 0x200, 0x200, 0x200 },
	{ },
        { 0x35, 0x07 }
};
#endif

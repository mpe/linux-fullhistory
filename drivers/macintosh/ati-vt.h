#if 0		/* not filled inaty_vt_reg_init yet */
/* Register values for 640x870, 75Hz Full Page Display (7) */
static struct aty_regvals aty_vt_reg_init_7 = {
	{ 0x10, 0x30, 0x68 },
	{ },
	{ }	/* pixel clock = 57.29MHz for V=75.01Hz */
};

/* Register values for 1024x768, 72Hz mode (15) */
static struct aty_regvals aty_vt_reg_init_15 = {
	{ 0x10, 0x28, 0x50 },
	{ },
	{ }	/* pixel clock = 78.12MHz for V=72.12Hz */
};

#endif

/* Register values for 1280x1024, 60Hz mode (20) */
static struct aty_regvals aty_vt_reg_init_20 = {
	{ 0, 0, 0 },
	{ 0x2e02a7, 0x2e02a7, 0x2e02a7},
	{ 0x3070200, 0x3070200, 0x3070200},
	{ 0xa00cb22, 0xb00cb23, 0xe00cb24 },
	
	0x9f00d2, 0x3ff0429, 0x30400, 0x28000000,
	{ 0x00, 0xaa }
};

/* Register values for 1152x870, 75Hz mode (18) */
static struct aty_regvals aty_vt_reg_init_18 = {
	{ 0, 0, 0 },
	{ 0x300295, 0x300194, 0x300194 },
	{ 0x3060200, 0x30e0300, 0x30e0300 },
	{ 0xa00cb21, 0xb00cb22, 0xe00cb23 },
    
    0x8f00b5, 0x3650392, 0x230368, 0x24000000,
    { 0x00, 0x9d }
	/* pixel clock = 100.33MHz for V=75.31Hz */
};

/* Register values for 1024x768, 75Hz mode (17) */
static struct aty_regvals aty_vt_reg_init_17 = {
    { 0, 0, 0 },

    { 0x2c0283, 0x2c0182, 0x2c0182 },
    { 0x3060200, 0x30a0300, 0x30a0300 },
    { 0xa00cb21, 0xb00cb22, 0xe00cb23 },
    
    0x7f00a3, 0x2ff031f, 0x30300, 0x20000000,
    { 0x01, 0xf7 }
};

/* Register values for 1024x768, 72Hz mode (15) */
static struct aty_regvals aty_vt_reg_init_15 = {
    { 0, 0, 0 },

    { 0x310284, 0x310183, 0x310183 },
    { 0x3060200, 0x3090300, 0x3090600 },
    { 0xa00cb21, 0xb00cb22, 0xe00cb23 },
    
    0x7f00a5, 0x2ff0325, 0x260302, 0x20000000,
    { 0x01, 0xeb }
};    

/* Register values for 1024x768, 60Hz mode (14) */
static struct aty_regvals aty_vt_reg_init_14 = {
	{ 0, 0, 0 },
    { 0x310284, 0x310183, 0x310183 },
    { 0x3060200, 0x3080300, 0x30f0600 },
    { 0xa00cb21, 0xb00cb22, 0xe00cb23 },
    
    0x7f00a7, 0x2ff0325, 0x260302, 0x20000000,
    { 0x01, 0xcc }
};  

/* Register values for 832x624, 75Hz mode (13) */
static struct aty_regvals aty_vt_reg_init_13 = {
	{ 0, 0, 0 },

    { 0x28026d, 0x28016c, 0x28016c },
    { 0x3060200, 0x3080300, 0x30f0600 },
    { 0xa00cb21, 0xb00cb21, 0xe00cb22 },
    
    0x67008f, 0x26f029a, 0x230270, 0x1a000000,
    { 0x01, 0xb4 }
};

/* Register values for 800x600, 75Hz mode (12) */
static struct aty_regvals aty_vt_reg_init_12 = {
	{ 0, 0, 0 },
    { 0x2a0267, 0x2a0166, 0x2a0565 },
    { 0x3040200, 0x3060300, 0x30d0600 },
    { 0xa00cb21, 0xb00cb21, 0xe00cb22 },
    
    0x630083, 0x2570270, 0x30258, 0x19000000,
    { 0x01, 0x9c }
	/* pixel clock = 49.11MHz for V=74.40Hz */
};

/* Register values for 800x600, 72Hz mode (11) */
static struct aty_regvals aty_vt_reg_init_11 = {
	{ 0, 0, 0 },
    
    { 0x2f026c, 0x2f016b, 0x2f056a },
    { 0x3040200, 0x3060300, 0x30d0600 },
    { 0xa00cb21, 0xb00cb21, 0xe00cb22 },
    
    0x630081, 0x257029b, 0x6027c, 0x19000000,
    { 0x01, 0x9d }
	/* pixel clock = 49.63MHz for V=71.66Hz */
};

/* Register values for 800x600, 60Hz mode (10) */
static struct aty_regvals aty_vt_reg_init_10 = {
	{ 0, 0, 0 },
    { 0x30026a, 0x300169, 0x300568 },
    { 0x3030200, 0x3050300, 0x30b0600 },
    { 0xa00cb21, 0xb00cb21, 0xe00cb22 },
    
    0x630083, 0x2570273, 0x40258, 0x19000000,
    { 0x02, 0xfb }
/* pixel clock = 41.41MHz for V=59.78Hz */
};

/* Register values for 640x480, 67Hz mode (6) */
static struct aty_regvals aty_vt_reg_init_6 = {
	{ 0, 0, 0 },

    { 0x280259, 0x280158, 0x280557 },
    { 0x3030200, 0x3040300, 0x3080600 },
    { 0xa00cb21, 0xb00cb21, 0xe00cb22 },
    
    0x4f006b, 0x1df020c, 0x2301e2, 0x14000000,
    { 0x02, 0xbe }
};

/* Register values for 640x480, 60Hz mode (5) */
static struct aty_regvals aty_vt_reg_init_5 = {
	{ 0, 0, 0 },
	
    { 0x2c0253, 0x2c0152, 0x2c0551 },
    { 0x3030200, 0x3040300, 0x3060600 },
    { 0xa00cb21, 0xb00cb21, 0xe00cb22 },
    
    0x4f0063, 0x1df020c, 0x2201e9, 0x14000000,
    { 0x02, 0x9e }
};

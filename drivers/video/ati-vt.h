/* Register values for 1280x1024, 60Hz mode (20) */
static struct aty_regvals aty_vt_reg_init_20 = {
  { 0, 0, 0 },
     
  { 0x002e02a7, 0x002e02a7, 0 },
  { 0x03070200, 0x03070200, 0 },
  { 0x0a00cb22, 0x0b00cb23, 0 },
     
    0x009f00d2, 0x03ff0429, 0x00030400, 0x28000000,
  { 0x00, 0xaa }
};

/* Register values for 1280x960, 75Hz mode (19) */
static struct aty_regvals aty_vt_reg_init_19 = {
  { 0, 0, 0 },
  { 0x003202a3, 0x003201a2, 0 },
  { 0x030b0200, 0x030b0300, 0 },
  { 0x0a00cb22, 0x0b00cb23, 0 },

    0x009f00d1, 0x03bf03e7, 0x000303c0, 0x28000000,
  { 0x00, 0xc6 }
};

/* Register values for 1152x870, 75Hz mode (18) */
static struct aty_regvals aty_vt_reg_init_18 = {
  { 0, 0, 0 },
     
  { 0x00300295, 0x00300194, 0 },
  { 0x03080200, 0x03080300, 0 },
  { 0x0a00cb21, 0x0b00cb22, 0 },
     
    0x008f00b5, 0x03650392, 0x00230368, 0x24000000,
  { 0x00, 0x9d }
};

/* Register values for 1024x768, 75Hz mode (17) */
static struct aty_regvals aty_vt_reg_init_17 = {
  { 0, 0, 0 },

  { 0x002c0283, 0x002c0182, 0 },
  { 0x03080200, 0x03080300, 0 },
  { 0x0a00cb21, 0x0b00cb22, 0 },
     
    0x007f00a3, 0x02ff031f, 0x00030300, 0x20000000,
  { 0x01, 0xf7 }
};

/* Register values for 1024x768, 70Hz mode (15) */
static struct aty_regvals aty_vt_reg_init_15 = {
  { 0, 0, 0 },
  { 0x00310284, 0x00310183, 0 },
  { 0x03080200, 0x03080300, 0 },
  { 0x0a00cb21, 0x0b00cb22, 0 },

    0x007f00a5, 0x02ff0325, 0x00260302, 0x20000000,
  { 0x01, 0xeb }
};    

/* Register values for 1024x768, 60Hz mode (14) */
static struct aty_regvals aty_vt_reg_init_14 = {
  { 0, 0, 0 },
     
  { 0x00310284, 0x00310183, 0x00310582 }, /* 32 bit 0x00310582 */
  { 0x03080200, 0x03080300, 0x03070600 }, /* 32 bit 0x03070600 */
  { 0x0a00cb21, 0x0b00cb22, 0x0e00cb23 },
    
    0x007f00a7, 0x02ff0325, 0x00260302, 0x20000000,
  { 0x01, 0xcc }
};  

/* Register values for 832x624, 75Hz mode (13) */
static struct aty_regvals aty_vt_reg_init_13 = {
  { 0, 0, 0 },

  { 0x0028026d, 0x0028016c, 0x0028056b },
  { 0x03080200, 0x03070300, 0x03090600 },
  { 0x0a00cb21, 0x0b00cb21, 0x0e00cb22 },

    0x0067008f, 0x026f029a, 0x00230270, 0x1a000000,
  { 0x01, 0xb4 }
};

/* Register values for 800x600, 75Hz mode (12) */
static struct aty_regvals aty_vt_reg_init_12 = {
  { 0, 0, 0 },
     
  { 0x002a0267, 0x002a0166, 0x002a0565 },
  { 0x03040200, 0x03060300, 0x03070600 },
  { 0x0a00cb21, 0x0b00cb21, 0x0e00cb22 },
     
    0x00630083, 0x02570270, 0x00030258, 0x19000000,
  { 0x01, 0x9c }
};

/* Register values for 800x600, 72Hz mode (11) */
static struct aty_regvals aty_vt_reg_init_11 = {
  { 0, 0, 0 },
     
  { 0x002f026c, 0x002f016b, 0x002f056a },
  { 0x03050200, 0x03070300, 0x03090600 },
  { 0x0a00cb21, 0x0b00cb21, 0x0e00cb22 },

    0x00630081, 0x02570299, 0x0006027c, 0x19000000,
  { 0x01, 0x9d }
};

/* Register values for 800x600, 60Hz mode (10) */
static struct aty_regvals aty_vt_reg_init_10 = {
  { 0, 0, 0 },
     
  { 0x0030026a, 0x00300169, 0x00300568 },
  { 0x03050200, 0x03070300, 0x03090600 },
  { 0x0a00cb21, 0x0b00cb21, 0x0e00cb22 },
     
    0x00630083, 0x02570273, 0x00040258, 0x19000000,
  { 0x02, 0xfb }
};

/* Register values for 640x480, 67Hz mode (6) */
static struct aty_regvals aty_vt_reg_init_6 = {
  { 0, 0, 0 },
      
  { 0x00280259, 0x00280158, 0x00280557 },
  { 0x03050200, 0x03070300, 0x030a0600 },
  { 0x0a00cb21, 0x0b00cb21, 0x0e00cb22 },
      
    0x004f006b, 0x01df020c, 0x002301e2, 0x14000000,
  { 0x02, 0xbe }
};

/* Register values for 640x480, 60Hz mode (5) */
static struct aty_regvals aty_vt_reg_init_5 = {
  { 0, 0, 0 },
      
  { 0x002c0253, 0x002c0152, 0x002c0551 },
  { 0x03050200, 0x03070300, 0x03090600 },
  { 0x0a00cb21, 0x0b00cb21, 0x0e00cb22 },

    0x004f0063, 0x01df020c, 0x002201e9, 0x14000000,
  { 0x02, 0x9e }
};
                               /*     8 bit       15 bit      32 bit   */
static int vt_mem_cntl[3][3] = { { 0x0A00CB21, 0x0B00CB21, 0x0E00CB21 },  /* 1 MB VRAM */
                                 { 0x0A00CB22, 0x0B00CB22, 0x0E00CB22 },  /* 2 MB VRAM */
                                { 0x0200053B, 0x0300053B, 0x0600053B }   /* 4 M B VRAM */
 };


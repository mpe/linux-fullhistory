#ifndef _LINUX_SISFB
#define _LINUX_SISFB

struct sis_memreq
{
    unsigned long offset;
    unsigned long size;
};

struct video_info
{
    /* card parameters */
    int    chip_id;
    int    video_size;
    unsigned long video_base;
    char  *video_vbase;
    unsigned long mmio_base;
    char  *mmio_vbase; 
    unsigned long vga_base;

    /* mode */
    int    video_bpp;
    int    video_width;
    int    video_height;
    unsigned int  refresh_rate;
    u8     status;
};

#ifdef __KERNEL__
extern struct video_info ivideo;

extern void sis_malloc(struct sis_memreq *req);
extern void sis_free(unsigned long base);
#endif
#endif

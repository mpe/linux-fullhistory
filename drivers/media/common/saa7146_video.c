#include <media/saa7146_vv.h>

static int memory = 32;

MODULE_PARM(memory,"i");
MODULE_PARM_DESC(memory, "maximum memory usage for capture buffers (default: 32Mb)");

/* format descriptions for capture and preview */
static struct saa7146_format formats[] = {
	{
		.name 		= "RGB-8 (3-3-2)",
		.pixelformat	= V4L2_PIX_FMT_RGB332,
		.trans 		= RGB08_COMPOSED,
		.depth		= 8,
	}, {
		.name 		= "RGB-16 (5/B-6/G-5/R)", 	/* really? */
		.pixelformat	= V4L2_PIX_FMT_RGB565,
		.trans 		= RGB16_COMPOSED,
		.depth		= 16,
	}, {
		.name 		= "RGB-24 (B-G-R)",
		.pixelformat	= V4L2_PIX_FMT_BGR24,
		.trans 		= RGB24_COMPOSED,
		.depth		= 24,
	}, {
		.name 		= "RGB-32 (B-G-R)",
		.pixelformat	= V4L2_PIX_FMT_BGR32,
		.trans 		= RGB32_COMPOSED,
		.depth		= 32,
	}, {
		.name 		= "Greyscale-8",
		.pixelformat	= V4L2_PIX_FMT_GREY,
		.trans 		= Y8,
		.depth		= 8,
	}, {
		.name 		= "YUV 4:2:2 planar (Y-Cb-Cr)",
		.pixelformat	= V4L2_PIX_FMT_YUV422P,
		.trans 		= YUV422_DECOMPOSED,
		.depth		= 16,
		.swap		= 1,
	}, {
		.name 		= "YVU 4:2:0 planar (Y-Cb-Cr)",
		.pixelformat	= V4L2_PIX_FMT_YVU420,
		.trans 		= YUV420_DECOMPOSED,
		.depth		= 12,
		.swap		= 1,
	}, {
		.name 		= "YUV 4:2:0 planar (Y-Cb-Cr)",
		.pixelformat	= V4L2_PIX_FMT_YUV420,
		.trans 		= YUV420_DECOMPOSED,
		.depth		= 12,
	}, {
		.name 		= "YUV 4:2:2 (U-Y-V-Y)",
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.trans 		= YUV422_COMPOSED,
		.depth		= 16,
	}
};

/* unfortunately, the saa7146 contains a bug which prevents it from doing on-the-fly byte swaps.
   due to this, it's impossible to provide additional *packed* formats, which are simply byte swapped
   (like V4L2_PIX_FMT_YUYV) ... 8-( */
   
static int NUM_FORMATS = sizeof(formats)/sizeof(struct saa7146_format);

struct saa7146_format* format_by_fourcc(struct saa7146_dev *dev, int fourcc)
{
	int i, j = NUM_FORMATS;
	
	for (i = 0; i < j; i++) {
		if (formats[i].pixelformat == fourcc) {
			return formats+i;
		}
	}
	
	DEB_D(("unknown pixelformat:'%4.4s'\n",(char *)&fourcc));
	return NULL;
}

static int g_fmt(struct saa7146_fh *fh, struct v4l2_format *f)
{
	struct saa7146_dev *dev = fh->dev;
	DEB_EE(("dev:%p, fh:%p\n",dev,fh));

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		f->fmt.pix = fh->video_fmt;
		return 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		f->fmt.win = fh->ov.win;
		return 0;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	{
		f->fmt.vbi = fh->vbi_fmt;
		return 0;
	}
	default:
		DEB_D(("invalid format type '%d'.\n",f->type));
		return -EINVAL;
	}
}

static int try_win(struct saa7146_dev *dev, struct v4l2_window *win)
{
	struct saa7146_vv *vv = dev->vv_data;
	enum v4l2_field field;
	int maxw, maxh;

	DEB_EE(("dev:%p\n",dev));

	if (NULL == vv->ov_fb.base) {
		DEB_D(("no fb base set.\n"));
		return -EINVAL;
	}
	if (NULL == vv->ov_fmt) {
		DEB_D(("no fb fmt set.\n"));
		return -EINVAL;
	}
	if (win->w.width < 48 || win->w.height <  32) {
		DEB_D(("min width/height. (%d,%d)\n",win->w.width,win->w.height));
		return -EINVAL;
	}
	if (win->clipcount > 16) {
		DEB_D(("clipcount too big.\n"));
		return -EINVAL;
	}

	field = win->field;
	maxw  = vv->standard->h_max_out;
	maxh  = vv->standard->v_max_out;

	if (V4L2_FIELD_ANY == field) {
                field = (win->w.height > maxh/2)
                        ? V4L2_FIELD_INTERLACED
                        : V4L2_FIELD_TOP;
	        }
        switch (field) {
        case V4L2_FIELD_TOP:
        case V4L2_FIELD_BOTTOM:
        case V4L2_FIELD_ALTERNATE:
                maxh = maxh / 2;
                break;
        case V4L2_FIELD_INTERLACED:
                break;
        default: {
		DEB_D(("no known field mode '%d'.\n",field));
                return -EINVAL;
	}
        }

	win->field = field;
	if (win->w.width > maxw)
		win->w.width = maxw;
	if (win->w.height > maxh)
		win->w.height = maxh;

	return 0;
}

static int try_fmt(struct saa7146_fh *fh, struct v4l2_format *f)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	int err;
	
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	{
		struct saa7146_format *fmt;
		enum v4l2_field field;
		int maxw, maxh;
		int calc_bpl;

		DEB_EE(("V4L2_BUF_TYPE_VIDEO_CAPTURE: dev:%p, fh:%p\n",dev,fh));

		fmt = format_by_fourcc(dev,f->fmt.pix.pixelformat);
		if (NULL == fmt) {
			return -EINVAL;
		}

		field = f->fmt.pix.field;
		maxw  = vv->standard->h_max_out;
		maxh  = vv->standard->v_max_out;
		
		if (V4L2_FIELD_ANY == field) {
			field = (f->fmt.pix.height > maxh/2)
				? V4L2_FIELD_INTERLACED
				: V4L2_FIELD_BOTTOM;
		}
		switch (field) {
		case V4L2_FIELD_ALTERNATE: {
			vv->last_field = V4L2_FIELD_TOP;
			maxh = maxh / 2;
			break;
		}
		case V4L2_FIELD_TOP:
		case V4L2_FIELD_BOTTOM:
			vv->last_field = V4L2_FIELD_INTERLACED;
			maxh = maxh / 2;
			break;
		case V4L2_FIELD_INTERLACED:
			vv->last_field = V4L2_FIELD_INTERLACED;
			break;
		default: {
			DEB_D(("no known field mode '%d'.\n",field));
			return -EINVAL;
		}
		}

		f->fmt.pix.field = field;
		if (f->fmt.pix.width > maxw)
			f->fmt.pix.width = maxw;
		if (f->fmt.pix.height > maxh)
			f->fmt.pix.height = maxh;

		calc_bpl = (f->fmt.pix.width * fmt->depth)/8;

		if (f->fmt.pix.bytesperline < calc_bpl)
			f->fmt.pix.bytesperline = calc_bpl;
			
		if (f->fmt.pix.bytesperline > (2*PAGE_SIZE * fmt->depth)/8) /* arbitrary constraint */
			f->fmt.pix.bytesperline = calc_bpl;
			
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
		DEB_D(("w:%d, h:%d, bytesperline:%d, sizeimage:%d\n",f->fmt.pix.width,f->fmt.pix.height,f->fmt.pix.bytesperline,f->fmt.pix.sizeimage));

		return 0;
	}
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		DEB_EE(("V4L2_BUF_TYPE_VIDEO_OVERLAY: dev:%p, fh:%p\n",dev,fh));
		err = try_win(dev,&f->fmt.win);
		if (0 != err) {
			return err;
		}
		return 0;
	default:
		DEB_EE(("unknown format type '%d'\n",f->type));
		return -EINVAL;
	}
}

int saa7146_start_preview(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	int err = 0;

	DEB_EE(("dev:%p, fh:%p\n",dev,fh));

	/* check if we have overlay informations */
	if( NULL == fh->ov.fh ) {
		DEB_D(("not overlay data available. try S_FMT first.\n"));
		return -EAGAIN;
	}

	/* check if overlay is running */
	if( 0 != vv->ov_data ) {
		if( fh != vv->ov_data->fh ) {
			DEB_D(("overlay is running in another open.\n"));
			return -EAGAIN;
		}
		DEB_D(("overlay is already active.\n"));
		return 0;
	}
	
	if( 0 != vv->streaming ) {
		DEB_D(("streaming capture is active.\n"));
		return -EBUSY;
	}	
	
	err = try_win(dev,&fh->ov.win);
	if (0 != err) {
		return err;
	}
	
	vv->ov_data = &fh->ov;

	DEB_D(("%dx%d+%d+%d %s field=%s\n",
		fh->ov.win.w.width,fh->ov.win.w.height,
		fh->ov.win.w.left,fh->ov.win.w.top,
		vv->ov_fmt->name,v4l2_field_names[fh->ov.win.field]));
	
	saa7146_set_overlay(dev, fh, 1);

	return 0;
}

int saa7146_stop_preview(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	DEB_EE(("saa7146.o: saa7146_stop_preview()\n"));

	/* check if overlay is running */
	if( 0 == vv->ov_data ) {
		DEB_D(("overlay is not active.\n"));
		return 0;
	}

	if( fh != vv->ov_data->fh ) {
		DEB_D(("overlay is active, but for another open.\n"));
		return -EBUSY;
	}

	saa7146_set_overlay(dev, fh, 0);
	vv->ov_data = NULL;
	
	return 0;
}

static int s_fmt(struct saa7146_fh *fh, struct v4l2_format *f)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	unsigned long flags;
	int err;
	
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		DEB_EE(("V4L2_BUF_TYPE_VIDEO_CAPTURE: dev:%p, fh:%p\n",dev,fh));
		if( fh == vv->streaming ) {
			DEB_EE(("streaming capture is active"));
			return -EAGAIN;
		}
		err = try_fmt(fh,f);
		if (0 != err)
			return err;
		fh->video_fmt = f->fmt.pix;
		DEB_EE(("set to pixelformat '%4.4s'\n",(char *)&fh->video_fmt.pixelformat));
		return 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		DEB_EE(("V4L2_BUF_TYPE_VIDEO_OVERLAY: dev:%p, fh:%p\n",dev,fh));
		err = try_win(dev,&f->fmt.win);
		if (0 != err)
			return err;
		down(&dev->lock);
		fh->ov.win    = f->fmt.win;
		fh->ov.nclips = f->fmt.win.clipcount;
		if (fh->ov.nclips > 16)
			fh->ov.nclips = 16;
		if (copy_from_user(fh->ov.clips,f->fmt.win.clips,sizeof(struct v4l2_clip)*fh->ov.nclips)) {
			up(&dev->lock);
			return -EFAULT;
		}
		
		/* fh->ov.fh is used to indicate that we have valid overlay informations, too */
		fh->ov.fh = fh;

		/* check if we have an active overlay */
		if( vv->ov_data != NULL ) {
			if( fh == vv->ov_data->fh) {
				spin_lock_irqsave(&dev->slock,flags);
				saa7146_stop_preview(fh);
				saa7146_start_preview(fh);
				spin_unlock_irqrestore(&dev->slock,flags);
			}
		}
		up(&dev->lock);
		return 0;
	default:
		DEB_D(("unknown format type '%d'\n",f->type));
		return -EINVAL;
	}
}

/********************************************************************************/
/* device controls */

static struct v4l2_queryctrl controls[] = {
	{
		id:            V4L2_CID_BRIGHTNESS,
		name:          "Brightness",
		minimum:       0,
		maximum:       255,
		step:          1,
		default_value: 128,
		type:          V4L2_CTRL_TYPE_INTEGER,
	},{
		id:            V4L2_CID_CONTRAST,
		name:          "Contrast",
		minimum:       0,
		maximum:       127,
		step:          1,
		default_value: 64,
		type:          V4L2_CTRL_TYPE_INTEGER,
	},{
		id:            V4L2_CID_SATURATION,
		name:          "Saturation",
		minimum:       0,
		maximum:       127,
		step:          1,
		default_value: 64,
		type:          V4L2_CTRL_TYPE_INTEGER,
	},{
		id:            V4L2_CID_VFLIP,
		name:          "Vertical flip",
		minimum:       0,
		maximum:       1,
		type:          V4L2_CTRL_TYPE_BOOLEAN,
	},{
		id:            V4L2_CID_HFLIP,
		name:          "Horizontal flip",
		minimum:       0,
		maximum:       1,
		type:          V4L2_CTRL_TYPE_BOOLEAN,
	},
};
static int NUM_CONTROLS = sizeof(controls)/sizeof(struct v4l2_queryctrl);

#define V4L2_CID_PRIVATE_LASTP1      (V4L2_CID_PRIVATE_BASE + 0)

static struct v4l2_queryctrl* ctrl_by_id(int id)
{
	int i;
	
	for (i = 0; i < NUM_CONTROLS; i++)
		if (controls[i].id == id)
			return controls+i;
	return NULL;
}

static int get_control(struct saa7146_fh *fh, struct v4l2_control *c)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	const struct v4l2_queryctrl* ctrl;
	u32 value = 0;

	ctrl = ctrl_by_id(c->id);
	if (NULL == ctrl)
		return -EINVAL;
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		value = saa7146_read(dev, BCS_CTRL);
		c->value = 0xff & (value >> 24);
		DEB_D(("V4L2_CID_BRIGHTNESS: %d\n",c->value));
		break;
	case V4L2_CID_CONTRAST:
		value = saa7146_read(dev, BCS_CTRL);
		c->value = 0x7f & (value >> 16);
		DEB_D(("V4L2_CID_CONTRAST: %d\n",c->value));
		break;
	case V4L2_CID_SATURATION:
		value = saa7146_read(dev, BCS_CTRL);
		c->value = 0x7f & (value >> 0);
		DEB_D(("V4L2_CID_SATURATION: %d\n",c->value));
		break;
	case V4L2_CID_VFLIP:
		c->value = vv->vflip;
		DEB_D(("V4L2_CID_VFLIP: %d\n",c->value));
		break;
	case V4L2_CID_HFLIP:
		c->value = vv->hflip;
		DEB_D(("V4L2_CID_HFLIP: %d\n",c->value));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int set_control(struct saa7146_fh *fh, struct v4l2_control *c)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	const struct v4l2_queryctrl* ctrl;
	unsigned long flags;
	int restart_overlay = 0;

	ctrl = ctrl_by_id(c->id);
	if (NULL == ctrl) {
		DEB_D(("unknown control %d\n",c->id));
		return -EINVAL;
	}
	
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER:
		if (c->value < ctrl->minimum)
			c->value = ctrl->minimum;
		if (c->value > ctrl->maximum)
			c->value = ctrl->maximum;
		break;
	default:
		/* nothing */;
	};

	switch (c->id) {
	case V4L2_CID_BRIGHTNESS: {
		u32 value = saa7146_read(dev, BCS_CTRL);
		value &= 0x00ffffff;
		value |= (c->value << 24);
		saa7146_write(dev, BCS_CTRL, value);
		saa7146_write(dev, MC2, MASK_22 | MASK_06 );
		break;
	}
	case V4L2_CID_CONTRAST: {
		u32 value = saa7146_read(dev, BCS_CTRL);
		value &= 0xff00ffff;
		value |= (c->value << 16);
		saa7146_write(dev, BCS_CTRL, value);
		saa7146_write(dev, MC2, MASK_22 | MASK_06 );
		break;
	}
	case V4L2_CID_SATURATION: {
		u32 value = saa7146_read(dev, BCS_CTRL);
		value &= 0xffffff00;
		value |= (c->value << 0);
		saa7146_write(dev, BCS_CTRL, value);
		saa7146_write(dev, MC2, MASK_22 | MASK_06 );
		break;
	}
	case V4L2_CID_HFLIP:
		/* fixme: we can supfhrt changing VFLIP and HFLIP here... */
		if( 0 != vv->streaming ) {
			DEB_D(("V4L2_CID_HFLIP while active capture.\n"));
			return -EINVAL;
		}
		vv->hflip = c->value;
		restart_overlay = 1;
		break;
	case V4L2_CID_VFLIP:
		if( 0 != vv->streaming ) {
			DEB_D(("V4L2_CID_VFLIP while active capture.\n"));
			return -EINVAL;
		}
		vv->vflip = c->value;
		restart_overlay = 1;
		break;
	default: {
		return -EINVAL;
	}
	}
	if( 0 != restart_overlay ) {
		if( 0 != vv->ov_data ) {
			if( fh == vv->ov_data->fh ) {
				spin_lock_irqsave(&dev->slock,flags);
				saa7146_stop_preview(fh);
				saa7146_start_preview(fh);
				spin_unlock_irqrestore(&dev->slock,flags);
			}
		}
	}
	return 0;
}

/********************************************************************************/
/* common pagetable functions */

static int saa7146_pgtable_build(struct saa7146_dev *dev, struct saa7146_buf *buf)
{
	struct pci_dev *pci = dev->pci;
	struct scatterlist *list = buf->vb.dma.sglist;
	int length = buf->vb.dma.sglen;
	struct saa7146_format *sfmt = format_by_fourcc(dev,buf->fmt->pixelformat);

	DEB_EE(("dev:%p, buf:%p, sg_len:%d\n",dev,buf,length));

	if( 0 != IS_PLANAR(sfmt->trans)) {
		struct saa7146_pgtable *pt1 = &buf->pt[0];
		struct saa7146_pgtable *pt2 = &buf->pt[1];
		struct saa7146_pgtable *pt3 = &buf->pt[2];
		u32  *ptr1, *ptr2, *ptr3;
		u32 fill;

		int size = buf->fmt->width*buf->fmt->height;
		int i,p,m1,m2,m3,o1,o2;

		switch( sfmt->depth ) {
			case 12: {
				/* create some offsets inside the page table */
				m1 = ((size+PAGE_SIZE)/PAGE_SIZE)-1;
				m2 = ((size+(size/4)+PAGE_SIZE)/PAGE_SIZE)-1;
				m3 = ((size+(size/2)+PAGE_SIZE)/PAGE_SIZE)-1;
				o1 = size%PAGE_SIZE;
				o2 = (size+(size/4))%PAGE_SIZE;
				DEB_CAP(("size:%d, m1:%d, m2:%d, m3:%d, o1:%d, o2:%d\n",size,m1,m2,m3,o1,o2));
				break;
			}
			case 16: {
				/* create some offsets inside the page table */
				m1 = ((size+PAGE_SIZE)/PAGE_SIZE)-1;
				m2 = ((size+(size/2)+PAGE_SIZE)/PAGE_SIZE)-1;
				m3 = ((2*size+PAGE_SIZE)/PAGE_SIZE)-1;
				o1 = size%PAGE_SIZE;
				o2 = (size+(size/2))%PAGE_SIZE;
				DEB_CAP(("size:%d, m1:%d, m2:%d, m3:%d, o1:%d, o2:%d\n",size,m1,m2,m3,o1,o2));
				break;
			}
			default: {
				return -1;
			}
		}
		
		ptr1 = pt1->cpu;
		ptr2 = pt2->cpu;
		ptr3 = pt3->cpu;

		/* walk all pages, copy all page addresses to ptr1 */
		for (i = 0; i < length; i++, list++) {
			for (p = 0; p * 4096 < list->length; p++, ptr1++) {
				*ptr1 = sg_dma_address(list) - list->offset;
			}
		}
/*
		ptr1 = pt1->cpu;
		for(j=0;j<40;j++) {
			printk("ptr1 %d: 0x%08x\n",j,ptr1[j]);
		}
*/		

		/* if we have a user buffer, the first page may not be
		   aligned to a page boundary. */
		pt1->offset = buf->vb.dma.sglist->offset;
		pt2->offset = pt1->offset+o1;
		pt3->offset = pt1->offset+o2;
		
		/* create video-dma2 page table */
		ptr1 = pt1->cpu;
		for(i = m1; i <= m2 ; i++, ptr2++) {
			*ptr2 = ptr1[i];
		}
		fill = *(ptr2-1);
		for(;i<1024;i++,ptr2++) {
			*ptr2 = fill;
		}
		/* create video-dma3 page table */
		ptr1 = pt1->cpu;
		for(i = m2; i <= m3; i++,ptr3++) {
			*ptr3 = ptr1[i];
		}
		fill = *(ptr3-1);
		for(;i<1024;i++,ptr3++) {
			*ptr3 = fill;
		}
		/* finally: finish up video-dma1 page table */		
		ptr1 = pt1->cpu+m1;
		fill = pt1->cpu[m1];
		for(i=m1;i<1024;i++,ptr1++) {
			*ptr1 = fill;
		}
/*
		ptr1 = pt1->cpu;
		ptr2 = pt2->cpu;
		ptr3 = pt3->cpu;
		for(j=0;j<40;j++) {
			printk("ptr1 %d: 0x%08x\n",j,ptr1[j]);
		}
		for(j=0;j<40;j++) {
			printk("ptr2 %d: 0x%08x\n",j,ptr2[j]);
		}
		for(j=0;j<40;j++) {
			printk("ptr3 %d: 0x%08x\n",j,ptr3[j]);
		}
*/				
	} else {
		struct saa7146_pgtable *pt = &buf->pt[0];
		return saa7146_pgtable_build_single(pci, pt, list, length);
	}

	return 0;
}


/********************************************************************************/
/* file operations */

static int video_begin(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	unsigned long flags;

	DEB_EE(("dev:%p, fh:%p\n",dev,fh));

	if( fh == vv->streaming ) {
		DEB_S(("already capturing.\n"));
		return 0;
	}
	if( vv->streaming != 0 ) {
		DEB_S(("already capturing, but in another open.\n"));
		return -EBUSY;
	}

	/* fixme: check for planar formats here, if we will interfere with
	   vbi capture for example */	

	spin_lock_irqsave(&dev->slock,flags);

	/* clear out beginning of streaming bit (rps register 0)*/
	saa7146_write(dev, MC2, MASK_27 );

	/* enable rps0 irqs */
	IER_ENABLE(dev, MASK_27);

	vv->streaming = fh;
	spin_unlock_irqrestore(&dev->slock,flags);
	return 0;
}

static int video_end(struct saa7146_fh *fh, struct file *file)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	unsigned long flags;
	
	DEB_EE(("dev:%p, fh:%p\n",dev,fh));

	if( vv->streaming != fh ) {
		DEB_S(("not capturing.\n"));
		return -EINVAL;
	}

	spin_lock_irqsave(&dev->slock,flags);

	/* disable rps0  */
	saa7146_write(dev, MC1, MASK_28);

	/* disable rps0 irqs */
	IER_DISABLE(dev, MASK_27);

	// fixme: only used formats here!
	/* fixme: look at planar formats here, especially at the
	   shutdown of planar formats! */

	/* shut down all used video dma transfers */
	/* fixme: what about the budget-dvb cards? they use
	   video-dma3, but video_end should not get called anyway ...*/
	saa7146_write(dev, MC1, 0x00700000);

	vv->streaming = NULL;

	spin_unlock_irqrestore(&dev->slock, flags);
	
	return 0;
}


/*
 * This function is _not_ called directly, but from
 * video_generic_ioctl (and maybe others).  userspace
 * copying is done already, arg is a kernel pointer.
 */

int saa7146_video_do_ioctl(struct inode *inode, struct file *file, unsigned int cmd, void *arg)
{
	struct saa7146_fh *fh  = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	unsigned long flags;
	int err = 0, result = 0, ee = 0;

	struct saa7146_use_ops *ops;
	struct videobuf_queue *q;

	/* check if extension handles the command */
	for(ee = 0; dev->ext_vv_data->ioctls[ee].flags != 0; ee++) {
		if( cmd == dev->ext_vv_data->ioctls[ee].cmd )
			break;
	}
	
	if( 0 != (dev->ext_vv_data->ioctls[ee].flags & SAA7146_EXCLUSIVE) ) {
		DEB_D(("extension handles ioctl exclusive.\n"));
		result = dev->ext_vv_data->ioctl(fh, cmd, arg);
		return result;
	}
	if( 0 != (dev->ext_vv_data->ioctls[ee].flags & SAA7146_BEFORE) ) {
		DEB_D(("extension handles ioctl before.\n"));
		result = dev->ext_vv_data->ioctl(fh, cmd, arg);
		if( -EAGAIN != result ) {
			return result;
		}
	}
	
	/* fixme: add handle "after" case (is it still needed?) */

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE: {
		ops = &saa7146_video_uops;
		q = &fh->video_q;
		break;
		}
	case V4L2_BUF_TYPE_VBI_CAPTURE: {
		ops = &saa7146_vbi_uops;
		q = &fh->vbi_q;
		break;
		}
	default:
		BUG();
		return 0;
	}

	switch (cmd) {
 	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;
		memset(cap,0,sizeof(*cap));

		DEB_EE(("VIDIOC_QUERYCAP\n"));
		
                strcpy(cap->driver, "saa7146 v4l2");
		strlcpy(cap->card, dev->ext->name, sizeof(cap->card));
		sprintf(cap->bus_info,"PCI:%s",dev->pci->slot_name);
		cap->version = SAA7146_VERSION_CODE;
		cap->capabilities =
			V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_VIDEO_OVERLAY |
			V4L2_CAP_READWRITE | 
			V4L2_CAP_STREAMING;
		cap->capabilities |= dev->ext_vv_data->capabilities;
		return 0;
	}
	case VIDIOC_G_FBUF:
	{
		struct v4l2_framebuffer *fb = arg;

		DEB_EE(("VIDIOC_G_FBUF\n"));

		*fb = vv->ov_fb;
		fb->capability = V4L2_FBUF_CAP_LIST_CLIPPING;
		return 0;
	}
	case VIDIOC_S_FBUF:
	{
		struct v4l2_framebuffer *fb = arg;
		struct saa7146_format *fmt;
		struct saa7146_fh *ov_fh = NULL;
		int restart_overlay = 0;

		DEB_EE(("VIDIOC_S_FBUF\n"));

/*
		if(!capable(CAP_SYS_ADMIN)) { // && !capable(CAP_SYS_RAWIO)) {
			DEB_D(("VIDIOC_S_FBUF: not CAP_SYS_ADMIN or CAP_SYS_RAWIO.\n"));
			return -EPERM;
		}
*/

		/* check args */
		fmt = format_by_fourcc(dev,fb->fmt.pixelformat);
		if (NULL == fmt) {
			return -EINVAL;
		}
		
		down(&dev->lock);

		if( vv->ov_data != NULL ) {
			ov_fh = vv->ov_data->fh;
			saa7146_stop_preview(ov_fh);
			restart_overlay = 1;
		}

		/* ok, accept it */
		vv->ov_fb = *fb;
		vv->ov_fmt = fmt;
		if (0 == vv->ov_fb.fmt.bytesperline)
			vv->ov_fb.fmt.bytesperline =
				vv->ov_fb.fmt.width*fmt->depth/8;

		if( 0 != restart_overlay ) {
			saa7146_start_preview(ov_fh);
		}

		up(&dev->lock);

		return 0;
	}
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = arg;
		int index;

		switch (f->type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		case V4L2_BUF_TYPE_VIDEO_OVERLAY: {
			index = f->index;
			if (index < 0 || index >= NUM_FORMATS) {
				return -EINVAL;
			}
			memset(f,0,sizeof(*f));
			f->index = index;
			strlcpy(f->description,formats[index].name,sizeof(f->description));
			f->pixelformat = formats[index].pixelformat;
			break;
		}
		default:
			return -EINVAL;	
		}

		DEB_EE(("VIDIOC_ENUM_FMT: type:%d, index:%d\n",f->type,f->index));
		return 0;
	}
	case VIDIOC_QUERYCTRL:
	{
		const struct v4l2_queryctrl *ctrl;
		struct v4l2_queryctrl *c = arg;

		if ((c->id <  V4L2_CID_BASE ||
		     c->id >= V4L2_CID_LASTP1) &&
		    (c->id <  V4L2_CID_PRIVATE_BASE ||
		     c->id >= V4L2_CID_PRIVATE_LASTP1))
			return -EINVAL;
			
		ctrl = ctrl_by_id(c->id);
		if( NULL == ctrl ) {
			return -EINVAL;
/*
			c->flags = V4L2_CTRL_FLAG_DISABLED;	
			return 0;
*/
		}

		DEB_EE(("VIDIOC_QUERYCTRL: id:%d\n",c->id));
		*c = *ctrl;
		return 0;
	}
	case VIDIOC_G_CTRL: {
		DEB_EE(("VIDIOC_G_CTRL\n"));
		return get_control(fh,arg);
	}
	case VIDIOC_S_CTRL:
	{
		DEB_EE(("VIDIOC_S_CTRL\n"));
		down(&dev->lock);
		err = set_control(fh,arg);
		up(&dev->lock);
		return err;
	}
        case VIDIOC_G_PARM:
        {
                struct v4l2_streamparm *parm = arg;
		if( parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ) {
			return -EINVAL;
		}
                memset(&parm->parm.capture,0,sizeof(struct v4l2_captureparm));
		parm->parm.capture.readbuffers = 1;
		// fixme: only for PAL!
		parm->parm.capture.timeperframe.numerator = 1;
		parm->parm.capture.timeperframe.denominator = 25;
                return 0;
        }
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = arg;
		DEB_EE(("VIDIOC_G_FMT\n"));
		return g_fmt(fh,f);
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;
		DEB_EE(("VIDIOC_S_FMT\n"));
		return s_fmt(fh,f);
	}
	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *f = arg;
		DEB_EE(("VIDIOC_TRY_FMT\n"));
		return try_fmt(fh,f);
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *id = arg;
		DEB_EE(("VIDIOC_G_STD\n"));
		*id = vv->standard->id;
		return 0;
	}
	/* the saa7146 supfhrts (used in conjunction with the saa7111a for example)
	   PAL / NTSC / SECAM. if your hardware does not (or does more)
	   -- override this function in your extension */
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *e = arg;
		if (e->index < 0 )
			return -EINVAL;
		if( e->index < dev->ext_vv_data->num_stds ) {
			DEB_EE(("VIDIOC_ENUMSTD: index:%d\n",e->index));
			v4l2_video_std_construct(e, dev->ext_vv_data->stds[e->index].id, dev->ext_vv_data->stds[e->index].name);
			return 0;
		}
		return -EINVAL;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;
		int i;
		
		int restart_overlay = 0;
		int found = 0;
		
		struct saa7146_fh *ov_fh = NULL;
						
		DEB_EE(("VIDIOC_S_STD\n"));

		if( 0 != vv->streaming ) {
			return -EBUSY;
		}

		down(&dev->lock);

		if( vv->ov_data != NULL ) {
			ov_fh = vv->ov_data->fh;
			saa7146_stop_preview(ov_fh);
			restart_overlay = 1;
		}

		for(i = 0; i < dev->ext_vv_data->num_stds; i++)
			if (*id & dev->ext_vv_data->stds[i].id)
				break;
		if (i != dev->ext_vv_data->num_stds) {
			vv->standard = &dev->ext_vv_data->stds[i];
			if( NULL != dev->ext_vv_data->std_callback )
				dev->ext_vv_data->std_callback(dev, vv->standard);
			found = 1;
		}

		if( 0 != restart_overlay ) {
			saa7146_start_preview(ov_fh);
		}
		up(&dev->lock);

		if( 0 == found ) {
			DEB_EE(("VIDIOC_S_STD: standard not found.\n"));
			return -EINVAL;
		}

		DEB_EE(("VIDIOC_S_STD: set to standard to '%s'\n",vv->standard->name));
		return 0;
	}
	case VIDIOC_OVERLAY:
	{
		int on = *(int *)arg;
		int err = 0;

		if( NULL == vv->ov_fmt && on != 0 ) {
			DEB_D(("VIDIOC_OVERLAY: no framebuffer informations. call S_FBUF first!\n"));
			return -EAGAIN;
		}

		DEB_D(("VIDIOC_OVERLAY on:%d\n",on));
		if( 0 != on ) {
			if( vv->ov_data != NULL ) {
				if( fh != vv->ov_data->fh) {
					return -EAGAIN;
				}
			}
			spin_lock_irqsave(&dev->slock,flags);
			err = saa7146_start_preview(fh);
			spin_unlock_irqrestore(&dev->slock,flags);
		} else {
			if( vv->ov_data != NULL ) {
				if( fh != vv->ov_data->fh) {
					return -EAGAIN;
				}
			}
			spin_lock_irqsave(&dev->slock,flags);
			err = saa7146_stop_preview(fh);
			spin_unlock_irqrestore(&dev->slock,flags);
		}
		return err;
	}
	case VIDIOC_REQBUFS: {
		struct v4l2_requestbuffers *req = arg;
		DEB_D(("VIDIOC_REQBUFS, type:%d\n",req->type));
		return videobuf_reqbufs(file,q,req);
	}
	case VIDIOC_QUERYBUF: {
		struct v4l2_buffer *buf = arg;
		DEB_D(("VIDIOC_QUERYBUF, type:%d, offset:%d\n",buf->type,buf->m.offset));
		return videobuf_querybuf(q,buf);
	}
	case VIDIOC_QBUF: {
		struct v4l2_buffer *buf = arg;
		int ret = 0;
		ret = videobuf_qbuf(file,q,buf);
		DEB_D(("VIDIOC_QBUF: ret:%d, index:%d\n",ret,buf->index));
		return ret;
	}
	case VIDIOC_DQBUF: {
		struct v4l2_buffer *buf = arg;
		int ret = 0;
		ret = videobuf_dqbuf(file,q,buf);
		DEB_D(("VIDIOC_DQBUF: ret:%d, index:%d\n",ret,buf->index));
		return ret;
	}
	case VIDIOC_STREAMON: {
		int *type = arg;
		DEB_D(("VIDIOC_STREAMON, type:%d\n",*type));

		if( 0 != (err = video_begin(fh))) {
				return err;
			}
		err = videobuf_streamon(file,q);
		return err;
	}
	case VIDIOC_STREAMOFF: {
		int *type = arg;

		DEB_D(("VIDIOC_STREAMOFF, type:%d\n",*type));
		err = videobuf_streamoff(file,q);
		video_end(fh, file);
		return err;
	}
	case VIDIOCGMBUF:
	{
		struct video_mbuf *mbuf = arg;
		struct videobuf_queue *q;
		int i;

		/* fixme: number of capture buffers and sizes for v4l apps */
		int gbuffers = 2; 
		int gbufsize = 768*576*4;
		
		DEB_D(("VIDIOCGMBUF \n"));

		q = &fh->video_q;
		down(&q->lock);
		err = videobuf_mmap_setup(file,q,gbuffers,gbufsize,
					  V4L2_MEMORY_MMAP);
		if (err < 0) {
			up(&q->lock);
			return err;
		}
		memset(mbuf,0,sizeof(*mbuf));
		mbuf->frames = gbuffers;
		mbuf->size   = gbuffers * gbufsize;
		for (i = 0; i < gbuffers; i++)
			mbuf->offsets[i] = i * gbufsize;
		up(&q->lock);
		return 0;
	}
	default:
		return v4l_compat_translate_ioctl(inode,file,cmd,arg,
						  saa7146_video_do_ioctl);
	}
	return 0;
}

/*********************************************************************************/
/* buffer handling functions                                                  */

static int buffer_activate (struct saa7146_dev *dev,
		     struct saa7146_buf *buf,
		     struct saa7146_buf *next)
{
	struct saa7146_vv *vv = dev->vv_data;

	buf->vb.state = STATE_ACTIVE;
	saa7146_set_capture(dev,buf,next);
	
	mod_timer(&vv->video_q.timeout, jiffies+BUFFER_TIMEOUT);
	return 0;
}

static int buffer_prepare(struct file *file, struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct saa7146_fh *fh = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_buf *buf = (struct saa7146_buf *)vb;
	int size,err = 0;

	DEB_CAP(("vbuf:%p\n",vb));

	/* sanity checks */
	if (fh->video_fmt.width  < 48 ||
	    fh->video_fmt.height < 32 ||
	    fh->video_fmt.width  > vv->standard->h_max_out ||
	    fh->video_fmt.height > vv->standard->v_max_out) {
		DEB_D(("w (%d) / h (%d) out of bounds.\n",fh->video_fmt.width,fh->video_fmt.height));
		return -EINVAL;
	}

	size = fh->video_fmt.sizeimage;
	if (0 != buf->vb.baddr && buf->vb.bsize < size) {
		DEB_D(("size mismatch.\n"));
		return -EINVAL;
	}
	
	DEB_CAP(("buffer_prepare [size=%dx%d,bytes=%d,fields=%s]\n",
		fh->video_fmt.width,fh->video_fmt.height,size,v4l2_field_names[fh->video_fmt.field]));
	if (buf->vb.width  != fh->video_fmt.width  ||
	    buf->vb.bytesperline != fh->video_fmt.bytesperline ||
	    buf->vb.height != fh->video_fmt.height ||
	    buf->vb.size   != size ||
	    buf->vb.field  != field      ||
	    buf->vb.field  != fh->video_fmt.field  ||
	    buf->fmt       != &fh->video_fmt) {
		saa7146_dma_free(dev,buf);
	}

	if (STATE_NEEDS_INIT == buf->vb.state) {
		struct saa7146_format *sfmt;
		
		buf->vb.bytesperline  = fh->video_fmt.bytesperline;
		buf->vb.width  = fh->video_fmt.width;
		buf->vb.height = fh->video_fmt.height;
		buf->vb.size   = size;
		buf->vb.field  = field;
		buf->fmt       = &fh->video_fmt;
		buf->vb.field  = fh->video_fmt.field;
		
		sfmt = format_by_fourcc(dev,buf->fmt->pixelformat);
			
		if( 0 != IS_PLANAR(sfmt->trans)) {
			saa7146_pgtable_free(dev->pci, &buf->pt[0]);
			saa7146_pgtable_free(dev->pci, &buf->pt[1]);
			saa7146_pgtable_free(dev->pci, &buf->pt[2]);

			saa7146_pgtable_alloc(dev->pci, &buf->pt[0]);
			saa7146_pgtable_alloc(dev->pci, &buf->pt[1]);
			saa7146_pgtable_alloc(dev->pci, &buf->pt[2]);
		} else {
			saa7146_pgtable_free(dev->pci, &buf->pt[0]);
			saa7146_pgtable_alloc(dev->pci, &buf->pt[0]);
		}
		
		err = videobuf_iolock(dev->pci,&buf->vb, &vv->ov_fb);
		if (err)
			goto oops;
		err = saa7146_pgtable_build(dev,buf);
		if (err)
			goto oops;
	}
	buf->vb.state = STATE_PREPARED;
	buf->activate = buffer_activate;

	return 0;

 oops:
	DEB_D(("error out.\n"));
	saa7146_dma_free(dev,buf);

	return err;
}

static int buffer_setup(struct file *file, unsigned int *count, unsigned int *size)
{
	struct saa7146_fh *fh = file->private_data;

	if (0 == *count || *count > MAX_SAA7146_CAPTURE_BUFFERS)
		*count = MAX_SAA7146_CAPTURE_BUFFERS;

	*size = fh->video_fmt.sizeimage;

	/* check if we exceed the "memory" parameter */
	if( (*count * *size) > (memory*1048576) ) {
		*count = (memory*1048576) / *size;
	}
	
	DEB_CAP(("%d buffers, %d bytes each.\n",*count,*size));

	return 0;
}

static void buffer_queue(struct file *file, struct videobuf_buffer *vb)
{
	struct saa7146_fh *fh = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_buf *buf = (struct saa7146_buf *)vb;
	
	DEB_CAP(("vbuf:%p\n",vb));
	saa7146_buffer_queue(fh->dev,&vv->video_q,buf);
}


static void buffer_release(struct file *file, struct videobuf_buffer *vb)
{
	struct saa7146_fh *fh = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_buf *buf = (struct saa7146_buf *)vb;
	
	DEB_CAP(("vbuf:%p\n",vb));
	saa7146_dma_free(dev,buf);
}

static struct videobuf_queue_ops video_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

/********************************************************************************/
/* file operations */

static void video_init(struct saa7146_dev *dev, struct saa7146_vv *vv)
{
        INIT_LIST_HEAD(&vv->video_q.queue);

	init_timer(&vv->video_q.timeout);
	vv->video_q.timeout.function = saa7146_buffer_timeout;
	vv->video_q.timeout.data     = (unsigned long)(&vv->video_q);
	vv->video_q.dev              = dev;

	/* set some default values */
	vv->standard = &dev->ext_vv_data->stds[0];

	/* FIXME: what's this? */
	vv->current_hps_source = SAA7146_HPS_SOURCE_PORT_A;
	vv->current_hps_sync = SAA7146_HPS_SYNC_PORT_A;
}


static void video_open(struct saa7146_dev *dev, struct file *file)
{
	struct saa7146_fh *fh = (struct saa7146_fh *)file->private_data;
	struct saa7146_format *sfmt;

	fh->video_fmt.width = 384;
	fh->video_fmt.height = 288;
	fh->video_fmt.pixelformat = V4L2_PIX_FMT_BGR24;
	fh->video_fmt.bytesperline = 0;
	fh->video_fmt.field = V4L2_FIELD_ANY;
	sfmt = format_by_fourcc(dev,fh->video_fmt.pixelformat);
	fh->video_fmt.sizeimage = (fh->video_fmt.width * fh->video_fmt.height * sfmt->depth)/8;

	videobuf_queue_init(&fh->video_q, &video_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct saa7146_buf));

	init_MUTEX(&fh->video_q.lock);
}


static void video_close(struct saa7146_dev *dev, struct file *file)
{
	struct saa7146_fh *fh = (struct saa7146_fh *)file->private_data;
	struct saa7146_vv *vv = dev->vv_data;
	unsigned long flags;
	
	if( 0 != vv->ov_data ) {
		if( fh == vv->ov_data->fh ) {
			spin_lock_irqsave(&dev->slock,flags);
			saa7146_stop_preview(fh);
			spin_unlock_irqrestore(&dev->slock,flags);
		}
	}
	
	if( fh == vv->streaming ) {
		video_end(fh, file);		
	}
}


static void video_irq_done(struct saa7146_dev *dev, unsigned long st)
{
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_dmaqueue *q = &vv->video_q;
	
	spin_lock(&dev->slock);
	DEB_CAP(("called.\n"));

	/* only finish the buffer if we have one... */
	if( NULL != q->curr ) {
		saa7146_buffer_finish(dev,q,STATE_DONE);
	}
	saa7146_buffer_next(dev,q,0);

	spin_unlock(&dev->slock);
}

static ssize_t video_read(struct file *file, char *data, size_t count, loff_t *ppos)
{
	struct saa7146_fh *fh = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	ssize_t ret = 0;

	int restart_overlay = 0;
	struct saa7146_fh *ov_fh = NULL;

	DEB_EE(("called.\n"));

	if( vv->ov_data != NULL ) {
		ov_fh = vv->ov_data->fh;
		saa7146_stop_preview(ov_fh);
		restart_overlay = 1;
	}

	if( 0 != video_begin(fh)) {
		return -EAGAIN;
	}
	ret = videobuf_read_one(file,&fh->video_q , data, count, ppos);
	video_end(fh, file);

	/* restart overlay if it was active before */
	if( 0 != restart_overlay ) {
		saa7146_start_preview(ov_fh);
	}
	
	return ret;
}

struct saa7146_use_ops saa7146_video_uops = {
	.init = video_init,
	.open = video_open,
	.release = video_close,
	.irq_done = video_irq_done,
	.read = video_read,
};

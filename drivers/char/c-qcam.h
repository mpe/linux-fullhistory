struct qcam_device {
	struct video_device vdev;
	struct pardevice *pdev;
	struct parport *pport;
	int width, height;
	int bpp;
	int contrast, brightness, whitebal;
	int transfer_scale;
	int top, left;
	unsigned int bidirectional;
};

#define QC_1_1 0
#define QC_2_1 2
#define QC_4_1 4
#define QC_16BPP 8
#define QC_32BPP 16
#define QC_24BPP 24

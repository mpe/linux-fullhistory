/*
 *  linux/drivers/block/umc8672.c	Version 0.01  Nov 16, 1995
 *
 *  Copyright (C) 1995  Linus Torvalds & author (see below)
 */

/*
 *  Principal Author/Maintainer:  PODIEN@hml2.atlas.de (Wolfram Podien)
 *
 *  This file provides support for the advanced features
 *  of the UMC 8672 IDE interface.
 *
 *  Version 0.01	Initial version, hacked out of ide.c,
 *			and #include'd rather than compiled separately.
 *			This will get cleaned up in a subsequent release.
 */

/*
 * VLB Controller Support from 
 * Wolfram Podien
 * Rohoefe 3
 * D28832 Achim
 * Germany
 *
 * To enable UMC8672 support there must a lilo line like
 * append="hd=umc8672"...
 * To set the speed according to the abilities of the hardware there must be a
 * line like
 * #define UMC_DRIVE0 11
 * in the beginning of the driver, which sets the speed of drive 0 to 11 (there
 * are some lines present). 0 - 11 are allowed speed values. These values are
 * the results from the DOS speed test programm supplied from UMC. 11 is the 
 * highest speed (about PIO mode 3)
 */

/*
 * The speeds will eventually become selectable using hdparm via ioctl's,
 * but for now they are coded here:
 */
#define UMC_DRIVE0      11              /* DOS messured drive Speeds */
#define UMC_DRIVE1      11              /* 0 - 11 allowed */
#define UMC_DRIVE2      11              /* 11 = Highest Speed */
#define UMC_DRIVE3      11              /* In case of crash reduce speed */

void out_umc (char port,char wert)
{
	outb_p (port,0x108);
	outb_p (wert,0x109);
}

byte in_umc (char port)
{
	outb_p (port,0x108);
	return inb_p (0x109);
}

void init_umc8672(void)
{
	int i,tmp;
	int speed [4];
/*      0    1    2    3    4    5    6    7    8    9    10   11      */
	char speedtab [3][12] = {
	{0xf ,0xb ,0x2 ,0x2 ,0x2 ,0x1 ,0x1 ,0x1 ,0x1 ,0x1 ,0x1 ,0x1 },
	{0x3 ,0x2 ,0x2 ,0x2 ,0x2 ,0x2 ,0x1 ,0x1 ,0x1 ,0x1 ,0x1 ,0x1 },
	{0xff,0xcb,0xc0,0x58,0x36,0x33,0x23,0x22,0x21,0x11,0x10,0x0}};

	cli ();
	outb_p (0x5A,0x108); /* enable umc */
	if (in_umc (0xd5) != 0xa0)
	{
		sti ();
		printk ("UMC8672 not found\n");
		return;  
	}
	speed[0] = UMC_DRIVE0;
	speed[1] = UMC_DRIVE1;
	speed[2] = UMC_DRIVE2;
	speed[3] = UMC_DRIVE3;
	for (i = 0;i < 4;i++)
	{
		if ((speed[i] < 0) || (speed[i] > 11))
		{
			sti ();
			printk ("UMC 8672 drive speed out of range. Drive %d Speed %d\n",
				i, speed[i]);
			printk ("UMC support aborted\n");
			return; 
		}
	}
	out_umc (0xd7,(speedtab[0][speed[2]] | (speedtab[0][speed[3]]<<4)));
	out_umc (0xd6,(speedtab[0][speed[0]] | (speedtab[0][speed[1]]<<4)));
	tmp = 0;
	for (i = 3; i >= 0; i--)
	{
		tmp = (tmp << 2) | speedtab[1][speed[i]];
	}
	out_umc (0xdc,tmp);
	for (i = 0;i < 4; i++)
	{
		out_umc (0xd0+i,speedtab[2][speed[i]]);
		out_umc (0xd8+i,speedtab[2][speed[i]]);
	}
	outb_p (0xa5,0x108); /* disable umc */
	sti ();
	printk ("Speeds for UMC8672 \n");
	for (i = 0;i < 4;i++)
		 printk ("Drive %d speed %d\n",i,speed[i]);
}

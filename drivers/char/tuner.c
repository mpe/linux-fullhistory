#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/version.h>

#include <linux/i2c.h>
#include <linux/videodev.h>

#include "tuner.h"

static int debug =  0; /* insmod parameter */
static int type  = -1; /* tuner type */

#define dprintk     if (debug) printk

#if LINUX_VERSION_CODE > 0x020100
MODULE_PARM(debug,"i");
MODULE_PARM(type,"i");
#endif

#if LINUX_VERSION_CODE < 0x02017f
void schedule_timeout(int j)
{
	current->state   = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + j;
	schedule();
}
#endif

struct tuner 
{
	struct i2c_bus   *bus;     /* where is our chip */
	int               addr;

	int type;            /* chip type */
	int freq;            /* keep track of the current settings */
	int radio;
};

/* ---------------------------------------------------------------------- */

struct tunertype 
{
	char *name;
	unsigned char Vendor;
	unsigned char Type;
  
	unsigned short thresh1; /* frequency Range for UHF,VHF-L, VHF_H */   
	unsigned short thresh2;  
	unsigned char VHF_L;
	unsigned char VHF_H;
	unsigned char UHF;
	unsigned char config; 
	unsigned char I2C;
	unsigned short IFPCoff;
};

/*
 *	The floats in the tuner struct are computed at compile time
 *	by gcc and cast back to integers. Thus we don't violate the
 *	"no float in kernel" rule.
 */
static struct tunertype tuners[] = {
        {"Temic PAL", TEMIC, PAL,
                16*140.25,16*463.25,0x02,0x04,0x01,0x8e,0xc2,623},
	{"Philips PAL_I", Philips, PAL_I,
	        16*140.25,16*463.25,0xa0,0x90,0x30,0x8e,0xc0,623},
	{"Philips NTSC", Philips, NTSC,
	        16*157.25,16*451.25,0xA0,0x90,0x30,0x8e,0xc0,732},
	{"Philips SECAM", Philips, SECAM,
	        16*168.25,16*447.25,0xA7,0x97,0x37,0x8e,0xc0,623},
	{"NoTuner", NoTuner, NOTUNER,
	         0        ,0        ,0x00,0x00,0x00,0x00,0x00,000},
	{"Philips PAL", Philips, PAL,
	        16*168.25,16*447.25,0xA0,0x90,0x30,0x8e,0xc0,623},
	{"Temic NTSC", TEMIC, NTSC,
	        16*157.25,16*463.25,0x02,0x04,0x01,0x8e,0xc2,732},
	{"TEMIC PAL_I", TEMIC, PAL_I,
	      //  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,0xc2,623},
	        16*170.00,16*450.00,0x02,0x04,0x01,0x8e,0xc2,623},
	{"Temic 4036 FY5 NTSC", TEMIC, NTSC,
		16*157.25,16*463.25,0xa0,0x90,0x30,0x8e,0xc2,732},
};

/* ---------------------------------------------------------------------- */

static int tuner_getstatus (struct tuner *t)
{
	return i2c_read(t->bus,t->addr+1);
}

#define TUNER_POR       0x80
#define TUNER_FL        0x40
#define TUNER_AFC       0x07

static int tuner_islocked (struct tuner *t)
{
        return (tuner_getstatus (t) & TUNER_FL);
}

static int tuner_afcstatus (struct tuner *t)
{
        return (tuner_getstatus (t) & TUNER_AFC) - 2;
}


static void set_tv_freq(struct tuner *t, int freq)
{
	u8 config;
	u16 div;
	struct tunertype *tun;
	LOCK_FLAGS;

	if (t->type == -1) {
		printk("tuner: tuner type not set\n");
		return;
	}

	tun=&tuners[t->type];
	if (freq < tun->thresh1) 
		config = tun->VHF_L;
	else if (freq < tun->thresh2) 
		config = tun->VHF_H;
	else
		config = tun->UHF;

	div=freq + tun->IFPCoff;
  	div&=0x7fff;

	LOCK_I2C_BUS(t->bus);
	if (i2c_write(t->bus, t->addr, (div>>8)&0x7f, div&0xff, 1)<0) {
	    printk("tuner: i2c i/o error #1\n");
	} else {
	    if (i2c_write(t->bus, t->addr, tun->config, config, 1))
		printk("tuner: i2c i/o error #2\n");
	}
	UNLOCK_I2C_BUS(t->bus);
}

static void set_radio_freq(struct tuner *t, int freq)
{
	u8 config;
	u16 div;
	struct tunertype *tun;
        LOCK_FLAGS;

	if (t->type == -1) {
		printk("tuner: tuner type not set\n");
		return;
	}

	tun=&tuners[t->type];
	config = 0xa5;
	div=freq + (int)(16*10.7);
  	div&=0x7fff;

	LOCK_I2C_BUS(t->bus);
	if (i2c_write(t->bus, t->addr, (div>>8)&0x7f, div&0xff, 1)<0) {
	    printk("tuner: i2c i/o error #1\n");
	} else {
	    if (i2c_write(t->bus, t->addr, tun->config, config, 1))
		printk("tuner: i2c i/o error #2\n");
	}
	if (debug) {
		UNLOCK_I2C_BUS(t->bus);
		current->state   = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/10);
		LOCK_I2C_BUS(t->bus);
		
		if (tuner_islocked (t))
			printk ("tuner: PLL locked\n");
		else
			printk ("tuner: PLL not locked\n");
		
		printk ("tuner: AFC: %d\n", tuner_afcstatus (t));
	}
	UNLOCK_I2C_BUS(t->bus);
}

/* ---------------------------------------------------------------------- */

static int tuner_attach(struct i2c_device *device)
{
	struct tuner *t;

	/*
	 *	For now we only try and attach these tuners to the BT848
	 *	bus. This same module will however work different species
	 *	of card using these chips. Just change the constraints
	 *	(i2c doesn't have a totally clash free 'address' space)
	 */
	 
	if(device->bus->id!=I2C_BUSID_BT848)
		return -EINVAL;
		
	device->data = t = kmalloc(sizeof(struct tuner),GFP_KERNEL);
	if (NULL == t)
		return -ENOMEM;
	memset(t,0,sizeof(struct tuner));
	strcpy(device->name,"tuner");
	t->bus  = device->bus;
	t->addr = device->addr;
	t->type = type;
	dprintk("tuner: type is %d (%s)\n",t->type,
		(t->type == -1 ) ? "autodetect" : tuners[t->type].name);
	
	MOD_INC_USE_COUNT;
	return 0;
}

static int tuner_detach(struct i2c_device *device)
{
	struct tuner *t = (struct tuner*)device->data;
	kfree(t);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int tuner_command(struct i2c_device *device,
	      unsigned int cmd, void *arg)
{
	struct tuner *t = (struct tuner*)device->data;
	int *iarg = (int*)arg;

	switch (cmd) 
	{
		case TUNER_SET_TYPE:
			if (t->type != -1)
				return 0;
			t->type = *iarg;
			dprintk("tuner: type set to %d (%s)\n",
			t->type,tuners[t->type].name);
			break;

		case TUNER_SET_TVFREQ:
			dprintk("tuner: tv freq set to %d.%02d\n",
				(*iarg)/16,(*iarg)%16*100/16);
			set_tv_freq(t,*iarg);
			t->radio = 0;
			t->freq = *iarg;
			break;
	    
		case TUNER_SET_RADIOFREQ:
			dprintk("tuner: radio freq set to %d.%02d\n",
				(*iarg)/16,(*iarg)%16*100/16);
			set_radio_freq(t,*iarg);
			t->radio = 1;
			t->freq = *iarg;
			break;
	    
		default:
			return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

struct i2c_driver i2c_driver_tuner = 
{
	"tuner",                      /* name       */
	I2C_DRIVERID_TUNER,           /* ID         */
	0xc0, 0xce,                   /* addr range */

	tuner_attach,
	tuner_detach,
	tuner_command
};

EXPORT_NO_SYMBOLS;

#ifdef MODULE
int init_module(void)
#else
int i2c_tuner_init(void)
#endif
{
	i2c_register_driver(&i2c_driver_tuner);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	i2c_unregister_driver(&i2c_driver_tuner);
}
#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

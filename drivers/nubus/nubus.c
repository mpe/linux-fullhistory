/*
 *	Macintosh Nubus Interface Code
 */

#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/nubus.h>
#include <linux/errno.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/hwtest.h>
/* for LCIII stuff; better find a general way like MACH_HAS_NUBUS */
#include <asm/macintosh.h>
#include <linux/proc_fs.h>


#undef LCIII_WEIRDNESS
 
static struct nubus_slot nubus_slots[16];
 
/*
 *	Please skip to the bottom of this file if you ate lunch recently
 *				-- Alan
 */
 

/*
 *	Yes this sucks. The ROM can appear on arbitary bytes of the long
 *	word. We are not amused.
 */
 
extern __inline__ int not_useful(void *p, int map)
{
	unsigned long pv=(unsigned long)p;
	pv&=3;
	if(map&(1<<pv))
		return 0;
	return 1;
}
 
static unsigned long nubus_get_rom(unsigned char **ptr, int len, int map)
{
	unsigned long v=0;
	unsigned char *p=*ptr;	/* as v|=*((*ptr)++) upset someone */
	while(len)
	{
		v<<=8;
		while(not_useful(p,map))
			p++;
		v|=*p++;
		len--;
	}
	*ptr=p;
	return v;
}

static void nubus_rewind(unsigned char **ptr, int len, int map)
{
	unsigned char *p=*ptr;
	
	if(len>65536)
		printk("rewind of %d!\n", len);
	while(len)
	{
		do
		{
			p--;
		}
		while(not_useful(p,map));
		len--;
	}
	*ptr=p;
}

static void nubus_advance(unsigned char **ptr, int len, int map)
{
	unsigned char *p=*ptr;
	if(len>65536)
		printk("advance of %d!\n", len);
	while(len)
	{
		while(not_useful(p,map))
			p++;
			p++;
		len--;
	}
	*ptr=p;
}

/*
 *	24bit signed offset to 32bit
 */
 
static unsigned long nubus_expand32(unsigned long foo)
{
	if(foo&0x00800000)	/* 24bit negative */
		foo|=0xFF000000;
	return foo;
}

static void nubus_move(unsigned char **ptr, int len, int map)
{
	if(len>0)
		nubus_advance(ptr,len,map);
	else if(len<0)
		nubus_rewind(ptr,-len,map);
}

static void *nubus_rom_addr(int slot)
{	
	/*
	 *	Returns the first byte after the card. We then walk
	 *	backwards to get the lane register and the config
	 */
	return (void *)(0xF1000000+(slot<<24));
}

void nubus_memcpy(int slot, void *to, unsigned char *p, int len)
{
	unsigned char *t=(unsigned char *)to;
	while(len)
	{
		*t++=nubus_get_rom(&p,1, nubus_slots[slot].slot_lanes);
		len--;
	}
}

void nubus_strncpy(int slot, void *to, unsigned char *p, int len)
{
	unsigned char *t=(unsigned char *)to;
	while(len)
	{
		*t=nubus_get_rom(&p,1, nubus_slots[slot].slot_lanes);
		if(!*t++)
			break;
		len--;
	}
}



	
unsigned char *nubus_dirptr(struct nubus_dirent *nd)
{
	unsigned char *p=(unsigned char *)(nd->base);
	
	nubus_move(&p, nubus_expand32(nd->value), nd->mask);
	return p;
}

	
struct nubus_dir *nubus_openrootdir(int slot)
{
	static struct nubus_dir nbdir;
	unsigned char *rp=nubus_rom_addr(slot);
	
	nubus_rewind(&rp,20, nubus_slots[slot].slot_lanes);

	nubus_move(&rp, nubus_expand32(nubus_slots[slot].slot_directory),
		nubus_slots[slot].slot_lanes);
		
	nbdir.base=rp;
	nbdir.length=nubus_slots[slot].slot_dlength;
	nbdir.count=0;
	nbdir.mask=nubus_slots[slot].slot_lanes;
	return &nbdir;
}

struct nubus_dir *nubus_opensubdir(struct nubus_dirent *d)
{
	static struct nubus_dir nbdir;
	unsigned char *rp=nubus_dirptr(d);
	nbdir.base=rp;
	nbdir.length=99999;/*slots[i].slot_dlength;*/
	nbdir.count=0;
	nbdir.mask=d->mask;
	return &nbdir;
}

void nubus_closedir(struct nubus_dir *nd)
{
	;
}

struct nubus_dirent *nubus_readdir(struct nubus_dir *nd)
{
	u32 resid;
	u8 rescode;
	static struct nubus_dirent d;
	
	if(nd->count==nd->length)
		return NULL;

	d.base=(unsigned long)nd->base;
		
	resid=nubus_get_rom(&nd->base, 4, nd->mask);
	nd->count++;
	rescode=resid>>24;
	if(rescode==0xFF)
	{
		nd->count=nd->length;
		return NULL;
	}
	d.type=rescode;
	d.value=resid&0xFFFFFF;
	d.mask=nd->mask;
	return &d;
}

/*
 *	MAC video handling irritations
 */

static unsigned char nubus_vid_byte[16];
static unsigned long nubus_vid_offset[16];

static void nubus_irqsplat(int slot, void *dev_id, struct pt_regs *regs)
{
	unsigned char *p=((unsigned char *)nubus_slot_addr(slot))+
			nubus_vid_offset[slot];
	*p=nubus_vid_byte[slot];
}

static int nubus_add_irqsplatter(int slot, unsigned long ptr, unsigned char v)
{
	nubus_vid_byte[slot]=v;
	nubus_vid_offset[slot]=ptr;
	nubus_request_irq(slot, NULL, nubus_irqsplat);
	return 0;
}
 
void nubus_video_shutup(int slot, struct nubus_type *nt)
{
	if(nt->category!=3 /* Display */ || nt->type!=1 /* Video */
		|| nt->DrSW!=1 /* Quickdraw device */)
		return;
	switch(nt->DrHW)
	{
		/*
		 *	Toby and MacII Hires cards. These behave in a MacII
		 *	anyway but not on an RBV box
		 */
		case 0x0001:
		case 0x0013:
			nubus_add_irqsplatter(slot, 0xA0000, 0);
			break;
		/*
		 *	Apple workstation video card.
		 */
		case 0x0006:
			nubus_add_irqsplatter(slot, 0xA00000, 0);
			break;
		/*
		 *	Futura cards
		 */
		case 0x0417:
		case 0x042F:
			nubus_add_irqsplatter(slot, 0xF05000, 0x80);
			break;
			
		/*
		 *	Fingers crossed 8)
		 *	
		 *	If you have another card and an RBV based mac you'll
		 *	almost certainly have to add it here to make it work.
		 */
		
		default:
			break;
	}
}

/*
 *	Device list
 */

static struct nubus_device_specifier *nubus_device_list=NULL;
 
void register_nubus_device(struct nubus_device_specifier *d)
{
	d->next=nubus_device_list;
	nubus_device_list=d;
}

void unregister_nubus_device(struct nubus_device_specifier *nb)
{
	struct nubus_device_specifier **t=&nubus_device_list;
	while(*t!=nb && *t)
		t=&((*t)->next);
	*t=nb->next;
}

static struct nubus_device_specifier *find_nubus_device(int slot, struct nubus_type *nt)
{
	struct nubus_device_specifier *t=nubus_device_list; 
	while(t!=NULL)
	{
		if(t->setup(t,slot, nt)==0)
			return t;
		t=t->next;
	}
	printk("No driver for device [%d %d %d %d]\n",
		nt->category, nt->type, nt->DrHW, nt->DrSW);
	return NULL;
}

/*
 *	Probe a nubus slot
 */

void nubus_probe_slot(int slot, int mode)
{
	unsigned char *rp;
	unsigned char dp;
	int lanes;
	int i;
	unsigned long dpat;
	struct nubus_dir *dir;
	struct nubus_dirent *nd;
	struct nubus_type type_info;

	/*
	 *	Ok see whats cooking in the bytelanes
	 */
	
	rp=nubus_rom_addr(slot);
	
	for(i=4;i;i--)
	{
		rp--;
		
		if(!hwreg_present(rp))
			continue;
			
		dp=*rp;
	
		if(dp==0)
			continue;
		
		/*
		 *	Valid ?
		 */
		 
		if((((dp>>4)^dp)&0x0F)!=0x0F)
			continue;
			
		if((dp&0x0F) >= 1<<i)
			continue;
			
		/*
		 *	Looks promising
		 */
		
		nubus_slots[slot].slot_flags|=NUBUS_DEVICE_PRESENT;
		lanes=dp;

		if (mode==0)
			printk("nubus%c: ",
				"0123456789abcdef"[slot]);
		
		
		/*
		 *	Time to dig deeper. Find the ROM base
		 *	and read it
		 */
		 
		rp=nubus_rom_addr(slot); 
		
		/*
		 *	Now to make this more fun the ROM is only visible
		 *	on its bytelanes - that is smeared across the address
		 *	space.
		 */
		
		nubus_rewind(&rp,20,lanes);
		
		nubus_slots[slot].slot_directory=	nubus_get_rom(&rp,4,lanes);
		nubus_slots[slot].slot_dlength	=	nubus_get_rom(&rp,4,lanes);
		nubus_slots[slot].slot_crc	=	nubus_get_rom(&rp,4,lanes);
		nubus_slots[slot].slot_rev	=	nubus_get_rom(&rp,1,lanes);
		nubus_slots[slot].slot_format	=	nubus_get_rom(&rp,1,lanes);
		nubus_slots[slot].slot_lanes	=	lanes;
		
		dpat=nubus_get_rom(&rp,4,lanes);
		
		/*
		 *	Ok now check what we got
		 */
		
		if(!(nubus_slots[slot].slot_directory&0x00FF0000))
			printk("Dodgy doffset ??\n");
		if(dpat!=0x5A932BC7)
			printk("Wrong test pattern %lx\n",dpat);
		
		/*
		 *	I wonder how the CRC is meant to work -
		 *		any takers ?
		 */
		
		
		/*
		 *	Now parse the directories on the card
		 */
		 
		
		dir=nubus_openrootdir(slot);
		
		/*
		 *	Find the board resource
		 */
		 
		while((nd=nubus_readdir(dir))!=NULL)
		{
			/*
			 *	This ought to be 1. 1 doesn't work, 0x80
			 *	does. Seems the Apple docs are wrong.
			 */
			if(nd->type==0x80/*RES_ID_BOARD_DIR*/)
				break;
		}
		
		nubus_closedir(dir);
		
		if(nd==NULL)
		{
			printk("board resource not found!\n");
			return;
		}
		
		dir=nubus_opensubdir(nd);
				
		/*
		 *	Walk the board resource
		 */
		 
		while((nd=nubus_readdir(dir))!=NULL)
		{
			switch(nd->type)
			{
				case RES_ID_TYPE:
				{
					unsigned short nbtdata[4];
					nubus_memcpy(slot, nbtdata, nubus_dirptr(nd), 8);
					type_info.category=nbtdata[0];
					type_info.type=nbtdata[1];
					type_info.DrHW=nbtdata[2];
					type_info.DrSW=nbtdata[3];
					break;
				}
				case RES_ID_NAME:
					nubus_strncpy(slot, nubus_slots[slot].slot_cardname,nubus_dirptr(nd),64);
					break;
				default:
					;
			}
		}
		
		nubus_closedir(dir);

		/*
		 *	Attempt to bind a driver to this slot
		 */
		
		if (mode==0) {
			printk("%s\n",
				nubus_slots[slot].slot_cardname);
			find_nubus_device(slot,&type_info);
		}
		if (mode==1)
			nubus_video_shutup(slot, &type_info);

		return;
	}
}


void nubus_probe_bus(void)
{
	int i;
	for(i=9;i<15;i++)
	{
		/* printk("nubus: probing slot %d !\n", i); */
		nubus_probe_slot(i, 0);
	}
}

/*
 *	RBV machines have level triggered video interrupts, and a VIA
 *	emulation that doesn't always seem to include being able to disable 
 *	an interrupt. Totally lusing hardware. Before we can init irq's we
 *	have to install a handler to shut the bloody things up.
 */

void nubus_sweep_video(void)
{
	int i;
	return; /* XXX why ?? */
	for(i=9;i<15;i++)
	{
		nubus_probe_slot(i,1);
	}
}

/*
 *	Support functions
 */
 
int nubus_ethernet_addr(int slot, unsigned char *addr)
{
	struct nubus_dir *nb;
	struct nubus_dirent *d;
	int ng=-ENOENT;
		
	nb=nubus_openrootdir(slot);
	
	if(nb==NULL)
		return -ENOENT;
		
	while((d=nubus_readdir(nb))!=NULL)
	{
		if(d->type==0x80)	/* First private resource */
			break;
	}
	if(d==NULL)
		return -ENOENT;
	
	nb=nubus_opensubdir(d);
	
	while((d=nubus_readdir(nb))!=NULL)
	{
		if(d->type==0x80)	/* First private field is the mac */
		{
			int i;
			nubus_memcpy(slot, addr, nubus_dirptr(d), 6);
/*			printk("d.base=%lX, d.value=%lX\n",
				d->base,d->value);
			memcpy(addr,"\xC0\xC1\xC2\xC3\xC4\xC5",6);*/
			printk("MAC address: ");
			for(i=0;i<6;i++)
			{
				printk("%s%02X", i?":":"", addr[i]);
			}
			ng=0;
			break;
		}
		else
			printk("ID=%d val=%x\n",
				d->type, d->value);
	}
	return ng;
}

#ifdef CONFIG_PROC_FS

/*
 *	/proc for Nubus devices
 */
 
static int sprint_nubus_config(int slot, char *ptr, int len)
{
	if(len<150)
		return -1;
	sprintf(ptr, "Device: %s %s\n", nubus_slots[slot].slot_cardname,
		(nubus_slots[slot].slot_flags&NUBUS_DEVICE_ACTIVE)?
			"[active]":"[unused]");
	return strlen(ptr);
}

int get_nubus_list(char *buf)
{
	int nprinted, len, size;
	int slot;
#define MSG "\nwarning: page-size limit reached!\n"

	/* reserve same for truncation warning message: */
	size  = PAGE_SIZE - (strlen(MSG) + 1);
	len   = sprintf(buf, "Nubus devices found:\n");

	for (slot=0; slot< 16; slot++) 
	{
		if(!(nubus_slots[slot].slot_flags&NUBUS_DEVICE_PRESENT))
			continue;
		nprinted = sprint_nubus_config(slot, buf + len, size - len);
		if (nprinted < 0) {
			return len + sprintf(buf + len, MSG);
		}
		len += nprinted;
	}
	return len;
}

static struct proc_dir_entry proc_nubus = {
	PROC_NUBUS, 5, "nubus",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};
#endif

void nubus_init(void)
{
	/* 
	 *	Register cards
	 */
#ifdef CONFIG_DAYNAPORT
	extern struct nubus_device_specifier nubus_8390;	
#endif

	if (!MACH_IS_MAC) 
		return;

#ifdef LCIII_WEIRDNESS
	if (macintosh_config->ident == MAC_MODEL_LCIII) {
	        printk("nubus init: LCIII has no nubus!\n");
		return;
	}
#endif

#ifdef CONFIG_DAYNAPORT
	register_nubus_device(&nubus_8390);
#endif

	/*
	 *	And probe
	 */
	 
	nubus_init_via();
	printk("Scanning nubus slots.\n");
	nubus_probe_bus();
#ifdef CONFIG_PROC_FS
	proc_register(&proc_root, &proc_nubus);
#endif
}

	
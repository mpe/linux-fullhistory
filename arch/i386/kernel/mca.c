/*
 *  linux/arch/i386/kernel/mca.c
 *  Written by Martin Kolinek, February 1996
 *
 * Changes:
 *
 *	Chris Beauregard July 28th, 1996
 *	- Fixed up integrated SCSI detection
 *
 *	Chris Beauregard August 3rd, 1996
 *	- Made mca_info local
 *	- Made integrated registers accessible through standard function calls
 *	- Added name field
 *	- More sanity checking
 *
 *	Chris Beauregard August 9th, 1996
 *	- Rewrote /proc/mca
 *	
 *	Chris Beauregard January 7th, 1997
 *	- Added basic NMI-processing
 *	- Added more information to mca_info structure
 *
 *	David Weinehall October 12th, 1998
 *	- Made a lot of cleaning up in the source
 *	- Added use of save_flags / restore_flags
 *	- Added the 'driver_loaded' flag in MCA_adapter
 *	- Added an alternative implemention of ZP Gu's mca_find_unused_adapter
 *
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mca.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/proc_fs.h>
#include <linux/mman.h>
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/ioport.h>
#include <asm/uaccess.h>
#include <linux/init.h>

/* This structure holds MCA information. Each (plug-in) adapter has 
 * eight POS registers. Then the machine may have integrated video and
 * SCSI subsystems, which also have eight POS registers.
 * Other miscellaneous information follows.
 */

typedef enum {                                                                  
	MCA_ADAPTER_NORMAL = 0,                                                 
	MCA_ADAPTER_NONE = 1,                                                   
	MCA_ADAPTER_DISABLED = 2,                                               
	MCA_ADAPTER_ERROR = 3                                                   
} MCA_AdapterStatus;    

struct MCA_adapter {
	MCA_AdapterStatus status;	/* is there a valid adapter? */
	int id;				/* adapter id value */
	unsigned char pos[8];		/* POS registers */
	int driver_loaded;		/* is there a driver installed? */
					/* 0 - No, 1 - Yes */
	char name[48];			/* adapter-name provided by driver */
	char procname[8];		/* name of /proc/mca file */
	MCA_ProcFn procfn;		/* /proc info callback */
	void* dev;			/* device/context info for callback */
};

struct MCA_info {
/* one for each of the 8 possible slots, plus one for integrated SCSI
   and one for integrated video. */

	struct MCA_adapter slot[MCA_NUMADAPTERS];

/* two potential addresses for integrated SCSI adapter - this will      
 * track which one we think it is
 */                                       

	unsigned char which_scsi;  
};

/* The mca_info structure pointer. If MCA bus is present, the function
 * mca_probe() is invoked. The function puts motherboard, then all
 * adapters into setup mode, allocates and fills an MCA_info structure,
 * and points this pointer to the structure. Otherwise the pointer 
 * is set to zero.
 */

static struct MCA_info* mca_info = 0;

/* MCA registers */

#define MCA_MOTHERBOARD_SETUP_REG	0x94
#define MCA_ADAPTER_SETUP_REG		0x96
#define MCA_POS_REG(n)			(0x100+(n))

#define MCA_ENABLED	0x01	/* POS 2, set if adapter enabled */

/*--------------------------------------------------------------------*/

#ifdef CONFIG_PROC_FS

static void mca_do_proc_init( void );
static int mca_default_procfn( char* buf, int slot );

static ssize_t proc_mca_read( struct file*, char*, size_t, loff_t *);

static struct file_operations proc_mca_operations = {
	NULL,			/* array_lseek */
	proc_mca_read,		/* array_read */
	NULL,			/* array_write */
	NULL,			/* array_readdir */
	NULL,			/* array_poll */
	NULL,			/* array_ioctl */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

static struct inode_operations proc_mca_inode_operations = {
	&proc_mca_operations,	/* default base directory file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};
#endif

/*--------------------------------------------------------------------*/

/* Build the status info for the adapter */

static void mca_configure_adapter_status( int slot ) {
	mca_info->slot[slot].status = MCA_ADAPTER_NONE;

	mca_info->slot[slot].id = mca_info->slot[slot].pos[0]
		+ (mca_info->slot[slot].pos[1] << 8);

	if( !mca_info->slot[slot].id ) {

		/* id = 0x0000 usually indicates hardware failure,
		 * however, ZP Gu (zpg@castle.net> reports that his 9556
		 * has 0x0000 as id and everything still works.
		 */

		mca_info->slot[slot].status = MCA_ADAPTER_ERROR;

		return;
	} else if( mca_info->slot[slot].id != 0xffff ) {

		/* 0xffff usually indicates that there's no adapter,
		 * however, some integrated adapters may have 0xffff as
		 * their id and still be valid. Examples are on-board
		 * VGA of the 55sx, the integrated SCSI of the 56 & 57,
		 * and possibly also the 95 ULTIMEDIA.
		 */

		mca_info->slot[slot].status = MCA_ADAPTER_NORMAL;
	}

	if( (mca_info->slot[slot].id == 0xffff ||
	     mca_info->slot[slot].id == 0x0000) && slot >= MCA_MAX_SLOT_NR ) {
		int j;

		for( j = 2; j < 8; j++ ) {
			if( mca_info->slot[slot].pos[j] != 0xff ) {
				mca_info->slot[slot].status = MCA_ADAPTER_NORMAL;
				break;
			}
		}
	}

	if( !(mca_info->slot[slot].pos[2] & MCA_ENABLED) ) {

		/* enabled bit is in pos 2 */

		mca_info->slot[slot].status = MCA_ADAPTER_DISABLED;
	}
} /* mca_configure_adapter_status */

/*--------------------------------------------------------------------*/

__initfunc(void mca_init(void))
{
	unsigned int  i, j;
	unsigned long flags;

	/* WARNING: Be careful when making changes here. Putting an adapter
	 * and the motherboard simultaneously into setup mode may result in 
	 * damage to chips (according to The Indispensible PC Hardware Book 
	 * by Hans-Peter Messmer). Also, we disable system interrupts (so  	
	 * that we are not disturbed in the middle of this).
	 */

	/* Make sure the MCA bus is present */
	
	if (!MCA_bus)
		return;
	printk( "Micro Channel bus detected.\n" );
	save_flags( flags );
	cli();

	/* Allocate MCA_info structure (at address divisible by 8) */

	mca_info = kmalloc(sizeof(struct MCA_info), GFP_ATOMIC);

	/* Make sure adapter setup is off */

	outb_p(0, MCA_ADAPTER_SETUP_REG);

	/* Put motherboard into video setup mode, read integrated video 
	 * pos registers, and turn motherboard setup off.
	 */

	outb_p(0xdf, MCA_MOTHERBOARD_SETUP_REG);
	mca_info->slot[MCA_INTEGVIDEO].name[0] = 0;
	for (j=0; j<8; j++) {
		mca_info->slot[MCA_INTEGVIDEO].pos[j] = inb_p(MCA_POS_REG(j)); 
	}
	mca_configure_adapter_status(MCA_INTEGVIDEO);

	/* Put motherboard into scsi setup mode, read integrated scsi
	 * pos registers, and turn motherboard setup off.
	 *
	 * It seems there are two possible SCSI registers.  Martin says that
	 * for the 56,57, 0xf7 is the one, but fails on the 76.
	 * Alfredo (apena@vnet.ibm.com) says
	 * 0xfd works on his machine.  We'll try both of them.  I figure it's
	 * a good bet that only one could be valid at a time.  This could
	 * screw up though if one is used for something else on the other
	 * machine.
	 */

	outb_p(0xf7, MCA_MOTHERBOARD_SETUP_REG);
	mca_info->slot[MCA_INTEGSCSI].name[0] = 0;
	for (j=0; j<8; j++)  {
		if( (mca_info->slot[MCA_INTEGSCSI].pos[j] = inb_p(MCA_POS_REG(j))) != 0xff ) 
		{
			/* 0xff all across means no device.  0x00 means something's
			 * broken, but a device is probably there.  However, if you get
			 * 0x00 from a motherboard register it won't matter what we
			 * find.  For the record, on the 57SLC, the integrated SCSI
			 * adapter has 0xffff for the adapter ID, but nonzero for
			 * other registers.
			 */

			mca_info->which_scsi = 0xf7;
		}
	}
	if( !mca_info->which_scsi ) { 

		/* Didn't find it at 0xf7, try somewhere else... */
		mca_info->which_scsi = 0xfd;

		outb_p(0xfd, MCA_MOTHERBOARD_SETUP_REG);
		for (j=0; j<8; j++) 
			mca_info->slot[MCA_INTEGSCSI].pos[j] = inb_p(MCA_POS_REG(j)); 
	}
	mca_configure_adapter_status(MCA_INTEGSCSI);
	
	/* turn off motherboard setup */

	outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);

	/* Now loop over MCA slots: put each adapter into setup mode, and
	 * read its pos registers. Then put adapter setup off.
	 */

	for (i=0; i<MCA_MAX_SLOT_NR; i++) {
		outb_p(0x8|(i&0xf), MCA_ADAPTER_SETUP_REG);
		for (j=0; j<8; j++) {
			mca_info->slot[i].pos[j]=inb_p(MCA_POS_REG(j)); 
		}
		mca_info->slot[i].name[0] = 0;
		mca_info->slot[i].driver_loaded = 0;
		mca_configure_adapter_status(i);
	}
	outb_p(0, MCA_ADAPTER_SETUP_REG);

	/* Enable interrupts and return memory start */

	restore_flags( flags );

	request_region(0x60,0x01,"system control port B (MCA)");
	request_region(0x90,0x01,"arbitration (MCA)");
	request_region(0x91,0x01,"card Select Feedback (MCA)");
	request_region(0x92,0x01,"system Control port A (MCA)");
	request_region(0x94,0x01,"system board setup (MCA)");
	request_region(0x96,0x02,"POS (MCA)");
	request_region(0x100,0x08,"POS (MCA)");

#ifdef CONFIG_PROC_FS
	mca_do_proc_init();
#endif
}

/*--------------------------------------------------------------------*/

static void mca_handle_nmi_slot( int slot, int check_flag )
{
        if( slot < MCA_MAX_SLOT_NR ) {                                          
                printk( "NMI: caused by MCA adapter in slot %d (%s)\n", slot+1,
                        mca_info->slot[slot].name );                            
        } else if( slot == MCA_INTEGSCSI ) {                                    
                printk( "NMI: caused by MCA integrated SCSI adapter (%s)\n",    
                        mca_info->slot[slot].name );                            
        } else if( slot == MCA_INTEGVIDEO ) {                                   
                printk( "NMI: caused by MCA integrated video adapter (%s)\n",   
                        mca_info->slot[slot].name );                            
        }                                                                       
                                                                                
        /* more info available in pos 6 and 7? */                               

        if( check_flag ) {                                                      
                unsigned char pos6, pos7;                                       
                                                                                
                pos6 = mca_read_pos( slot, 6 );                                 
                pos7 = mca_read_pos( slot, 7 );                                 
                                                                                
                printk( "NMI: POS 6 = 0x%x, POS 7 = 0x%x\n", pos6, pos7 );      
        }                                                                       
                                                                                
} /* mca_handle_nmi_slot */                                                     
                                                                                
/*--------------------------------------------------------------------*/        

void mca_handle_nmi( void )
{

	int i;
        unsigned char pos5;                                                     
                                                                                
        /* First try - scan the various adapters and see if a specific          
         * adapter was responsible for the error
	 */                                

        for( i = 0; i < MCA_NUMADAPTERS; i += 1 ) {                             
                                                                                
                /* bit 7 of POS 5 is reset when this adapter has a hardware     
                 * error.  bit 7 it reset if there's error information
                 * available in pos 6 and 7. */                                              
                                                                                
                pos5 = mca_read_pos( i, 5 );                                    
                                                                                
                if( !(pos5 & 0x80) ) {                                          
                        mca_handle_nmi_slot( i, !(pos5 & 0x40) );               
                        return;                                                 
                }                                                               
        }                                                                       
                                                                                
        /* if I recall correctly, there's a whole bunch of other things that    
         * we can do to check for NMI problems, but that's all I know about
	 * at the moment.
	 */                                                          

        printk( "NMI generated from unknown source!\n" );                       
} /* mca_handle_nmi */                                                          

/*--------------------------------------------------------------------*/

int mca_find_adapter( int id, int start ) 
{
	if( mca_info == 0 || id == 0 || id == 0xffff ) {
		return MCA_NOTFOUND;
	}

	for( ; start >= 0 && start < MCA_NUMADAPTERS; start += 1 ) {

		/* not sure about this.  There's no point in returning
		 * adapters that aren't enabled, since they can't actually
		 * be used.  However, they might be needed for statistical
		 * purposes or something... But if that is the case, the
		 * user is free to write a routine that manually iterates
		 * through the adapters.
		 */

		if( mca_info->slot[start].status == MCA_ADAPTER_DISABLED ) {
			continue;
		}

		if( id == mca_info->slot[start].id ) {
			return start;
		}
	}

	return MCA_NOTFOUND;
} /* mca_find_adapter() */

/*--------------------------------------------------------------------*/

int mca_find_unused_adapter( int id, int start ) 
{
	if( mca_info == 0 || id == 0 || id == 0xffff ) {
		return MCA_NOTFOUND;
	}

	for( ; start >= 0 && start < MCA_NUMADAPTERS; start += 1 ) {

		/* not sure about this.  There's no point in returning
		 * adapters that aren't enabled, since they can't actually
		 * be used.  However, they might be needed for statistical
		 * purposes or something... But if that is the case, the
		 * user is free to write a routine that manually iterates
		 * through the adapters.
		 */

		if( mca_info->slot[start].status == MCA_ADAPTER_DISABLED ||
		    mca_info->slot[start].driver_loaded ) {
			continue;
		}

		if( id == mca_info->slot[start].id ) {
			return start;
		}
	}

	return MCA_NOTFOUND;
} /* mca_find_unused_adapter() */	

/*--------------------------------------------------------------------*/

unsigned char mca_read_stored_pos( int slot, int reg ) 
{
	if( slot < 0 || slot >= MCA_NUMADAPTERS || mca_info == 0 ) return 0;
	if( reg < 0 || reg >= 8 ) return 0;
	return mca_info->slot[slot].pos[reg];
} /* mca_read_stored_pos() */

/*--------------------------------------------------------------------*/

unsigned char mca_read_pos( int slot, int reg ) 
{
	unsigned int byte = 0;
	unsigned long flags;

	if( slot < 0 || slot >= MCA_NUMADAPTERS || mca_info == 0 ) return 0;
	if( reg < 0 || reg >= 8 ) return 0;

	save_flags( flags );
	cli();

	/* make sure motherboard setup is off */

	outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);

	/* read in the appropriate register */

	if( slot == MCA_INTEGSCSI && mca_info->which_scsi ) {

		/* disable adapter setup, enable motherboard setup */

		outb_p(0, MCA_ADAPTER_SETUP_REG);
		outb_p(mca_info->which_scsi, MCA_MOTHERBOARD_SETUP_REG);

		byte = inb_p(MCA_POS_REG(reg));
		outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);
	} else if( slot == MCA_INTEGVIDEO ) {

		/* disable adapter setup, enable motherboard setup */

		outb_p(0, MCA_ADAPTER_SETUP_REG);
		outb_p(0xdf, MCA_MOTHERBOARD_SETUP_REG);

		byte = inb_p(MCA_POS_REG(reg));
		outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);
	} else if( slot < MCA_MAX_SLOT_NR ) {

		/* make sure motherboard setup is off */

		outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);

		/* read the appropriate register */

		outb_p(0x8|(slot&0xf), MCA_ADAPTER_SETUP_REG);
		byte = inb_p(MCA_POS_REG(reg));
		outb_p(0, MCA_ADAPTER_SETUP_REG);
	}

	/* make sure the stored values are consistent, while we're here */

	mca_info->slot[slot].pos[reg] = byte;

	restore_flags( flags );

	return byte;
} /* mca_read_pos() */

/*--------------------------------------------------------------------*/

/* Note that this a technically a Bad Thing, as IBM tech stuff says
 * you should only set POS values through their utilities.
 * However, some devices such as the 3c523 recommend that you write
 * back some data to make sure the configuration is consistent.
 * I'd say that IBM is right, but I like my drivers to work.
 * This function can't do checks to see if multiple devices end up
 * with the same resources, so you might see magic smoke if someone
 * screws up.
 */

void mca_write_pos( int slot, int reg, unsigned char byte ) 
{
	unsigned long flags;

	if( slot < 0 || slot >= MCA_MAX_SLOT_NR ) return;
	if( reg < 0 || reg >= 8 ) return;
	if (mca_info == 0 )  return;

	save_flags( flags );
	cli();

	/* make sure motherboard setup is off */

	outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);

	/* read in the appropriate register */

	outb_p(0x8|(slot&0xf), MCA_ADAPTER_SETUP_REG);
	outb_p( byte, MCA_POS_REG(reg) );
	outb_p(0, MCA_ADAPTER_SETUP_REG);

	restore_flags( flags );

	/* update the global register list, while we have the byte */

	mca_info->slot[slot].pos[reg] = byte;
} /* mca_write_pos() */

/*--------------------------------------------------------------------*/

void mca_set_adapter_name( int slot, char* name ) 
{
	if( mca_info == 0 ) return;

	if( slot >= 0 && slot < MCA_NUMADAPTERS ) {
		if( name != NULL ) {
			strncpy( mca_info->slot[slot].name, name,
				sizeof(mca_info->slot[slot].name)-1 );
			mca_info->slot[slot].name[
				sizeof(mca_info->slot[slot].name)-1] = 0;
		} else {
			mca_info->slot[slot].name[0] = 0;
		}
	}
}

void mca_set_adapter_procfn( int slot, MCA_ProcFn procfn, void* dev)
{
	if( mca_info == 0 ) return;

	if( slot >= 0 && slot < MCA_NUMADAPTERS ) {
		mca_info->slot[slot].procfn = procfn;
		mca_info->slot[slot].dev = dev;
	}
}

int mca_is_adapter_used( int slot )
{
	return mca_info->slot[slot].driver_loaded;
}

int mca_mark_as_used( int slot )
{
	if(mca_info->slot[slot].driver_loaded) return 1;
	mca_info->slot[slot].driver_loaded = 1;
	return 0;
}

void mca_mark_as_unused( int slot )
{
	mca_info->slot[slot].driver_loaded = 0;
}
 
char *mca_get_adapter_name( int slot ) 
{
	if( mca_info == 0 ) return 0;

	if( slot >= 0 && slot < MCA_NUMADAPTERS ) {
		return mca_info->slot[slot].name;
	}

	return 0;
}

int mca_isadapter( int slot )
{
	if( mca_info == 0 ) return 0;

	if( slot >= 0 && slot < MCA_NUMADAPTERS ) {
		return (( mca_info->slot[slot].status == MCA_ADAPTER_NORMAL )
			|| (mca_info->slot[slot].status == MCA_ADAPTER_DISABLED ) );
	}

	return 0;
}

int mca_isenabled( int slot )
{
	if( mca_info == 0 ) return 0;

	if( slot >= 0 && slot < MCA_NUMADAPTERS ) {
		return (mca_info->slot[slot].status == MCA_ADAPTER_NORMAL);
	}

	return 0;
}

/*--------------------------------------------------------------------*/

#ifdef CONFIG_PROC_FS

int  get_mca_info(char *buf) 
{
	int  i, j, len = 0; 

	if( MCA_bus && mca_info != 0 ) 
	{
		/* Format pos registers of eight MCA slots */

		for (i=0; i<MCA_MAX_SLOT_NR; i++) 
		{
			len += sprintf(buf+len, "Slot %d: ", i+1);
			for (j=0; j<8; j++) 
				len += sprintf(buf+len, "%02x ", mca_info->slot[i].pos[j]);
			len += sprintf( buf+len, " %s\n", mca_info->slot[i].name );
		}    

		/* Format pos registers of integrated video subsystem */

		len += sprintf(buf+len, "Video : ");
		for (j=0; j<8; j++) 
			len += sprintf(buf+len, "%02x ", mca_info->slot[MCA_INTEGVIDEO].pos[j]);
		len += sprintf( buf+len, " %s\n", mca_info->slot[MCA_INTEGVIDEO].name );

		/* Format pos registers of integrated SCSI subsystem */
	
		len += sprintf(buf+len, "SCSI  : ");
		for (j=0; j<8; j++) 
			len += sprintf(buf+len, "%02x ", mca_info->slot[MCA_INTEGSCSI].pos[j]);
		len += sprintf( buf+len, " %s\n", mca_info->slot[MCA_INTEGSCSI].name );
	} 
	else 
	{
	  	/* Leave it empty if MCA not detected - this should *never*
		 * happen! 
		 */
	}

	return len;
}


/*--------------------------------------------------------------------*/

__initfunc(void mca_do_proc_init( void ))
{
	int i = 0;
	struct proc_dir_entry* node = 0;

	if( mca_info == 0 ) return;	/* should never happen */

	proc_register( &proc_mca, &(struct proc_dir_entry) {
		PROC_MCA_REGISTERS, 3, "pos", S_IFREG|S_IRUGO,
		1, 0, 0, 0, &proc_mca_inode_operations,} );

	proc_register( &proc_mca, &(struct proc_dir_entry) {
		PROC_MCA_MACHINE, 7, "machine", S_IFREG|S_IRUGO,
		1, 0, 0, 0, &proc_mca_inode_operations,} );

	/* initialize /proc/mca entries for existing adapters */

	for( i = 0; i < MCA_NUMADAPTERS; i += 1 ) {
		mca_info->slot[i].procfn = 0;
		mca_info->slot[i].dev = 0;

		if( ! mca_isadapter( i ) ) continue;
		node = kmalloc(sizeof(struct proc_dir_entry), GFP_ATOMIC);

		if( i < MCA_MAX_SLOT_NR ) {
			node->low_ino = PROC_MCA_SLOT + i;
			node->namelen = sprintf( mca_info->slot[i].procname,
				"slot%d", i+1 );
		} else if( i == MCA_INTEGVIDEO ) {
			node->low_ino = PROC_MCA_VIDEO;
			node->namelen = sprintf( mca_info->slot[i].procname,
				"video" );
		} else if( i == MCA_INTEGSCSI ) {
			node->low_ino = PROC_MCA_SCSI;
			node->namelen = sprintf( mca_info->slot[i].procname,
				"scsi" );
		}
		node->name = mca_info->slot[i].procname;
		node->mode = S_IFREG | S_IRUGO;
		node->ops = &proc_mca_inode_operations;
		proc_register( &proc_mca, node );
	}

} /* mca_do_proc_init() */

/*--------------------------------------------------------------------*/

int mca_default_procfn( char* buf, int slot ) 
{
	int len = 0, i;

	/* this really shouldn't happen... */

	if( mca_info == 0 ) {
		*buf = 0;
		return 0;
	}

	/* print out the basic information */

	if( slot < MCA_MAX_SLOT_NR ) {
		len += sprintf( buf+len, "Slot: %d\n", slot+1 );
	} else if( slot == MCA_INTEGSCSI ) {
		len += sprintf( buf+len, "Integrated SCSI Adapter\n" );
	} else if( slot == MCA_INTEGVIDEO ) {
		len += sprintf( buf+len, "Integrated Video Adapter\n" );
	}
	if( mca_info->slot[slot].name[0] ) {

		/* drivers might register a name without /proc handler... */

		len += sprintf( buf+len, "Adapter Name: %s\n",
			mca_info->slot[slot].name );
	} else {
		len += sprintf( buf+len, "Adapter Name: Unknown\n" );
	}
	len += sprintf( buf+len, "Id: %02x%02x\n",
		mca_info->slot[slot].pos[1], mca_info->slot[slot].pos[0] );
	len += sprintf( buf+len, "Enabled: %s\nPOS: ",
		mca_isenabled(slot) ? "Yes" : "No" );
	len += sprintf( buf+len, "Driver Installed: %s\n",
		mca_is_adapter_used(slot) ? "Yes" : "No" );
	for (i=0; i<8; i++) {
		len += sprintf(buf+len, "%02x ", mca_info->slot[slot].pos[i]);
	}
	buf[len++] = '\n';
	buf[len] = 0;

	return len;
} /* mca_default_procfn() */

static int get_mca_machine_info( char* buf ) 
{
	int len = 0;

	len += sprintf( buf+len, "Model Id: 0x%x\n", machine_id );
	len += sprintf( buf+len, "Submodel Id: 0x%x\n", machine_submodel_id );
	len += sprintf( buf+len, "BIOS Revision: 0x%x\n", BIOS_revision );

	return len;
}

static int mca_fill( char* page, int pid, int type, char** start,
	loff_t *offset, int length)
{
	int len = 0;
	int slot = 0;

	switch( type ) {
		case PROC_MCA_REGISTERS:
			return get_mca_info( page );
		case PROC_MCA_MACHINE:
			return get_mca_machine_info( page );
		case PROC_MCA_VIDEO:
			slot = MCA_INTEGVIDEO;
			break;
		case PROC_MCA_SCSI:
			slot = MCA_INTEGSCSI;
			break;
		default:
			if( type < PROC_MCA_SLOT || type >= PROC_MCA_LAST ) {
				return -EBADF;
			}
			slot = type - PROC_MCA_SLOT;
			break;
	}

	/* if we made it here, we better have a valid slot */

	/* get the standard info */

	len = mca_default_procfn( page, slot );

	/* do any device-specific processing, if there is any */

	if( mca_info->slot[slot].procfn ) {
		len += mca_info->slot[slot].procfn( page+len, slot,
			mca_info->slot[slot].dev );
	}

	return len;
} /* mca_fill() */

/* Blatantly stolen from fs/proc/array.c, and thus is probably overkill */ 

#define PROC_BLOCK_SIZE	(3*1024)

static ssize_t proc_mca_read( struct file* file,
	char* buf, size_t count, loff_t *ppos)
{
	unsigned long page;
	char *start;
	int length;
	int end;
	unsigned int type, pid;
	struct proc_dir_entry *dp;
	struct inode *inode = file->f_dentry->d_inode;

	if (count < 0)
		return -EINVAL;
	if (count > PROC_BLOCK_SIZE)
		count = PROC_BLOCK_SIZE;
	if (!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	type = inode->i_ino;
	pid = type >> 16;
	type &= 0x0000ffff;
	start = 0;
	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	length = mca_fill((char *) page, pid, type,
			    &start, ppos, count);
	if (length < 0) {
		free_page(page);
		return length;
	}
	if (start != 0) {
		/* We have had block-adjusting processing! */

		copy_to_user(buf, start, length);
		*ppos += length;
		count = length;
	} else {
		/* Static 4kB (or whatever) block capacity */

		if (*ppos >= length) {
			free_page(page);
			return 0;
		}
		if (count + *ppos > length)
			count = length - *ppos;
		end = count + *ppos;
		copy_to_user(buf, (char *) page + *ppos, count);
		*ppos = end;
	}
	free_page(page);
	return count;
} /* proc_mca_read() */

#endif

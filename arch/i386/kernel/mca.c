/*
 *  linux/arch/i386/kernel/mca.c
 *  Written by Martin Kolinek, February 1996
 *
 * Changes:
 *   July 28, 1996: fixed up integrated SCSI detection. Chris Beauregard
 *   August 3rd, 1996: made mca_info local, made integrated registers
 *     accessible through standard function calls, added name field,
 *     more sanity checking. Chris Beauregard
 *   August 9, 1996: Rewrote /proc/mca.  cpbeaure
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

/* This structure holds MCA information. Each (plug-in) adapter has 
 * eight POS registers. Then the machine may have integrated video and
 * SCSI subsystems, which also have eight POS registers.
 * Other miscellaneous information follows.
*/
struct MCA_adapter {
	unsigned char pos[8];	/* POS registers */
	char name[32];		/* name of the device - provided by driver */
	char procname[8];	/* name of /proc/mca file */
	MCA_ProcFn procfn;	/* /proc info callback */
	void* dev;		/* device/context info for callback */
};

struct MCA_info {
	/* one for each of the 8 possible slots, plus one for integrated SCSI
	and one for integrated video. */
	struct MCA_adapter slot[MCA_NUMADAPTERS];
};

/* The mca_info structure pointer. If MCA bus is present, the function
 * mca_probe() is invoked. The function puts motherboard, then all
 * adapters into setup mode, allocates and fills an MCA_info structure,
 * and points this pointer to the structure. Otherwise the pointer 
 * is set to zero.
*/
static struct MCA_info* mca_info = 0;

/*MCA registers*/
#define MCA_MOTHERBOARD_SETUP_REG  0x94
#define MCA_ADAPTER_SETUP_REG      0x96
#define MCA_POS_REG(n)             (0x100+(n))

#define MCA_ENABLED	0x01	/* POS 2, set if adapter enabled */

/*--------------------------------------------------------------------*/

#ifdef CONFIG_PROC_FS
static long mca_do_proc_init( long memory_start, long memory_end );
static int mca_default_procfn( char* buf, int slot );

static long proc_mca_read( struct inode*, struct file*, char* buf, unsigned long count );
static struct file_operations proc_mca_operations = {
	NULL, proc_mca_read,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static struct inode_operations proc_mca_inode_operations = {
	&proc_mca_operations,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
#endif

/*--------------------------------------------------------------------*/

long mca_init(long memory_start, long memory_end)
{
	unsigned int  i, j;
	int foundscsi = 0;

	/* WARNING: Be careful when making changes here. Putting an adapter
	 * and the motherboard simultaneously into setup mode may result in 
	 * damage to chips (according to The Indispensible PC Hardware Book 
	 * by Hans-Peter Messmer). Also, we disable system interrupts (so  	
	 * that we are not disturbed in the middle of this).
	*/

	/*
	 *	Make sure the MCA bus is present
	 */
	
	if (!MCA_bus)
		return memory_start;
	cli();

	/*
	 *	Allocate MCA_info structure (at address divisible by 8)
	 */ 

	if( ((memory_start+7)&(~7)) > memory_end ) 
	{
	  	/* uh oh */
	  	return memory_start;
	}

	mca_info = (struct MCA_info*) ((memory_start+7)&(~7));
	memory_start = ((long)mca_info) + sizeof(struct MCA_info);

	/*
	 *	Make sure adapter setup is off
	 */

	outb_p(0, MCA_ADAPTER_SETUP_REG);

	/*
	 *	Put motherboard into video setup mode, read integrated video 
	 * pos registers, and turn motherboard setup off.
	 */

	outb_p(0xdf, MCA_MOTHERBOARD_SETUP_REG);
	mca_info->slot[MCA_INTEGVIDEO].name[0] = 0;
	for (j=0; j<8; j++) {
		mca_info->slot[MCA_INTEGVIDEO].pos[j] = inb_p(MCA_POS_REG(j)); 
	}

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
	  		broken, but a device is probably there.  However, if you get
	  		0x00 from a motherboard register it won't matter what we
	  		find.  For the record, on the 57SLC, the integrated SCSI
	  		adapter has 0xffff for the adapter ID, but nonzero for
	  		other registers.  */
			foundscsi = 1;
		}
	}
	if( !foundscsi ) 
	{
		/* 
		 *	Didn't find it at 0xfd, try somewhere else... 
		 */
		outb_p(0xfd, MCA_MOTHERBOARD_SETUP_REG);
		for (j=0; j<8; j++) 
			mca_info->slot[MCA_INTEGSCSI].pos[j] = inb_p(MCA_POS_REG(j)); 
	}

	/* turn off motherboard setup */
	outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);

	/*
	 *	Now loop over MCA slots: put each adapter into setup mode, and
	 *	read its pos registers. Then put adapter setup off.
	 */

	for (i=0; i<MCA_MAX_SLOT_NR; i++) {
		outb_p(0x8|(i&0xf), MCA_ADAPTER_SETUP_REG);
		for (j=0; j<8; j++)  mca_info->slot[i].pos[j]=inb_p(MCA_POS_REG(j)); 
		mca_info->slot[i].name[0] = 0;
	}
	outb_p(0, MCA_ADAPTER_SETUP_REG);

	/*
	 *	Enable interrupts and return memory start
	 */
	sti();

	request_region(0x60,0x01,"system control port B (MCA)");
	request_region(0x90,0x01,"arbitration (MCA)");
	request_region(0x91,0x01,"card Select Feedback (MCA)");
	request_region(0x92,0x01,"system Control port A (MCA)");
	request_region(0x94,0x01,"system board setup (MCA)");
	request_region(0x96,0x02,"POS (MCA)");
	request_region(0x100,0x08,"POS (MCA)");

#ifdef CONFIG_PROC_FS
	memory_start = mca_do_proc_init( memory_start, memory_end );
#endif

	return memory_start;
}

/*--------------------------------------------------------------------*/

int mca_find_adapter( int id, int start ) 
{
	int slot_id = 0;
	unsigned char status = 0;

	if( mca_info == 0 || id == 0 || id == 0xffff ) {
		return MCA_NOTFOUND;
	}

	for( ; start >= 0 && start < MCA_NUMADAPTERS; start += 1 ) {
		slot_id = (mca_info->slot[start].pos[1] << 8)
			+ mca_info->slot[start].pos[0];
		status = mca_info->slot[start].pos[2];

		/* not sure about this.  There's no point in returning
		adapters that aren't enabled, since they can't actually
		be used.  However, they might be needed for statistical
		purposes or something... */
		if( !(status & MCA_ENABLED) ) {
			continue;
		}

		if( id == slot_id ) {
			return start;
		}
	}

	return MCA_NOTFOUND;
} /* mca_find_adapter() */

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

	if( slot < 0 || slot >= MCA_MAX_SLOT_NR || mca_info == 0 ) return 0;
	if( reg < 0 || reg >= 8 ) return 0;

	cli();

	/*make sure motherboard setup is off*/
	outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);

	/* read in the appropriate register */
	outb_p(0x8|(slot&0xf), MCA_ADAPTER_SETUP_REG);
	byte = inb_p(MCA_POS_REG(reg)); 
	outb_p(0, MCA_ADAPTER_SETUP_REG);

	sti();

	/* make sure the stored values are consistent, while we're here */
	mca_info->slot[slot].pos[reg] = byte;

	return byte;
} /* mca_read_pos() */

/*--------------------------------------------------------------------*/
/* Note that this a technically a Bad Thing, as IBM tech stuff says
	you should only set POS values through their utilities.
	However, some devices such as the 3c523 recommend that you write
	back some data to make sure the configuration is consistent.
	I'd say that IBM is right, but I like my drivers to work.
	This function can't do checks to see if multiple devices end up
	with the same resources, so you might see magic smoke if someone
	screws up.  */

void mca_write_pos( int slot, int reg, unsigned char byte ) 
{
	if( slot < 0 || slot >= MCA_MAX_SLOT_NR ) return;
	if( reg < 0 || reg >= 8 ) return;
	if (mca_info == 0 )  return;

	cli();

	/*make sure motherboard setup is off*/
	outb_p(0xff, MCA_MOTHERBOARD_SETUP_REG);

	/* read in the appropriate register */
	outb_p(0x8|(slot&0xf), MCA_ADAPTER_SETUP_REG);
	outb_p( byte, MCA_POS_REG(reg) );
	outb_p(0, MCA_ADAPTER_SETUP_REG);

	sti();

	/* update the global register list, while we have the byte */
	mca_info->slot[slot].pos[reg] = byte;
} /* mca_write_pos() */

/*--------------------------------------------------------------------*/

void mca_set_adapter_name( int slot, char* name ) 
{
	if( mca_info == 0 ) return;

	if( slot >= 0 && slot < MCA_NUMADAPTERS ) {
		strncpy( mca_info->slot[slot].name, name,
			sizeof(mca_info->slot[slot].name) );
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

	if( slot >= MCA_MAX_SLOT_NR ) {
		/* some integrated adapters have 0xffff for an ID, but
		are still there. VGA, for example. */
		int i;
		for( i = 0; i < 8; i ++ ) {
			if( mca_info->slot[slot].pos[i] != 0xff ) {
				return 1;
			}
		}
		return 0;
	} else if( slot >= 0 && slot < MCA_NUMADAPTERS ) {
		return (mca_info->slot[slot].pos[0] != 0xff ||
			mca_info->slot[slot].pos[1] != 0xff);
	}

	return 0;
}

int mca_isenabled( int slot )
{
	if( mca_info == 0 ) return 0;

	if( slot >= 0 && slot < MCA_NUMADAPTERS ) {
		return (mca_info->slot[slot].pos[2] & MCA_ENABLED);
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
		/*
		 *	Format pos registers of eight MCA slots
		 */
		for (i=0; i<MCA_MAX_SLOT_NR; i++) 
		{
			len += sprintf(buf+len, "Slot %d: ", i+1);
			for (j=0; j<8; j++) 
				len += sprintf(buf+len, "%02x ", mca_info->slot[i].pos[j]);
			len += sprintf( buf+len, " %s\n", mca_info->slot[i].name );
		}    

		/*
		 *	Format pos registers of integrated video subsystem
		 */

		len += sprintf(buf+len, "Video: ");
		for (j=0; j<8; j++) 
			len += sprintf(buf+len, "%02x ", mca_info->slot[MCA_INTEGVIDEO].pos[j]);
		len += sprintf( buf+len, " %s\n", mca_info->slot[MCA_INTEGVIDEO].name );

		/*
		 *	Format pos registers of integrated SCSI subsystem
		 */
	
		len += sprintf(buf+len, "SCSI: ");
		for (j=0; j<8; j++) 
			len += sprintf(buf+len, "%02x ", mca_info->slot[MCA_INTEGSCSI].pos[j]);
		len += sprintf( buf+len, " %s\n", mca_info->slot[MCA_INTEGSCSI].name );
	} 
	else 
	{
	  	/* 
	  	 *	Leave it empty if MCA not detected
		 *	this should never happen 
		 */
	}

	return len;
}


/*--------------------------------------------------------------------*/
long mca_do_proc_init( long memory_start, long memory_end )
{
	int i = 0;
	struct proc_dir_entry* node = 0;

	if( mca_info == 0 ) return memory_start;	/* never happens */

	proc_register( &proc_mca, &(struct proc_dir_entry) {
		PROC_MCA_REGISTERS, 3, "pos", S_IFREG|S_IRUGO,
		1, 0, 0, 0, &proc_mca_inode_operations,} );

	proc_register( &proc_mca, &(struct proc_dir_entry) {
		PROC_MCA_MACHINE, 7, "machine", S_IFREG|S_IRUGO,
		1, 0, 0, 0, &proc_mca_inode_operations,} );

	/* initialize /proc entries for existing adapters */
	for( i = 0; i < MCA_NUMADAPTERS; i += 1 ) {
		mca_info->slot[i].procfn = 0;
		mca_info->slot[i].dev = 0;

		if( ! mca_isadapter( i ) ) continue;
		if( memory_start + sizeof(struct proc_dir_entry) > memory_end ) {
			continue;
		}
		node = (struct proc_dir_entry*) memory_start;
		memory_start += sizeof(struct proc_dir_entry);

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

	return memory_start;
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

/*
static int mca_not_implemented( char* buf ) 
{
	return sprintf( buf, "Sorry, not implemented yet...\n" );
}
*/

static int mca_fill( char* page, int pid, int type, char** start,
	off_t offset, int length)
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

/*
 *	Blatantly stolen from fs/proc/array.c, and thus is probably overkill 
 */

#define PROC_BLOCK_SIZE	(3*1024)

long proc_mca_read( struct inode* inode, struct file* file,
	char* buf, unsigned long count)
{
	unsigned long page;
	char *start;
	int length;
	int end;
	unsigned int type, pid;
	struct proc_dir_entry *dp;

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
			    &start, file->f_pos, count);
	if (length < 0) {
		free_page(page);
		return length;
	}
	if (start != 0) {
		/* We have had block-adjusting processing! */
		copy_to_user(buf, start, length);
		file->f_pos += length;
		count = length;
	} else {
		/* Static 4kB (or whatever) block capacity */
		if (file->f_pos >= length) {
			free_page(page);
			return 0;
		}
		if (count + file->f_pos > length)
			count = length - file->f_pos;
		end = count + file->f_pos;
		copy_to_user(buf, (char *) page + file->f_pos, count);
		file->f_pos = end;
	}
	free_page(page);
	return count;
} /* proc_mca_read() */

#endif

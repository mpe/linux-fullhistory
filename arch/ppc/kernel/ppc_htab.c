/*
 * $Id: ppc_htab.c,v 1.16 1997/11/17 18:25:04 cort Exp $
 *
 * PowerPC hash table management proc entry.  Will show information
 * about the current hash table and will allow changes to it.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>

static ssize_t ppc_htab_read(struct file * file, char * buf,
			     size_t count, loff_t *ppos);
static ssize_t ppc_htab_write(struct file * file, const char * buffer,
			      size_t count, loff_t *ppos);
static long long ppc_htab_lseek(struct file * file, loff_t offset, int orig);

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern unsigned long _SDR1;
extern unsigned long htab_reloads;
extern unsigned long htab_evicts;
extern unsigned long pte_misses;
extern unsigned long pte_errors;

static struct file_operations ppc_htab_operations = {
    ppc_htab_lseek,	/* lseek   */
    ppc_htab_read,	/* read	   */
    ppc_htab_write,	/* write   */
    NULL,		/* readdir */
    NULL,		/* poll    */
    NULL,		/* ioctl   */
    NULL,		/* mmap	   */
    NULL,		/* no special open code	   */
    NULL,		/* no special release code */
    NULL		/* can't fsync */
};

/*
 * proc files can do almost nothing..
 */
struct inode_operations proc_ppc_htab_inode_operations = {
    &ppc_htab_operations,  /* default proc file-ops */
    NULL,	    /* create	   */
    NULL,	    /* lookup	   */
    NULL,	    /* link	   */
    NULL,	    /* unlink	   */
    NULL,	    /* symlink	   */
    NULL,	    /* mkdir	   */
    NULL,	    /* rmdir	   */
    NULL,	    /* mknod	   */
    NULL,	    /* rename	   */
    NULL,	    /* readlink	   */
    NULL,	    /* follow_link */
    NULL,	    /* readpage	   */
    NULL,	    /* writepage   */
    NULL,	    /* bmap	   */
    NULL,	    /* truncate	   */
    NULL	    /* permission  */
};

/* these will go into processor.h when I'm done debugging -- Cort */
#define MMCR0 952
#define MMCR0_PMC1_CYCLES (0x1<<7)
#define MMCR0_PMC1_ICACHEMISS (0x5<<7)
#define MMCR0_PMC1_DTLB (0x6<<7)
#define MMCR0_PMC2_DCACHEMISS (0x6)
#define MMCR0_PMC2_CYCLES (0x1)
#define MMCR0_PMC2_ITLB (0x7)
#define MMCR0_PMC2_LOADMISSTIME (0x5)

#define PMC1 953
#define PMC2 954

char *pmc1_lookup(unsigned long mmcr0)
{
	switch ( mmcr0 & (0x7f<<7) )
	{
	case 0x0:
		return "none";
	case MMCR0_PMC1_CYCLES:
		return "cycles";
	case MMCR0_PMC1_ICACHEMISS:
		return "ic miss";
	case MMCR0_PMC1_DTLB:
		return "dtlb miss";
	default:
		return "unknown";
	}
}	

char *pmc2_lookup(unsigned long mmcr0)
{
	switch ( mmcr0 & 0x3f )
	{
	case 0x0:
		return "none";
	case MMCR0_PMC2_CYCLES:
		return "cycles";
	case MMCR0_PMC2_DCACHEMISS:
		return "dc miss";
	case MMCR0_PMC2_ITLB:
		return "itlb miss";
	case MMCR0_PMC2_LOADMISSTIME:
		return "load miss time";
	default:
		return "unknown";
	}
}	


/*
 * print some useful info about the hash table.  This function
 * is _REALLY_ slow (see the nested for loops below) but nothing
 * in here should be really timing critical. -- Cort
 */
static ssize_t ppc_htab_read(struct file * file, char * buf,
			     size_t count, loff_t *ppos)
{
	unsigned long mmcr0 = 0, pmc1 = 0, pmc2 = 0;
	int n = 0, valid;
	unsigned int kptes = 0, overflow = 0, uptes = 0, zombie_ptes = 0;
	PTE *ptr;
	struct task_struct *p;
	char buffer[512];

	if (count < 0)
		return -EINVAL;

	switch ( _get_PVR()>>16 )
	{
	case 4:  /* 604 */
	case 9:  /* 604e */
	case 10: /* 604ev5 */
		asm volatile ("mfspr %0,952 \n\t"
		    "mfspr %1,953 \n\t"
		    "mfspr %2,954 \n\t"
		    : "=r" (mmcr0), "=r" (pmc1), "=r" (pmc2) );
		n += sprintf( buffer + n,
			      "604 Performance Monitoring\n"
			      "MMCR0\t\t: %08lx %s%s ",
			      mmcr0,
			      ( mmcr0>>28 & 0x2 ) ? "(user mode counted)" : "",
			      ( mmcr0>>28 & 0x4 ) ? "(kernel mode counted)" : "");
		n += sprintf( buffer + n,
			      "\nPMC1\t\t: %08lx (%s)\n"
			      "PMC2\t\t: %08lx (%s)\n",
			      pmc1, pmc1_lookup(mmcr0),
			      pmc2, pmc2_lookup(mmcr0));
		break;
	default:
		break;
	}

	
	/* if we don't have a htab */
	if ( Hash_size == 0 )
	{
		n += sprintf( buffer + n, "No Hash Table used\n");
		goto return_string;
	}
	
	/*
	 * compute user/kernel pte's table this info can be
	 * misleading since there can be valid (v bit set) entries
	 * in the table but their vsid is used by no process (mm->context)
	 * due to the way tlb invalidation is handled on the ppc
	 * -- Cort
	 */
	for ( ptr = Hash ; ptr < Hash_end ; ptr++)
	{
		if (ptr->v)
		{
			/* make sure someone is using this context/vsid */
			valid = 0;
			for_each_task(p)
			{
				if ( (ptr->vsid >> 4) == p->mm->context )
				{
					valid = 1;
					break;
				}
			}
			if ( !valid )
			{
				zombie_ptes++;
				continue;
			}
			/* user not allowed read or write */
			if (ptr->pp == PP_RWXX)
				kptes++;
			else
				uptes++;
			if (ptr->h == 1)
				overflow++;
		}
	}
	
	n += sprintf( buffer + n,
		      "PTE Hash Table Information\n"
		      "Size\t\t: %luKb\n"
		      "Buckets\t\t: %lu\n"
 		      "Address\t\t: %08lx\n"
		      "Entries\t\t: %lu\n"
		      "User ptes\t: %u\n"
		      "Kernel ptes\t: %u\n"
		      "Overflows\t: %u\n"
		      "Zombies\t\t: %u\n"
		      "Percent full\t: %%%lu\n",
                      (unsigned long)(Hash_size>>10),
		      (Hash_size/(sizeof(PTE)*8)),
		      (unsigned long)Hash,
		      Hash_size/sizeof(PTE),
                      uptes,
		      kptes,
		      overflow,
		      zombie_ptes,
		      ((kptes+uptes)*100) / (Hash_size/sizeof(PTE))
		);

	n += sprintf( buffer + n,
		      "Reloads\t\t: %08lx\n"
		      "Evicts\t\t: %08lx\n"
		      "Non-error misses: %08lx\n"
		      "Error misses\t: %08lx\n",
		      htab_reloads, htab_evicts, pte_misses, pte_errors);
	
return_string:
	if (*ppos >= strlen(buffer))
		return 0;
	if (n > strlen(buffer) - *ppos)
		n = strlen(buffer) - *ppos;
	copy_to_user(buf, buffer + *ppos, n);
	*ppos += n;
	return n;
}

/*
 * Allow user to define performance counters and resize the hash table
 */
static ssize_t ppc_htab_write(struct file * file, const char * buffer,
			      size_t count, loff_t *ppos)
{
	unsigned long tmp;
	if ( current->uid != 0 )
		return -EACCES;
	/* don't set the htab size for now */
	if ( !strncmp( buffer, "size ", 5) )
		return -EBUSY;

	/* turn off performance monitoring */
	if ( !strncmp( buffer, "off", 3) )
	{
		switch ( _get_PVR()>>16 )
		{
		case 4:  /* 604 */
		case 9:  /* 604e */
		case 10: /* 604ev5 */
			asm volatile ("mtspr %0, %3 \n\t"
			    "mtspr %1, %3 \n\t"
			    "mtspr %2, %3 \n\t"			    
			    :: "i" (MMCR0), "i" (PMC1), "i" (PMC2), "r" (0));
			break;
		default:
			break;
		}
			
	}

	if ( !strncmp( buffer, "reset", 5) )
	{
		switch ( _get_PVR()>>16 )
		{
		case 4:  /* 604 */
		case 9:  /* 604e */
		case 10: /* 604ev5 */
			/* reset PMC1 and PMC2 */
			asm volatile (
				"mtspr 953, %0 \n\t"
				"mtspr 954, %0 \n\t"
				:: "r" (0));
			break;
		default:
			break;
		}
		htab_reloads = 0;
		htab_evicts = 0;
		pte_misses = 0;
		pte_errors = 0;
	}

	if ( !strncmp( buffer, "user", 4) )
	{
		switch ( _get_PVR()>>16 )
		{
		case 4:  /* 604 */
		case 9:  /* 604e */
		case 10: /* 604ev5 */
			/* setup mmcr0 and clear the correct pmc */
			asm("mfspr %0,%1\n\t"  : "=r" (tmp) : "i" (MMCR0));
			tmp &= ~(0x60000000);
			tmp |= 0x20000000;
			asm volatile (
				"mtspr %1,%0 \n\t"    /* set new mccr0 */
				"mtspr %3,%4 \n\t"    /* reset the pmc */
				"mtspr %5,%4 \n\t"    /* reset the pmc2 */
				:: "r" (tmp), "i" (MMCR0), "i" (0),
				"i" (PMC1),  "r" (0), "i"(PMC2) );
			break;
		default:
			break;
		}
	}

	if ( !strncmp( buffer, "kernel", 6) )
	{
		switch ( _get_PVR()>>16 )
		{
		case 4:  /* 604 */
		case 9:  /* 604e */
		case 10: /* 604ev5 */
			/* setup mmcr0 and clear the correct pmc */
			asm("mfspr %0,%1\n\t"  : "=r" (tmp) : "i" (MMCR0));
			tmp &= ~(0x60000000);
			tmp |= 0x40000000;
			asm volatile (
				"mtspr %1,%0 \n\t"    /* set new mccr0 */
				"mtspr %3,%4 \n\t"    /* reset the pmc */
				"mtspr %5,%4 \n\t"    /* reset the pmc2 */
				:: "r" (tmp), "i" (MMCR0), "i" (0),
				"i" (PMC1),  "r" (0), "i"(PMC2) );
			break;
		default:
			break;
		}
	}
	
	/* PMC1 values */
	if ( !strncmp( buffer, "dtlb", 4) )
	{
		switch ( _get_PVR()>>16 )
		{
		case 4:  /* 604 */
		case 9:  /* 604e */
		case 10: /* 604ev5 */
			/* setup mmcr0 and clear the correct pmc */
			asm("mfspr %0,%1\n\t"  : "=r" (tmp) : "i" (MMCR0));
			tmp &= ~(0x7f<<7);
			tmp |= MMCR0_PMC1_DTLB;
			asm volatile (
				"mtspr %1,%0 \n\t"    /* set new mccr0 */
				"mtspr %3,%4 \n\t"    /* reset the pmc */
				:: "r" (tmp), "i" (MMCR0), "i" (MMCR0_PMC1_DTLB),
				"i" (PMC1),  "r" (0) );
		}
	}	

	if ( !strncmp( buffer, "ic miss", 7) )
	{
		switch ( _get_PVR()>>16 )
		{
		case 4:  /* 604 */
		case 9:  /* 604e */
		case 10: /* 604ev5 */
			/* setup mmcr0 and clear the correct pmc */
			asm("mfspr %0,%1\n\t"  : "=r" (tmp) : "i" (MMCR0));
			tmp &= ~(0x7f<<7);
			tmp |= MMCR0_PMC1_ICACHEMISS;
			asm volatile (
				"mtspr %1,%0 \n\t"    /* set new mccr0 */
				"mtspr %3,%4 \n\t"    /* reset the pmc */
				:: "r" (tmp), "i" (MMCR0),
				"i" (MMCR0_PMC1_ICACHEMISS), "i" (PMC1),  "r" (0));
		}
	}	

	/* PMC2 values */
	if ( !strncmp( buffer, "load miss time", 14) )
	{
		switch ( _get_PVR()>>16 )
		{
		case 4:  /* 604 */
		case 9:  /* 604e */
		case 10: /* 604ev5 */
			/* setup mmcr0 and clear the correct pmc */
		       asm volatile(
			       "mfspr %0,%1\n\t"     /* get current mccr0 */
			       "rlwinm %0,%0,0,0,31-6\n\t"  /* clear bits [26-31] */
			       "ori   %0,%0,%2 \n\t" /* or in mmcr0 settings */
			       "mtspr %1,%0 \n\t"    /* set new mccr0 */
			       "mtspr %3,%4 \n\t"    /* reset the pmc */
			       : "=r" (tmp)
			       : "i" (MMCR0), "i" (MMCR0_PMC2_LOADMISSTIME),
			       "i" (PMC2),  "r" (0) );
		}
	}
	
	if ( !strncmp( buffer, "itlb", 4) )
	{
		switch ( _get_PVR()>>16 )
		{
		case 4:  /* 604 */
		case 9:  /* 604e */
		case 10: /* 604ev5 */
			/* setup mmcr0 and clear the correct pmc */
		       asm volatile(
			       "mfspr %0,%1\n\t"     /* get current mccr0 */
			       "rlwinm %0,%0,0,0,31-6\n\t"  /* clear bits [26-31] */
			       "ori   %0,%0,%2 \n\t" /* or in mmcr0 settings */
			       "mtspr %1,%0 \n\t"    /* set new mccr0 */
			       "mtspr %3,%4 \n\t"    /* reset the pmc */
			       : "=r" (tmp)
			       : "i" (MMCR0), "i" (MMCR0_PMC2_ITLB),
			       "i" (PMC2),  "r" (0) );
		}
	}

	if ( !strncmp( buffer, "dc miss", 7) )
	{
		switch ( _get_PVR()>>16 )
		{
		case 4:  /* 604 */
		case 9:  /* 604e */
		case 10: /* 604ev5 */
			/* setup mmcr0 and clear the correct pmc */
		       asm volatile(
			       "mfspr %0,%1\n\t"     /* get current mccr0 */
			       "rlwinm %0,%0,0,0,31-6\n\t"  /* clear bits [26-31] */
			       "ori   %0,%0,%2 \n\t" /* or in mmcr0 settings */
			       "mtspr %1,%0 \n\t"    /* set new mccr0 */
			       "mtspr %3,%4 \n\t"    /* reset the pmc */
			       : "=r" (tmp)
			       : "i" (MMCR0), "i" (MMCR0_PMC2_DCACHEMISS),
			       "i" (PMC2),  "r" (0) );
		}
	}	
	

	return count;
	
#if 0 /* resizing htab is a bit difficult right now -- Cort */
	unsigned long size;
	extern void reset_SDR1(void);
	
	/* only know how to set size right now */
	if ( strncmp( buffer, "size ", 5) )
		return -EINVAL;

	size = simple_strtoul( &buffer[5], NULL, 10 );
	
	/* only allow to shrink */
	if ( size >= Hash_size>>10 )
		return -EINVAL;

	/* minimum size of htab */
	if ( size < 64 )
		return -EINVAL;
	
	/* make sure it's a multiple of 64k */
	if ( size % 64 )
		return -EINVAL;
	
	printk("Hash table resize to %luk\n", size);
	/*
	 * We need to rehash all kernel entries for the new htab size.
	 * Kernel only since we do a flush_tlb_all().  Since it's kernel
	 * we only need to bother with vsids 0-15.  To avoid problems of
	 * clobbering un-rehashed values we put the htab at a new spot
	 * and put everything there.
	 * -- Cort
	 */
	Hash_size = size<<10;
	Hash_mask = (Hash_size >> 6) - 1;
        _SDR1 = __pa(Hash) | (Hash_mask >> 10);
	flush_tlb_all();

	reset_SDR1();
#endif	
	return count;
}


static long long
ppc_htab_lseek(struct file * file, loff_t offset, int orig)
{
    switch (orig) {
    case 0:
	file->f_pos = offset;
	return(file->f_pos);
    case 1:
	file->f_pos += offset;
	return(file->f_pos);
    case 2:
	return(-EINVAL);
    default:
	return(-EINVAL);
    }
}


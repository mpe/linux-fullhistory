/*
 * arch/mips/dec/decstation.c
 *
 * Copyright (C) 1996 Paul M. Antoine
 *
 * Written by Paul Antoine.
 *
 * FIXME: still plenty to do in this file, as we don't yet fully fill
 *        the boot info structure with DEC-specific tags.  Also still
 *	  too specific to the Person Decstattion 5000/2x!!
 */

#include <asm/dec/decstation.h>
#include <asm/dec/maxine.h>	/* FIXME: what about other decstations? */
#include <asm/bootinfo.h>

/*
 * dec_setup: Called at boot from dec_entry() in boot.S to do 
 *            DECStation-specific setup, and to fill in the kernel argument
 *            tags.
 *
 * FIXME: I'm not sure all DEC workstations are correctly supported.  This
 *        code may not need to be here when booting off floppy or HD??
 */

unsigned long mach_mem_upper = 0;
unsigned long mach_mem_lower = 0;
unsigned long mips_dcache_size = 0;
unsigned long mips_icache_size = 0;
unsigned long rex_prom_magic; /* from boot.S */
unsigned long dec_get_memory_size(void);

void dec_setup(void)
{
    unsigned long int mem_mask = 0;
    unsigned long tag_data_dummy, dec_sysid;
    unsigned char dec_cpunum, dec_systype, dec_firmrev, dec_etc;
    extern const char *linux_banner;

    pmax_printf("%s\n", linux_banner);
    /* First we need the memory upper bound before we can add tag entries... */
    mach_mem_lower = 0x80000000L;
    mach_mem_upper = mach_mem_lower + dec_get_memory_size();

    /* First tag is always memory upper limit, right Stoned?? */
    (void)bi_TagAdd(tag_memupper, ULONGSIZE, &mach_mem_upper);

    /* We're obviously one of the DEC machines */
    tag_data_dummy = MACH_GROUP_DEC;
    (void)bi_TagAdd(tag_machgroup, ULONGSIZE, &tag_data_dummy);

    /* Now let's try to figure out what type of DECStation we are */
    pmax_printf("System id is: ");
    if ((dec_sysid = pmax_getsysid()) != 0)
      pmax_printf("%x\n", dec_sysid);
    else
      pmax_printf("unknown\n");

    dec_cpunum = (dec_sysid & 0xff000000) >> 24;
    dec_systype = (dec_sysid & 0xff0000) >> 16;
    dec_firmrev = (dec_sysid & 0xff00) >> 8;
    dec_etc = dec_sysid & 0xff;

    /*
     * FIXME: for now use the PROM to determine the CPU type - should
     *        probably just get the CPU to tell us.
     */
    pmax_printf("System has an ");
    switch(dec_cpunum)
    {
	case 0x82:
	    {
	        pmax_printf("R3000 CPU\n");
		tag_data_dummy = CPU_R3000A;
		break;
	    }
	case 0x84:
	    {
	        pmax_printf("R4000 CPU\n");
		/* FIXME: assume a plain R4000PC for now */
		tag_data_dummy = CPU_R4000PC;
		break;
	    }
    	default:
	    {
	        pmax_printf("unknown CPU, code is %x\n", dec_cpunum);
		/* FIXME: assume an R2000 for now */
		tag_data_dummy = CPU_R2000;
		break;
	    }
    }
    /* Add the CPU type */
    (void)bi_TagAdd(tag_cputype, ULONGSIZE, &tag_data_dummy);

    pmax_printf("System has firmware type: ");
    if (dec_firmrev == 2)
      pmax_printf("TCF0\n");
    else
      pmax_printf("TCF1\n");

    pmax_printf("This DECStation is a: ");
    switch(dec_systype) {
    case 1: /* DS2100/3100 Pmax */
            pmax_printf("DS2100/3100\n");
	    tag_data_dummy = MACH_DECSTATION;
	    break;
    case 2: /* DS5000 3max */
            pmax_printf("DS5000\n");
	    tag_data_dummy = MACH_DECSTATION;
	    break;
    case 3: /* DS5000/100 3min */
            pmax_printf("DS5000/1x0\n");
	    tag_data_dummy = MACH_DECSTATION;
	    break;
    case 7: /* Personal DS5000/2x */
            pmax_printf("Personal DS5000/2x\n");
	    tag_data_dummy = MACH_DECSTATION;
	    break;
    default:
            pmax_printf("unknown, id is: %x\n", dec_systype);
	    tag_data_dummy = MACH_UNKNOWN;
	    break;
    }

    /* Add the machine type */
    (void)bi_TagAdd(tag_machtype, ULONGSIZE, &tag_data_dummy);

    /* Add the number of tlb entries */
    tag_data_dummy = 64;
    (void)bi_TagAdd(tag_tlb_entries, ULONGSIZE, &tag_data_dummy);

    /*
     * Add the instruction cache size
     * FIXME: should determine this somehow
     */
    tag_data_dummy = 0x100000;		/* set it to 64K for now */
    (void)bi_TagAdd(tag_icache_size, ULONGSIZE, &tag_data_dummy);
    mips_icache_size = tag_data_dummy;

    /*
     * Add the data cache size
     * FIXME: should determine this somehow
     */
    tag_data_dummy = 0x100000;		/* set it to 64K for now */
    (void)bi_TagAdd(tag_dcache_size, ULONGSIZE, &tag_data_dummy);
    mips_dcache_size = tag_data_dummy;

    /* FIXME: should determine vram_base properly */
    tag_data_dummy = 0xa8000000;
    (void)bi_TagAdd(tag_vram_base, ULONGSIZE, &tag_data_dummy);

    /* FIXME: dummy drive info tag */
    tag_data_dummy = 0;
    (void)bi_TagAdd(tag_drive_info, ULONGSIZE, &tag_data_dummy);

    /* FIXME: do we need a dummy tag at the end? */
    tag_data_dummy = 0;
    (void)bi_TagAdd(tag_dummy, 0, &tag_data_dummy);

    pmax_printf("Added tags\n");
} /* dec_setup */

unsigned long dec_get_memory_size()
{
    int i, bitmap_size;
    unsigned long mem_size = 0;
    struct pmax_bitmap {
      int		pagesize;
      unsigned char	bitmap[64*1024*1024 - 4];
    } *bm;

    /* some free 64k */
    bm = (struct pmax_bitmap *)0x8002f000;
    bitmap_size = pmax_getbitmap(bm);

    pmax_printf("Page size is: %x\n", bm->pagesize);
    pmax_printf("Bitmap size is: %d bytes\n", bitmap_size);

    for (i = 0; i < bitmap_size; i++)
    {
      /* FIXME: very simplistically only add full sets of pages */
      if (bm->bitmap[i] == 0xff)
	  mem_size += (8 * bm->pagesize);
    }
    pmax_printf("Main memory size is: %d KB\n", (mem_size / 1024));
    return(mem_size);
} /* dec_get_memory_size */

unsigned char maxine_rtc_read_data(unsigned long addr)
{
    char *rtc = (char *)(PMAX_RTC_BASE);
    return(rtc[addr * 4]);
} /* maxine_rtc_read_data */

void maxine_rtc_write_data(unsigned char data, unsigned long addr)
{
    char *rtc = (char *)(PMAX_RTC_BASE);
    rtc[addr * 4] = data;
} /* maxine_rtc_read_data */


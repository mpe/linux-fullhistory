
#define MAX_SCSI_DEVICE_CODE 10
const char *const scsi_dev_types[MAX_SCSI_DEVICE_CODE] =
{
    "Direct-Access    ",
    "Sequential-Access",
    "Printer	      ",
    "Processor	      ",
    "WORM	      ",
    "CD-ROM	      ",
    "Scanner	      ",
    "Optical Device   ",
    "Medium Changer   ",
    "Communications   "
};


void swap_statistics(u8 *p)
{
    u32 y;
    u32 *lp, h_lp;
    u16 *sp, h_sp;
    u8 *bp;
    
    lp = (u32 *)p;
    sp = ((short *)lp) + 1;	    /* Convert Header */
    h_sp = *sp = ntohs(*sp);
    lp++;

    do {
	sp = (u16 *)lp;		  /* Convert SubHeader */
    *sp = ntohs(*sp);
    bp = (u8 *) lp;
    y = *(bp + 3);
    lp++;
    for (h_lp = (u32)lp; (u32)lp < h_lp + ((u32)*(bp + 3)); lp++)
	*lp = ntohl(*lp);
    }while ((u32)lp < ((u32)p) + 4 + h_sp);

}

/*
 * eata_set_info
 * buffer : pointer to the data that has been written to the hostfile
 * length : number of bytes written to the hostfile
 * HBA_ptr: pointer to the Scsi_Host struct
 */
int eata_set_info(char *buffer, int length, struct Scsi_Host *HBA_ptr)
{
    if (length >= 8 && strncmp(buffer, "eata_dma", 8) == 0) {
        buffer += 9;
        length -= 9;
        if(length >= 8 && strncmp(buffer, "latency", 7) == 0) {
            SD(HBA_ptr)->do_latency = TRUE;
            return(length+9);
        } 
        
        if(length >=10 && strncmp(buffer, "nolatency", 9) == 0) {
            SD(HBA_ptr)->do_latency = FALSE;
            return(length+9);
        } 
        
        printk("Unknown command:%s length: %d\n", buffer, length);
    } else 
        printk("Wrong Signature:%10s\n", (char *) ((ulong)buffer-9));
    
    return(-EINVAL);
}

/*
 * eata_proc_info
 * inout : decides on the direction of the dataflow and the meaning of the 
 *         variables
 * buffer: If inout==FALSE data is beeing written to it else read from it
 * *start: If inout==FALSE start of the valid data in the buffer
 * offset: If inout==FALSE offset from the beginning of the imaginary file 
 *         from which we start writing into the buffer
 * length: If inout==FALSE max number of bytes to be written into the buffer 
 *         else number of bytes in the buffer
 */
int eata_proc_info(char *buffer, char **start, off_t offset, int length, 
                   int hostno, int inout)
{

    Scsi_Device *scd;
    struct Scsi_Host *HBA_ptr;
    Scsi_Cmnd scmd;
    static u8 buff[512];
    static u8 buff2[512];
    hst_cmd_stat *rhcs, *whcs;
    coco	 *cc;
    scsitrans	 *st;
    scsimod	 *sm;
    hobu	 *hb;
    scbu	 *sb;
    boty	 *bt;
    memco	 *mc;
    firm	 *fm;
    subinf	 *si; 
    pcinf	 *pi;
    arrlim	 *al;
    int i, x; 
    int	  size, len = 0;
    off_t begin = 0;
    off_t pos = 0;

    HBA_ptr = first_HBA;
    for (i = 1; i <= registered_HBAs; i++) {
	if (HBA_ptr->host_no == hostno)
	    break;
	HBA_ptr = SD(HBA_ptr)->next;
    }	     

    if(inout == TRUE) /* Has data been writen to the file ? */ 
	return(eata_set_info(buffer, length, HBA_ptr));

    if (offset == 0)
	memset(buff, 0, sizeof(buff));

    cc = (coco *)     (buff + 0x148);
    st = (scsitrans *)(buff + 0x164); 
    sm = (scsimod *)  (buff + 0x16c);
    hb = (hobu *)     (buff + 0x172);
    sb = (scbu *)     (buff + 0x178);
    bt = (boty *)     (buff + 0x17e);
    mc = (memco *)    (buff + 0x186);
    fm = (firm *)     (buff + 0x18e);
    si = (subinf *)   (buff + 0x196); 
    pi = (pcinf *)    (buff + 0x19c);
    al = (arrlim *)   (buff + 0x1a2);

    size = sprintf(buffer+len, "EATA (Extended Attachment) driver version: "
		   "%d.%d%s\n",VER_MAJOR, VER_MINOR, VER_SUB);
    len += size; pos = begin + len;
    size = sprintf(buffer + len, "queued commands:     %10ld\n"
		   "processed interrupts:%10ld\n", queue_counter, int_counter);
    len += size; pos = begin + len;

    size = sprintf(buffer + len, "\nscsi%-2d: HBA %.10s\n",
		   HBA_ptr->host_no, SD(HBA_ptr)->name);
    len += size; 
    pos = begin + len;
    size = sprintf(buffer + len, "Firmware revision: v%s\n", 
		   SD(HBA_ptr)->revision);
    len += size;
    pos = begin + len;
    size = sprintf(buffer + len, "Hardware Configuration:\n");
    len += size; 
    pos = begin + len;
    
    if(SD(HBA_ptr)->bustype == IS_EISA) {
	if (HBA_ptr->dma_channel == 0xff)
	    size = sprintf(buffer + len, "DMA: BUSMASTER\n");
	else
	    size = sprintf(buffer + len, "DMA: %d\n", HBA_ptr->dma_channel);
	len += size; 
	pos = begin + len;

	size = sprintf(buffer + len, "Base IO : %#.4x\n", (u32) HBA_ptr->base);
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "Host Bus: EISA\n"); 
	len += size; 
	pos = begin + len;
    } else {

	scmd.cmnd[0] = LOG_SENSE;
	scmd.cmnd[1] = 0;
	scmd.cmnd[2] = 0x33 + (3<<6);
	scmd.cmnd[3] = 0;
	scmd.cmnd[4] = 0;
	scmd.cmnd[5] = 0;
	scmd.cmnd[6] = 0;
	scmd.cmnd[7] = 0x00;
	scmd.cmnd[8] = 0x66;
	scmd.cmnd[9] = 0;
	scmd.cmd_len = 10;
	
	scmd.host = HBA_ptr; 
	scmd.target = HBA_ptr->this_id; 
	scmd.lun = 0; 
	scmd.channel = 0;
	
	scmd.use_sg = 0;
	scmd.request_bufflen = 0x66;
	scmd.request_buffer = buff + 0x144;
	HBA_interpret = TRUE;
	
	eata_queue(&scmd, (void *) eata_scsi_done);
	while (internal_command_finished == FALSE)
	    barrier();
	
	size = sprintf(buffer + len, "IRQ: %2d, %s triggered\n", cc->interrupt,
		       (cc->intt == TRUE)?"level":"edge");
	len += size; 
	pos = begin + len;
	if (HBA_ptr->dma_channel == 0xff)
	    size = sprintf(buffer + len, "DMA: BUSMASTER\n");
	else
	    size = sprintf(buffer + len, "DMA: %d\n", HBA_ptr->dma_channel);
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "CPU: MC680%02d %dMHz\n", bt->cpu_type,
		       bt->cpu_speed);
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "Base IO : %#.4x\n", (u32) HBA_ptr->base);
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "Host Bus: %s\n", 
		       (SD(HBA_ptr)->bustype == IS_PCI)?"PCI ":
		       (SD(HBA_ptr)->bustype == IS_EISA)?"EISA":"ISA ");
	
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "SCSI Bus:%s%s Speed: %sMB/sec. %s\n",
		       (sb->wide == TRUE)?" WIDE":"", 
		       (sb->dif == TRUE)?" DIFFERENTIAL":"",
		       (sb->speed == 0)?"5":(sb->speed == 1)?"10":"20",
		       (sb->ext == TRUE)?"With external cable detection":"");
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "SCSI channel expansion Module: %s installed\n",
		       (bt->sx1 == TRUE)?"SX1 (one channel)":
		       ((bt->sx2 == TRUE)?"SX2 (two channels)":"not"));
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "SmartRAID hardware: %spresent.\n",
		       (cc->srs == TRUE)?"":"not ");
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "	  Type: %s\n",
		       ((cc->key == TRUE)?((bt->dmi == TRUE)?"integrated"
					   :((bt->dm4 == TRUE)?"DM401X"
					   :(bt->dm4k == TRUE)?"DM4000"
					   :"-"))
					   :"-"));
	len += size; 
	pos = begin + len;
	
	size = sprintf(buffer + len, "	  Max array groups:		 %d\n",
		       (al->code == 0x0e)?al->max_groups:7);
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "	  Max drives per RAID 0 array:	 %d\n",
		       (al->code == 0x0e)?al->raid0_drv:7);
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "	  Max drives per RAID 3/5 array: %d\n",
		       (al->code == 0x0e)?al->raid35_drv:7);
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "Cache Module: %sinstalled.\n",
		       (cc->csh)?"":"not ");
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "	  Type: %s\n",
		       ((cc->csh == TRUE)?((bt->cmi == TRUE)?"integrated"
					 :((bt->cm4 == TRUE)?"CM401X"
					 :((bt->cm4k == TRUE)?"CM4000"
					 :"-")))
					 :"-"));
	len += size; 
	pos = begin + len;
	for (x = 0; x <= 3; x++) {
	    size = sprintf(buffer + len, "    Bank%d: %dMB with%s ECC\n",x,
			   mc->banksize[x] & 0x7f, 
			   (mc->banksize[x] & 0x80)?"":"out");
	    len += size; 
	    pos = begin + len;	    
	}   
	size = sprintf(buffer + len, "Timer Modification: %sinstalled\n",
		       (cc->tmr == TRUE)?"":"not ");
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "NVRAM: %spresent\n",
		       (cc->nvr == TRUE)?"":"not ");
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "SmartROM: %senabled\n",
		       (bt->srom == TRUE)?"not ":"");
	len += size; 
	pos = begin + len;
	size = sprintf(buffer + len, "HBA indicates %salarm.\n",
		       (bt->alrm == TRUE)?"":"no ");
	len += size; 
	pos = begin + len;
	
	if (pos < offset) {
	    len = 0;
	    begin = pos;
	}
	if (pos > offset + length)
	    goto stop_output; 
	
	scmd.cmnd[0] = LOG_SENSE;
	scmd.cmnd[1] = 0;
	scmd.cmnd[2] = 0x32 + (3<<6); 
	scmd.cmnd[3] = 0;
	scmd.cmnd[4] = 0;
	scmd.cmnd[5] = 0;
	scmd.cmnd[6] = 0;
	scmd.cmnd[7] = 0x01;
	scmd.cmnd[8] = 0x44;
	scmd.cmnd[9] = 0;
	scmd.cmd_len = 10;
	scmd.host = HBA_ptr; 
	scmd.target = HBA_ptr->this_id; 
	scmd.lun = 0; 
	scmd.channel = 0;
	scmd.use_sg = 0;
	scmd.request_bufflen = 0x144;
	scmd.request_buffer = buff2;
	HBA_interpret = TRUE;
	
	eata_queue(&scmd, (void *) eata_scsi_done);
	while (internal_command_finished == FALSE)
	    barrier();
	
	swap_statistics(buff2);
	rhcs = (hst_cmd_stat *)(buff2 + 0x2c); 
	whcs = (hst_cmd_stat *)(buff2 + 0x8c);		 
	
	for (x = 0; x <= 11; x++) {
	    SD(HBA_ptr)->reads[x] += rhcs->sizes[x];
	    SD(HBA_ptr)->writes[x] += whcs->sizes[x];
	    SD(HBA_ptr)->reads[12] += rhcs->sizes[x];
	    SD(HBA_ptr)->writes[12] += whcs->sizes[x];
	}
	size = sprintf(buffer + len, "Host Disk Command Statistics:\n"
		       "	 Reads:	     Writes:\n");
	len += size; 
	pos = begin + len;
	for (x = 0; x <= 10; x++) {
	    size = sprintf(buffer+len,"%5dk:%12u %12u\n", 1 << x,
			   SD(HBA_ptr)->reads[x], 
			   SD(HBA_ptr)->writes[x]);
	    len += size; 
	    pos = begin + len;
	}
	size = sprintf(buffer+len,">1024k:%12u %12u\n",
		       SD(HBA_ptr)->reads[11], 
		       SD(HBA_ptr)->writes[11]);
	len += size; 
	pos = begin + len;
	size = sprintf(buffer+len,"Sum   : %12u %12u\n",
		       SD(HBA_ptr)->reads[12], 
		       SD(HBA_ptr)->writes[12]);
	len += size; 
	pos = begin + len;
    }
    
    if (pos < offset) {
	len = 0;
	begin = pos;
    }
    if (pos > offset + length)
	goto stop_output;

    if(SD(HBA_ptr)->do_latency == TRUE) {
	size = sprintf(buffer + len, "Host Latency Command Statistics:\n"
                       "Current timer resolution: 10ms\n"
		       "	 Reads:	      Min:(ms)     Max:(ms)     Ave:(ms)\n");
	len += size; 
	pos = begin + len;
	for (x = 0; x <= 10; x++) {
	    size = sprintf(buffer+len,"%5dk:%12u %12u %12u %12u\n", 
                           1 << x,
			   SD(HBA_ptr)->reads_lat[x][0], 
			   (SD(HBA_ptr)->reads_lat[x][1] == 0xffffffff) 
                           ? 0:(SD(HBA_ptr)->reads_lat[x][1] * 10), 
			   SD(HBA_ptr)->reads_lat[x][2] * 10, 
			   SD(HBA_ptr)->reads_lat[x][3] * 10 /
                           ((SD(HBA_ptr)->reads_lat[x][0])
                            ? SD(HBA_ptr)->reads_lat[x][0]:1));
	    len += size; 
	    pos = begin + len;
	}
	size = sprintf(buffer+len,">1024k:%12u %12u %12u %12u\n",
			   SD(HBA_ptr)->reads_lat[11][0], 
			   (SD(HBA_ptr)->reads_lat[11][1] == 0xffffffff)
                           ? 0:(SD(HBA_ptr)->reads_lat[11][1] * 10), 
			   SD(HBA_ptr)->reads_lat[11][2] * 10, 
			   SD(HBA_ptr)->reads_lat[11][3] * 10 /
                           ((SD(HBA_ptr)->reads_lat[x][0])
                            ? SD(HBA_ptr)->reads_lat[x][0]:1));
	len += size; 
	pos = begin + len;

        if (pos < offset) {
            len = 0;
            begin = pos;
        }
        if (pos > offset + length)
            goto stop_output;

	size = sprintf(buffer + len,
		       "	 Writes:      Min:(ms)     Max:(ms)     Ave:(ms)\n");
	len += size; 
	pos = begin + len;
	for (x = 0; x <= 10; x++) {
	    size = sprintf(buffer+len,"%5dk:%12u %12u %12u %12u\n", 
                           1 << x,
			   SD(HBA_ptr)->writes_lat[x][0], 
			   (SD(HBA_ptr)->writes_lat[x][1] == 0xffffffff)
                           ? 0:(SD(HBA_ptr)->writes_lat[x][1] * 10), 
			   SD(HBA_ptr)->writes_lat[x][2] * 10, 
			   SD(HBA_ptr)->writes_lat[x][3] * 10 /
                           ((SD(HBA_ptr)->writes_lat[x][0])
                            ? SD(HBA_ptr)->writes_lat[x][0]:1));
	    len += size; 
	    pos = begin + len;
	}
	size = sprintf(buffer+len,">1024k:%12u %12u %12u %12u\n",
			   SD(HBA_ptr)->writes_lat[11][0], 
			   (SD(HBA_ptr)->writes_lat[11][1] == 0xffffffff)
                           ? 0:(SD(HBA_ptr)->writes_lat[x][1] * 10), 
			   SD(HBA_ptr)->writes_lat[11][2] * 10, 
			   SD(HBA_ptr)->writes_lat[11][3] * 10/
                           ((SD(HBA_ptr)->writes_lat[x][0])
                            ? SD(HBA_ptr)->writes_lat[x][0]:1));
	len += size; 
	pos = begin + len;

        if (pos < offset) {
            len = 0;
            begin = pos;
        }
        if (pos > offset + length)
            goto stop_output;
    }

    scd = scsi_devices;
    
    size = sprintf(buffer+len,"Attached devices: %s\n", (scd)?"":"none");
    len += size; 
    pos = begin + len;
    
    while (scd) {
	if (scd->host == HBA_ptr) {
	    
	    size = sprintf(buffer + len, 
                           "Channel: %02d Id: %02d Lun: %02d\n  Vendor: ",
			   scd->channel, scd->id, scd->lun);
	    for (x = 0; x < 8; x++) {
		if (scd->vendor[x] >= 0x20)
		    size += sprintf(buffer + len + size, "%c", scd->vendor[x]);
		else
		    size += sprintf(buffer + len + size," ");
	    }
	    size += sprintf(buffer + len + size, " Model: ");
	    for (x = 0; x < 16; x++) {
		if (scd->model[x] >= 0x20)
		    size +=  sprintf(buffer + len + size, "%c", scd->model[x]);
		else
		    size += sprintf(buffer + len + size, " ");
	    }
	    size += sprintf(buffer + len + size, " Rev: ");
	    for (x = 0; x < 4; x++) {
		if (scd->rev[x] >= 0x20)
		    size += sprintf(buffer + len + size, "%c", scd->rev[x]);
		else
		    size += sprintf(buffer + len + size, " ");
	    }
	    size += sprintf(buffer + len + size, "\n");
	    
	    size += sprintf(buffer + len + size, "  Type:   %s ",
			    scd->type < MAX_SCSI_DEVICE_CODE ? 
			    scsi_dev_types[(int)scd->type] : "Unknown	       " );
	    size += sprintf(buffer + len + size, "		 ANSI"
			    " SCSI revision: %02x", (scd->scsi_level < 3)?1:2);
	    if (scd->scsi_level == 2)
		size += sprintf(buffer + len + size, " CCS\n");
	    else
		size += sprintf(buffer + len + size, "\n");
	    len += size; 
	    pos = begin + len;
	    
	    if (pos < offset) {
		len = 0;
		begin = pos;
	    }
	    if (pos > offset + length)
		goto stop_output;
	}
	scd = scd->next;
    }
    
 stop_output:
    DBG(DBG_PROC, printk("2pos: %ld offset: %ld len: %d\n", pos, offset, len));
    *start=buffer+(offset-begin);   /* Start of wanted data */
    len-=(offset-begin);	    /* Start slop */
    if(len>length)
	len = length;		    /* Ending slop */
    DBG(DBG_PROC, printk("3pos: %ld offset: %ld len: %d\n", pos, offset, len));
    
    return (len);     
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

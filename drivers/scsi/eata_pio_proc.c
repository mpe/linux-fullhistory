
#define MAX_SCSI_DEVICE_CODE 10
const char *const pio_scsi_dev_types[MAX_SCSI_DEVICE_CODE] =
{
    "Direct-Access    ",
    "Sequential-Access",
    "Printer          ",
    "Processor        ",
    "WORM             ",
    "CD-ROM           ",
    "Scanner          ",
    "Optical Device   ",
    "Medium Changer   ",
    "Communications   "
};

/*
 * eata_set_info
 * buffer : pointer to the data that has been written to the hostfile
 * length : number of bytes written to the hostfile
 * HBA_ptr: pointer to the Scsi_Host struct
 */
int eata_pio_set_info(char *buffer, int length, struct Scsi_Host *HBA_ptr)
{
    DBG(DBG_PROC_WRITE, printk("%s\n", buffer));
    return(-ENOSYS);  /* Currently this is a no-op */
}

/*
 * eata_proc_info
 * inout : decides on the direction of the dataflow and the meaning of the variables
 * buffer: If inout==FALSE data is beeing written to it else read from it
 * *start: If inout==FALSE start of the valid data in the buffer
 * offset: If inout==FALSE offset from the beginning of the imaginary file from which we start writing into the buffer
 * length: If inout==FALSE max number of bytes to be written into the buffer else number of bytes in the buffer
 */
int eata_pio_proc_info(char *buffer, char **start, off_t offset, int length, int hostno, int inout)
{

    Scsi_Device *scd;
    struct Scsi_Host *HBA_ptr;
    static u8 buff[512];
    int i, x; 
    int   size, len = 0;
    off_t begin = 0;
    off_t pos = 0;

    HBA_ptr = first_HBA;
    for (i = 1; i <= registered_HBAs; i++) {
	if (HBA_ptr->host_no == hostno)
	    break;
	HBA_ptr = SD(HBA_ptr)->next;
    }        

    if(inout == TRUE) /* Has data been writen to the file ? */ 
	return(eata_pio_set_info(buffer, length, HBA_ptr));

    if (offset == 0)
	memset(buff, 0, sizeof(buff));

    size = sprintf(buffer+len, "EATA (Extended Attachment) PIO driver version: "
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
    size = sprintf(buffer + len, "IO: PIO\n");
    len += size; 
    pos = begin + len;
    size = sprintf(buffer + len, "Base IO : %#.4x\n", (u32) HBA_ptr->base);
    len += size; 
    pos = begin + len;
    size = sprintf(buffer + len, "Host Bus: %s\n", 
		   (SD(HBA_ptr)->bustype == 'P')?"PCI ":
		   (SD(HBA_ptr)->bustype == 'E')?"EISA":"ISA ");
    
    len += size; 
    pos = begin + len;
    
    if (pos < offset) {
	len = 0;
	begin = pos;
    }
    if (pos > offset + length)
	goto stop_output;
    
    scd = scsi_devices;
    
    size = sprintf(buffer+len,"Attached devices: %s\n", (scd)?"":"none");
    len += size; 
    pos = begin + len;
    
    while (scd) {
	if (scd->host == HBA_ptr) {
	    
	    size = sprintf(buffer + len, "Channel: %02d Id: %02d Lun: %02d\n  Vendor: ",
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
			    pio_scsi_dev_types[(int)scd->type] : "Unknown          " );
	    size += sprintf(buffer + len + size, "               ANSI"
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
    len-=(offset-begin);            /* Start slop */
    if(len>length)
	len = length;               /* Ending slop */
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

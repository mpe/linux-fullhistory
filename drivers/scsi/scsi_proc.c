/*
 * linux/drivers/scsi/scsi_proc.c
 *
 * The functions in this file provide an interface between
 * the PROC file system and the SCSI device drivers
 * It is mainly used for debugging, statistics and to pass 
 * information directly to the lowlevel driver.
 *
 * (c) 1995 Michael Neuffer neuffer@goofy.zdv.uni-mainz.de 
 * Version: 0.99.7   last change: 95/07/18
 * 
 * generic command parser provided by: 
 * Andreas Heilwagen <crashcar@informatik.uni-koblenz.de>
 */

#ifdef MODULE
/*
 * Don't import our own symbols, as this would severely mess up our
 * symbol tables.
 */
#define _SCSI_SYMS_VER_
#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include "../block/blk.h"
#include "scsi.h"
#include "hosts.h"

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

extern struct proc_dir_entry scsi_dir[];
extern struct proc_dir_entry scsi_hba_dir[];
extern int scsi_proc_info(char *, char **, off_t, int, int, int);
 

int get_hba_index(int ino)
{
    Scsi_Host_Template *tpnt = scsi_hosts;
    struct Scsi_Host *hpnt = scsi_hostlist;
    uint x = 0;

    /*
     * Danger - this has massive race conditions in it.
     * If the someone adds/removes entries from the scsi chain
     * while someone else is looking at /proc/scsi, unpredictable
     * results will be obtained.
     */
    while (tpnt) {
	if (ino == tpnt->low_ino) 
		return(x);
	x += 3;
	while (hpnt) {
            if(hpnt->hostt == tpnt) /* This gives us the correct index */
                x++;
	    hpnt = hpnt->next;
	}
	tpnt = tpnt->next;
    }
    return(0);
}

/* generic_proc_info
 * Used if the driver currently has no own support for /proc/scsi
 */
int generic_proc_info(char *buffer, char **start, off_t offset, 
		     int length, int inode, int inout)
{
    int len, pos, begin;

    if(inout == TRUE)
	return(-ENOSYS);  /* This is a no-op */
    
    begin = 0;
    pos = len = sprintf(buffer, 
			"The driver does not yet support the proc-fs\n");
    if(pos < offset) {
	len = 0;
	begin = pos;
    }
    
    *start = buffer + (offset - begin);   /* Start of wanted data */
    len -= (offset - begin);
    if(len > length)
	len = length;
    
    return(len);
}

/* dispatch_scsi_info is the central dispatcher 
 * It is the interface between the proc-fs and the SCSI subsystem code
 */
extern int dispatch_scsi_info(int ino, char *buffer, char **start, 
			      off_t offset, int length, int func)
{
    struct Scsi_Host *hpnt = scsi_hostlist;

    if(func != 2) {    
	if(ino == PROC_SCSI_SCSI) {            
            /*
             * This is for the scsi core, rather than any specific
             * lowlevel driver.
             */
            return(scsi_proc_info(buffer, start, offset, length, 0, func));
        }

	while(hpnt) {
	    if (ino == (hpnt->host_no + PROC_SCSI_FILE)) {
                if(hpnt->hostt->proc_info == NULL)
                    return generic_proc_info(buffer, start, offset, length, 
                                             hpnt->host_no, func);
                else
                    return(hpnt->hostt->proc_info(buffer, start, offset, 
                                                  length, hpnt->host_no, func));
            }
	    hpnt = hpnt->next;
	}
	return(-EBADF);
    } else
	return(get_hba_index(ino));
}

inline uint count_templates(void)
{
    Scsi_Host_Template *tpnt = scsi_hosts;
    uint x = 0;
    
    while (tpnt) {
	tpnt = tpnt->next;
	x++;
    }
    return (x);
}

void build_proc_dir_hba_entries(void)
{
    Scsi_Host_Template *tpnt = scsi_hosts;
    struct Scsi_Host *hpnt;
    uint x, y;

    /* namespace for 16 HBAs with host_no 0-999 
     * I don't think we'll need more. Having more than 2 
     * HBAs in one system is already highly unusual 
     */
    static char names[PROC_SCSI_LAST - PROC_SCSI_FILE][4];
    
    x = y = 0;

    while (tpnt) {
	scsi_hba_dir[x].low_ino = tpnt->low_ino;
	scsi_hba_dir[x].namelen = 1;
	scsi_hba_dir[x++].name = ".";
	scsi_hba_dir[x].low_ino = PROC_SCSI;
	scsi_hba_dir[x].namelen = 2;
	scsi_hba_dir[x++].name = "..";

        hpnt = scsi_hostlist;
        while (hpnt) {
	    if (tpnt == hpnt->hostt) {
		scsi_hba_dir[x].low_ino = PROC_SCSI_FILE + hpnt->host_no;
		scsi_hba_dir[x].namelen = sprintf(names[y],"%d",hpnt->host_no);
		scsi_hba_dir[x].name = names[y];
		y++;
		x++;
	    }
	    hpnt = hpnt->next;
	}

        scsi_hba_dir[x].low_ino = 0;
	scsi_hba_dir[x].namelen = 0;
	scsi_hba_dir[x++].name = NULL;
	tpnt = tpnt->next;
    }    
}

void build_proc_dir_entries(void)
{
    Scsi_Host_Template *tpnt = scsi_hosts;
    
    uint newnum; 
    uint x;
    
    newnum = count_templates();
    
    scsi_dir[0].low_ino = PROC_SCSI;
    scsi_dir[0].namelen = 1;
    scsi_dir[0].name = ".";
    scsi_dir[1].low_ino = PROC_ROOT_INO;
    scsi_dir[1].namelen = 2;
    scsi_dir[1].name = "..";
    scsi_dir[2].low_ino = PROC_SCSI_SCSI;
    scsi_dir[2].namelen = 4;
    scsi_dir[2].name = "scsi";

    for(x = 3; x < newnum + 3; x++, tpnt = tpnt->next) { 
	scsi_dir[x].low_ino = tpnt->low_ino;
	scsi_dir[x].namelen = strlen(tpnt->procname);
	scsi_dir[x].name = tpnt->procname;
    }

    scsi_dir[x].low_ino = 0;
    scsi_dir[x].namelen = 0;
    scsi_dir[x].name = NULL;
    
    build_proc_dir_hba_entries();
}


/*
 *  parseHandle *parseInit(char *buf, char *cmdList, int cmdNum); 
 *	 	gets a pointer to a null terminated data buffer
 *		and a list of commands with blanks as delimiter 
 *      in between. 
 *      The commands have to be alphanumerically sorted. 
 *      cmdNum has to contain the number of commands.
 *		On success, a pointer to a handle structure
 *		is returned, NULL on failure
 *
 *	int parseOpt(parseHandle *handle, char **param);
 *		processes the next parameter. On success, the
 *		index of the appropriate command in the cmdList
 *		is returned, starting with zero.
 *		param points to the null terminated parameter string.
 *		On failure, -1 is returned.
 *
 *	The databuffer buf may only contain pairs of commands
 *	    options, separated by blanks:
 *		<Command> <Parameter> [<Command> <Parameter>]*
 */

typedef struct
{
    char *buf,				   /* command buffer  */
	 *cmdList,                         /* command list    */
	 *bufPos,                          /* actual position */
	 **cmdPos,                         /* cmdList index   */
	 cmdNum;                           /* cmd number      */
} parseHandle;
	

inline int parseFree (parseHandle *handle)               /* free memory     */
{
    kfree (handle->cmdPos);
    kfree (handle);
    
    return(-1);
}

	
parseHandle *parseInit(char *buf, char *cmdList, int cmdNum)
{
    char        *ptr;                               /* temp pointer    */
    parseHandle *handle;                            /* new handle      */
    
    if (!buf || !cmdList)                           /* bad input ?     */
	return(NULL);
    if ((handle = (parseHandle*) kmalloc(sizeof(parseHandle), 1)) == 0)
	return(NULL);                               /* out of memory   */
    if ((handle->cmdPos = (char**) kmalloc(sizeof(int), cmdNum)) == 0) {
	kfree(handle);
	return(NULL);                               /* out of memory   */
    }
    
    handle->buf     = handle->bufPos = buf;         /* init handle     */
    handle->cmdList = cmdList;
    handle->cmdNum  = cmdNum;
    
    handle->cmdPos[cmdNum = 0] = cmdList;
    for (ptr = cmdList; *ptr; ptr++) {          /* scan command string */
	if(*ptr == ' ') {                       /* and insert zeroes   */
	    *ptr++ = 0;
	    handle->cmdPos[++cmdNum] = ptr++;
	} 
    }
    return(handle);
}


int parseOpt(parseHandle *handle, char **param)
{
    int  cmdIndex = 0, 
	 cmdLen = 0;
    char *startPos;
    
    if (!handle)                                    /* invalid handle  */
	return(parseFree(handle));
    /* skip spaces     */  
    for (; *(handle->bufPos) && *(handle->bufPos) == ' '; handle->bufPos++);
    if (!*(handle->bufPos))
	return(parseFree(handle));                  /* end of data     */
    
    startPos = handle->bufPos;                      /* store cmd start */
    for (; handle->cmdPos[cmdIndex][cmdLen] && *(handle->bufPos); handle->bufPos++)
    {                                               /* no string end?  */
	for (;;)
	{
	    if (*(handle->bufPos) == handle->cmdPos[cmdIndex][cmdLen])
		break;                              /* char matches ?  */
	    else
		if (memcmp(startPos, (char*)(handle->cmdPos[++cmdIndex]), cmdLen))
		    return(parseFree(handle));      /* unknown command */
	    
	    if (cmdIndex >= handle->cmdNum)
		return(parseFree(handle));          /* unknown command */     
	}
	
	cmdLen++;                                   /* next char       */
    }
    
    /* Get param. First skip all blanks, then insert zero after param  */
    
    for (; *(handle->bufPos) && *(handle->bufPos) == ' '; handle->bufPos++);
    *param = handle->bufPos; 
    
    for (; *(handle->bufPos) && *(handle->bufPos) != ' '; handle->bufPos++);
    *(handle->bufPos++) = 0;
    
    return(cmdIndex);
}

#define MAX_SCSI_DEVICE_CODE 10
const char *const scsi_dev_types[MAX_SCSI_DEVICE_CODE] =
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

void proc_print_scsidevice(Scsi_Device *scd, char *buffer, int *size, int len)
{	    
    int x, y = *size;
    
    y = sprintf(buffer + len, 
		    "Channel: %02d Id: %02d Lun: %02d\n  Vendor: ",
		    scd->channel, scd->id, scd->lun);
    for (x = 0; x < 8; x++) {
	if (scd->vendor[x] >= 0x20)
	    y += sprintf(buffer + len + y, "%c", scd->vendor[x]);
	else
	    y += sprintf(buffer + len + y," ");
    }
    y += sprintf(buffer + len + y, " Model: ");
    for (x = 0; x < 16; x++) {
	if (scd->model[x] >= 0x20)
	    y +=  sprintf(buffer + len + y, "%c", scd->model[x]);
	else
	    y += sprintf(buffer + len + y, " ");
    }
    y += sprintf(buffer + len + y, " Rev: ");
    for (x = 0; x < 4; x++) {
	if (scd->rev[x] >= 0x20)
	    y += sprintf(buffer + len + y, "%c", scd->rev[x]);
	else
	    y += sprintf(buffer + len + y, " ");
    }
    y += sprintf(buffer + len + y, "\n");
    
    y += sprintf(buffer + len + y, "  Type:   %s ",
		     scd->type < MAX_SCSI_DEVICE_CODE ? 
		     scsi_dev_types[(int)scd->type] : "Unknown          " );
    y += sprintf(buffer + len + y, "               ANSI"
		     " SCSI revision: %02x", (scd->scsi_level < 3)?1:2);
    if (scd->scsi_level == 2)
	y += sprintf(buffer + len + y, " CCS\n");
    else
	y += sprintf(buffer + len + y, "\n");

    *size = y; 
    return;
}

/*
 * Overrides for Emacs so that we get a uniform tabbing style.
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

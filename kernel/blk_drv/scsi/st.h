
#ifndef _ST_H
	#define _ST_H
/*
	$Header: /usr/src/linux/kernel/blk_drv/scsi/RCS/st.h,v 1.1 1992/04/24 18:01:50 root Exp root $
*/

#ifndef _SCSI_H
#include "scsi.h"
#endif

#define MAX_ST 1

typedef struct 
	{
	/*
		Undecided goodies go here!!!
	*/
	Scsi_Device* device;	
	} Scsi_Tape;


extern int NR_ST;
extern Scsi_Tape scsi_tapes[MAX_ST];
void st_init(void);
#endif

#ifndef _FD_H
#define _FD_H

#define FDCLRPRM 0 /* clear user-defined parameters */
#define FDSETPRM 1 /* set user-defined parameters for current media */
#define FDDEFPRM 2 /* set user-defined parameters until explicitly cleared */
#define FDGETPRM 3 /* get disk parameters */
#define	FDMSGON  4 /* issue kernel messages on media type change */
#define	FDMSGOFF 5 /* don't issue kernel messages on media type change */
#define FDFMTBEG 6 /* begin formatting a disk */
#define	FDFMTTRK 7 /* format the specified track */
#define FDFMTEND 8 /* end formatting a disk */

#define FD_FILL_BYTE 0xF6 /* format fill byte */

#define FORMAT_NONE	0	/* no format request */
#define FORMAT_WAIT	1	/* format request is waiting */
#define FORMAT_BUSY	2	/* formatting in progress */
#define FORMAT_OKAY	3	/* successful completion */
#define FORMAT_ERROR	4	/* formatting error */

struct floppy_struct {
	unsigned int size, sect, head, track, stretch;
	unsigned char gap,rate,spec1,fmt_gap;
	char *name; /* used only for predefined formats */
};

struct format_descr {
    unsigned int device,head,track;
};

#endif

#ifndef _CONSTANTS_H
#define _CONSTANTS_H
extern void print_command(unsigned char *);
extern int print_msg(unsigned char *);
extern void print_sense(char *,  Scsi_Cmnd *);
extern void print_status(int);
extern void print_Scsi_Cmnd (Scsi_Cmnd *);
#endif /* def _CONSTANTS_H */

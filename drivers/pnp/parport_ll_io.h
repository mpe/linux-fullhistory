/* $Id: parport_ll_io.h,v 1.1.2.1 1997/03/26 13:01:09 phil Exp $ 
 * David Campbell's "favourite IO routines" for parallel ports
 */

#define r_dtr(x)	inb((x)->base)
#define r_str(x)	inb((x)->base+1)
#define r_ctr(x)	inb((x)->base+2)
#define r_epp(x)	inb((x)->base+4)
#define r_fifo(x)	inb((x)->base+0x400)
#define r_ecr(x)	inb((x)->base+0x402)
#define r_cnfgA(x)	inb((x)->base+0x400)
#define r_cnfgB(x)	inb((x)->base+0x401)

#define w_dtr(x,y)	outb((y), (x)->base)
#define w_str(x,y)	outb((y), (x)->base+1)
#define w_ctr(x,y)	outb((y), (x)->base+2)
#define w_epp(x,y)	outb((y), (x)->base+4)
#define w_fifo(x,y)	outb((y), (x)->base+0x400)
#define w_ecr(x,y)	outb((y), (x)->base+0x402)
#define w_cnfgA(x,y)	outb((y), (x)->base+0x400)
#define w_cnfgB(x,y)	outb((y), (x)->base+0x401)

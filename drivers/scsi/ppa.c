/*      ppa.c   --  low level driver for the IOMEGA PPA3 
                    parallel port SCSI host adapter.

        (The PPA3 is the embedded controller in the ZIP drive.)

        (c) 1995,1996 Grant R. Guenther, grant@torque.net,
                      under the terms of the GNU Public License.

*/

/*      This driver was developed without the benefit of any technical
        specifications for the interface.  Instead, a modified version of
        DOSemu was used to monitor the protocol used by the DOS driver
        for this adapter.  I have no idea how my programming model relates
        to IOMEGA's design.

        IOMEGA's driver does not generate linked commands.  I've never
        observed a SCSI message byte in the protocol transactions, so
        I am assuming that as long as linked commands are not used
        we won't see any.  

        So far, this driver has been tested with the embedded PPA3 in the
        ZIP drive, only.   It can detect and adapt to 4- and 8-bit parallel
        ports, but there is currently no support for EPP or ECP ports, as
        I have been unable to make the DOS drivers work in these modes on
        my test rig.

        For more information, see the file drivers/scsi/README.ppa.

*/

#define   PPA_VERSION   "0.26"

/* Change these variables here or with insmod or with a LILO or LOADLIN
   command line argument
*/

static int      ppa_base       = 0x378;  /* parallel port address    */
static int      ppa_speed_high = 1;      /* port delay in data phase */
static int      ppa_speed_low  = 6;      /* port delay otherwise     */
static int      ppa_nybble     = 0;      /* don't force nybble mode  */


#define   PPA_CAN_QUEUE         1       /* use "queueing" interface */
#define   PPA_SELECT_TMO        5000    /* how long to wait for target ? */
#define   PPA_SPIN_TMO          5000000 /* ppa_wait loop limiter */
#define   PPA_SECTOR_SIZE       512     /* for a performance hack only */

#include  <unistd.h>
#include  <linux/module.h>
#include  <linux/kernel.h>
#include  <linux/tqueue.h>
#include  <linux/ioport.h>
#include  <linux/delay.h>
#include  <linux/blk.h>
#include  <linux/proc_fs.h>
#include  <linux/stat.h>
#include  <asm/io.h>
#include  "sd.h"
#include  "hosts.h"
#include  "ppa.h"

struct proc_dir_entry proc_scsi_ppa = 
                { PROC_SCSI_PPA, 3, "ppa", S_IFDIR|S_IRUGO|S_IXUGO, 2 };

static int              ppa_abort_flag = 0;
static int              ppa_error_code = DID_OK;
static char             ppa_info_string[132];
static Scsi_Cmnd        *ppa_current = 0;
static void             (*ppa_done) (Scsi_Cmnd *);
static int              ppa_port_delay;

void    out_p( short port, char byte)

{       outb(byte,ppa_base+port);
        udelay(ppa_port_delay);
}

char    in_p( short port)

{       return inb(ppa_base+port);
        udelay(ppa_port_delay);
}

void    ppa_d_pulse( char b )

{       out_p(0,b);
        out_p(2,0xc); out_p(2,0xe); out_p(2,0xc); out_p(2,0x4); out_p(2,0xc);
}

void    ppa_disconnect( void )

{       ppa_d_pulse(0);
        ppa_d_pulse(0x3c);
        ppa_d_pulse(0x20);
        ppa_d_pulse(0xf);
}

void    ppa_c_pulse( char b )

{       out_p(0,b);
        out_p(2,0x4); out_p(2,0x6); out_p(2,0x4); out_p(2,0xc);
}

void    ppa_connect( void )

{       ppa_c_pulse(0);
        ppa_c_pulse(0x3c);
        ppa_c_pulse(0x20);
        ppa_c_pulse(0x8f);
}

void    ppa_do_reset( void )

{       out_p(2,0);             /* This is really just a guess */
        udelay(100);
}

char    ppa_select( int  initiator, int target )

{       char    r;
        int     k;

        r = in_p(1);
        out_p(0,(1<<target)); out_p(2,0xe); out_p(2,0xc);
        out_p(0,(1<<initiator)); out_p(2,0x8);

        k = 0;
        while ( !(r = (in_p(1) & 0xf0)) && (k++ < PPA_SELECT_TMO)) barrier();
        return r;
}

char    ppa_wait( void ) 

/*      Wait for the high bit to be set.

        In principle, this could be tied to an interrupt, but the adapter
        doesn't appear to be designed to support interrupts.  We spin on
        the 0x80 ready bit. 
*/

{       int     k;
        char    r;

        ppa_error_code = DID_OK;
        k = 0;
        while (!((r = in_p(1)) & 0x80) 
                && (k++ < PPA_SPIN_TMO) && !ppa_abort_flag  ) barrier();
                        
        if (ppa_abort_flag) {
                if (ppa_abort_flag == 1) ppa_error_code = DID_ABORT;
                else {  ppa_do_reset();
                        ppa_error_code = DID_RESET;
                }
                ppa_disconnect();
                return 0;
        }
        if (k >= PPA_SPIN_TMO) { 
                ppa_error_code = DID_TIME_OUT;
                ppa_disconnect();
                return 0;               /* command timed out */
        }
        return (r & 0xf0);
}

int     ppa_init( void )

/* This is based on a trace of what the Iomega DOS 'guest' driver does.
   I've tried several different kinds of parallel ports with guest and
   coded this to react in the same ways that it does.

   The return value from this function is just a hint about where the
   handshaking failed.

*/

{       char    r, s;

        out_p(0,0xaa); 
        if (in_p(0) != (char) 0xaa) return 1; 
        ppa_disconnect();
        ppa_connect();
        out_p(2,0x6); 
        if ((in_p(1) & 0xf0) != 0xf0) return 2; 
        out_p(2,0x4);
        if ((in_p(1) & 0xf0) != 0x80) return 3; 
        ppa_disconnect();
        s = in_p(2);
        out_p(2,0xec);
        out_p(0,0x55); 
        r = in_p(0);    
        if (r != (char) 0xff) { 
                ppa_nybble = 1;
                if (r != (char) 0x55) return 4; 
                out_p(0,0xaa); if (in_p(0) != (char) 0xaa) return 5; 
        }
        out_p(2,s);
        ppa_connect();
        out_p(0,0x40); out_p(2,0x8); out_p(2,0xc);
        ppa_disconnect();

        return 0;
}

int     ppa_start( Scsi_Cmnd * cmd )

{       int     k;

        ppa_error_code = DID_OK;
        ppa_abort_flag = 0;

        if (cmd->target == PPA_INITIATOR) {
                ppa_error_code = DID_BAD_TARGET;
                return 0;
        }
        ppa_connect();
        if (!ppa_select(PPA_INITIATOR,cmd->target)) {
                ppa_disconnect();
                ppa_error_code = DID_NO_CONNECT;
                return 0;
        }
        out_p(2,0xc);

        for (k=0; k < cmd->cmd_len; k++) {        /* send the command */
                if (!ppa_wait()) return 0;
                out_p(0,cmd->cmnd[k]);
                out_p(2,0xe);
                out_p(2,0xc);
        }

#ifdef PPA_DEBUG
        printk("PPA: command out: ");
        for (k=0; k < cmd->cmd_len; k++)
                printk("%3x",(cmd->cmnd[k]) & 0xff );
        printk("\n");
#endif

        return 1;
}

int     ppa_completion( Scsi_Cmnd * cmd )

/* The bulk flag enables some optimisations in the data transfer loops,
   it should be true for any command that transfers data in integral
   numbers of sectors.

   The driver appears to remain stable if we speed up the parallel port
   i/o in this function, but not elsewhere.
*/

{       char    r, l, h, v;
        int     dir, cnt, blen, fast, bulk;
        char    *buffer;

#ifdef PPA_DEBUG
        int     k;
#endif

        if (!(r = ppa_wait())) return 0;
        v = cmd->cmnd[0];
        bulk = ((v==READ_6)||(v==READ_10)||(v==WRITE_6)||(v==WRITE_10));
        buffer = cmd->request_buffer;
        blen = cmd->request_bufflen;
        cnt = 0;  dir = 0;
        if (r == (char) 0xc0) dir = 1;  /* d0 = read c0 = write f0 = status */

        ppa_port_delay = ppa_speed_high;

        while (r != (char) 0xf0) {
                if (((r & 0xc0) != 0xc0 ) || (cnt >= blen)) {
                        ppa_disconnect();
                        ppa_error_code = DID_ERROR;
                        return 0;
                }
                fast = bulk && ((blen - cnt) >= PPA_SECTOR_SIZE);
                if (dir) do {
                        out_p(0,buffer[cnt++]);
                        out_p(2,0xe); out_p(2,0xc);
                        if (!fast) break;
                } while (cnt % PPA_SECTOR_SIZE);
                else {  
                        if (ppa_nybble) do {
                                out_p(2,0x4); h = in_p(1); 
                                out_p(2,0x6); l = in_p(1);
                                v = ((l >> 4) & 0x0f) + (h & 0xf0);
                                buffer[cnt++] = v;
                                if (!fast) break;
                        } while (cnt % PPA_SECTOR_SIZE);
                        else do {
                                out_p(2,0x25); v = in_p(0); out_p(2,0x27);
                                buffer[cnt++] = v;
                                if (!fast) break;
                        } while (cnt % PPA_SECTOR_SIZE);
                        if (!ppa_nybble) {
                                out_p(2,0x5); out_p(2,0x4);
                        }
                        out_p(2,0xc);
                }
                if (!(r = ppa_wait())) return 0;
        }

        ppa_port_delay = ppa_speed_low;

        out_p(2,0x4);            /* now read status byte */
        h = in_p(1);
        out_p(2,0x6);
        l = in_p(1);
        out_p(2,0xc);
        r = ((l >> 4) & 0x0f) + (h & 0xf0);

        out_p(2,0xe); out_p(2,0xc);
        ppa_disconnect();

#ifdef PPA_DEBUG
        printk("PPA: status: %x, data[%d]: ",r & STATUS_MASK,cnt);
        if (cnt > 12) cnt = 12;
        for (k=0; k < cnt; k++)
           printk("%3x",buffer[k] & 0xff );
        printk("\n");   
#endif

        return (r & STATUS_MASK);
}

/* deprecated synchronous interface */

int     ppa_command( Scsi_Cmnd * cmd )

{       int     s;

        sti();
        s = 0;
        if (ppa_start(cmd))
           if (ppa_wait()) 
               s = ppa_completion(cmd);
        return s + (ppa_error_code << 16);
}

/* pseudo-interrupt queueing interface */

/* Since the PPA itself doesn't generate interrupts, we use
   the scheduler's task queue to generate a stream of call-backs and
   complete the request when the drive is ready.
*/

static void ppa_interrupt( void *data);

static struct tq_struct ppa_tq = {0,0,ppa_interrupt,NULL};

static void ppa_interrupt( void *data)

{       Scsi_Cmnd *cmd;
        void  (*done) (Scsi_Cmnd *);

        cmd = ppa_current;
        done = ppa_done;
        if (!cmd) return;
        
        if (ppa_abort_flag) {
                ppa_disconnect();
                if(ppa_abort_flag == 1) cmd->result = DID_ABORT << 16;
                else { ppa_do_reset();
                       cmd->result = DID_RESET << 16;
                }
                ppa_current = 0;
                done(cmd);
                return;
        }
        if (!( in_p(1) & 0x80)) {
                queue_task(&ppa_tq,&tq_scheduler);
                return;
        }
        cmd->result = ppa_completion(cmd) + (ppa_error_code << 16);
        ppa_current = 0;
        done(cmd);
        return;
}

int     ppa_queuecommand( Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))

{       if (ppa_current) return 0;
        sti();
        ppa_current = cmd;
        ppa_done = done;
        if (!ppa_start(cmd)) {
                cmd->result = ppa_error_code << 16;
                ppa_current = 0;
                done(cmd);
                return 0;
        }
        queue_task(&ppa_tq,&tq_scheduler);
        return 0;
}

int     ppa_detect( Scsi_Host_Template * host )

{       struct  Scsi_Host       *hreg;
        int     rs;

        /* can we have the ports ? */

        if (check_region(ppa_base,3)) {
                printk("PPA: ports at 0x%3x are not available\n",ppa_base);
                return 0;
        }

        /* attempt to initialise the controller */

        ppa_port_delay = ppa_speed_low;

        rs = ppa_init();
        if (rs) {
            printk("PPA: unable to initialise controller at 0x%x, error %d\n",
                   ppa_base,rs);
            return 0;
        }

        /* now the glue ... */

        host->proc_dir = &proc_scsi_ppa;

        request_region(ppa_base,3,"ppa");

        host->can_queue = PPA_CAN_QUEUE;

        hreg = scsi_register(host,0);
        hreg->io_port = ppa_base;
        hreg->n_io_port = 3;
        hreg->dma_channel = -1;

        sprintf(ppa_info_string,
                "PPA driver version %s using %d-bit mode on port 0x%x.",
                PPA_VERSION,8-ppa_nybble*4,ppa_base);
        host->name = ppa_info_string;

        return 1;       /* 1 host detected */
}

int     ppa_biosparam( Disk * disk, kdev_t dev, int ip[])

/*  Apparently the the disk->capacity attribute is off by 1 sector 
    for all disk drives.  We add the one here, but it should really
    be done in sd.c.  Even if it gets fixed there, this will still
    work.
*/

{       ip[0] = 0x40;
        ip[1] = 0x20;
        ip[2] = (disk->capacity +1) / (ip[0] * ip[1]);
        if (ip[2] > 1024) {
                ip[0] = 0xff;
                ip[1] = 0x3f;
                ip[2] = (disk->capacity +1) / (ip[0] * ip[1]);
                if (ip[2] > 1023)
                        ip[2] = 1023;
        }
        return 0;
}

int     ppa_abort( Scsi_Cmnd * cmd )

{       ppa_abort_flag = 1;
        return SCSI_ABORT_SNOOZE;
}

int     ppa_reset( Scsi_Cmnd * cmd )

{       ppa_abort_flag = 2;
        return SCSI_RESET_PUNT;
}

const char      *ppa_info( struct Scsi_Host * host )

{       return ppa_info_string;
}

#ifndef MODULE

/* Command line parameters (for built-in driver):

   Syntax:  ppa=base[,speed_high[,speed_low[,nybble]]]

   For example:  ppa=0x378   or   ppa=0x378,0,3

*/

void    ppa_setup(char *str, int *ints)

{       if (ints[0] > 0) ppa_base = ints[1];
        if (ints[0] > 1) ppa_speed_high = ints[2];
        if (ints[0] > 2) ppa_speed_low = ints[3];
        if (ints[0] > 3) ppa_nybble = ints[4];
        if (ints[0] > 4) ppa_nybble = ints[5];
}

#else

Scsi_Host_Template      driver_template = PPA;

#include  "scsi_module.c"

#endif

/* end of ppa.c */

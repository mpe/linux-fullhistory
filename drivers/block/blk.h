#ifndef _BLK_H
#define _BLK_H

#include <linux/blkdev.h>
#include <linux/locks.h>
#include <linux/config.h>

/*
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 */
#define NR_REQUEST	64

/*
 * This is used in the elevator algorithm: Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical than writes.
 */
#define IN_ORDER(s1,s2) \
((s1)->cmd < (s2)->cmd || ((s1)->cmd == (s2)->cmd && \
((s1)->dev < (s2)->dev || (((s1)->dev == (s2)->dev && \
(s1)->sector < (s2)->sector)))))

/*
 * These will have to be changed to be aware of different buffer
 * sizes etc.. It actually needs a major cleanup.
 */
#ifdef IDE_DRIVER
#define SECTOR_MASK ((BLOCK_SIZE >> 9) - 1)
#else
#define SECTOR_MASK (blksize_size[MAJOR_NR] &&     \
	blksize_size[MAJOR_NR][MINOR(CURRENT->dev)] ? \
	((blksize_size[MAJOR_NR][MINOR(CURRENT->dev)] >> 9) - 1) :  \
	((BLOCK_SIZE >> 9)  -  1))
#endif /* IDE_DRIVER */

#define SUBSECTOR(block) (CURRENT->current_nr_sectors > 0)

extern unsigned long cdu31a_init(unsigned long mem_start, unsigned long mem_end);
extern unsigned long mcd_init(unsigned long mem_start, unsigned long mem_end);
#ifdef CONFIG_AZTCD
extern unsigned long aztcd_init(unsigned long mem_start, unsigned long mem_end);
#endif
#ifdef CONFIG_CDU535
extern unsigned long sony535_init(unsigned long mem_start, unsigned long mem_end);
#endif
#ifdef CONFIG_BLK_DEV_HD
extern unsigned long hd_init(unsigned long mem_start, unsigned long mem_end);
#endif
#ifdef CONFIG_BLK_DEV_IDE
extern unsigned long ide_init(unsigned long mem_start, unsigned long mem_end);
#endif
#ifdef CONFIG_SBPCD
extern unsigned long sbpcd_init(unsigned long, unsigned long);
#endif CONFIG_SBPCD
extern void set_device_ro(int dev,int flag);

extern void floppy_init(void);
#ifdef FD_MODULE
static
#else
extern
#endif
int new_floppy_init(void);
extern void rd_load(void);
extern long rd_init(long mem_start, int length);
extern int ramdisk_size;

extern unsigned long xd_init(unsigned long mem_start, unsigned long mem_end);

#define RO_IOCTLS(dev,where) \
  case BLKROSET: if (!suser()) return -EACCES; \
		 set_device_ro((dev),get_fs_long((long *) (where))); return 0; \
  case BLKROGET: { int __err = verify_area(VERIFY_WRITE, (void *) (where), sizeof(long)); \
		   if (!__err) put_fs_long(0!=is_read_only(dev),(long *) (where)); return __err; }
		 
#if defined(MAJOR_NR) || defined(IDE_DRIVER)

/*
 * Add entries as needed.
 */

#ifdef IDE_DRIVER

#define DEVICE_NR(device)	(MINOR(device) >> PARTN_BITS)
#define DEVICE_ON(device)	/* nothing */
#define DEVICE_OFF(device)	/* nothing */

#elif (MAJOR_NR == MEM_MAJOR)

/* ram disk */
#define DEVICE_NAME "ramdisk"
#define DEVICE_REQUEST do_rd_request
#define DEVICE_NR(device) ((device) & 7)
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == FLOPPY_MAJOR)

static void floppy_off(unsigned int nr);

#define DEVICE_NAME "floppy"
#define DEVICE_INTR do_floppy
#define DEVICE_REQUEST do_fd_request
#define DEVICE_NR(device) ( ((device) & 3) | (((device) & 0x80 ) >> 5 ))
#define DEVICE_ON(device)
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

#elif (MAJOR_NR == HD_MAJOR)

/* harddisk: timeout is 6 seconds.. */
#define DEVICE_NAME "harddisk"
#define DEVICE_INTR do_hd
#define DEVICE_TIMEOUT HD_TIMER
#define TIMEOUT_VALUE 600
#define DEVICE_REQUEST do_hd_request
#define DEVICE_NR(device) (MINOR(device)>>6)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == SCSI_DISK_MAJOR)

#define DEVICE_NAME "scsidisk"
#define DEVICE_INTR do_sd  
#define TIMEOUT_VALUE 200
#define DEVICE_REQUEST do_sd_request
#define DEVICE_NR(device) (MINOR(device) >> 4)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == SCSI_TAPE_MAJOR)

#define DEVICE_NAME "scsitape"
#define DEVICE_INTR do_st  
#define DEVICE_NR(device) (MINOR(device) & 0x7f)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == SCSI_CDROM_MAJOR)

#define DEVICE_NAME "CD-ROM"
#define DEVICE_INTR do_sr
#define DEVICE_REQUEST do_sr_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == XT_DISK_MAJOR)

#define DEVICE_NAME "xt disk"
#define DEVICE_REQUEST do_xd_request
#define DEVICE_NR(device) (MINOR(device) >> 6)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == CDU31A_CDROM_MAJOR)

#define DEVICE_NAME "CDU31A"
#define DEVICE_REQUEST do_cdu31a_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MITSUMI_CDROM_MAJOR)

#define DEVICE_NAME "Mitsumi CD-ROM"
/* #define DEVICE_INTR do_mcd */
#define DEVICE_REQUEST do_mcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == AZTECH_CDROM_MAJOR)

#define DEVICE_NAME "Aztech CD-ROM"
#define DEVICE_REQUEST do_aztcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == CDU535_CDROM_MAJOR)

#define DEVICE_NAME "SONY-CDU535"
#define DEVICE_INTR do_cdu535
#define DEVICE_REQUEST do_cdu535_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #1"
#define DEVICE_REQUEST do_sbpcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM2_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #2"
#define DEVICE_REQUEST do_sbpcd2_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM3_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #3"
#define DEVICE_REQUEST do_sbpcd3_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM4_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #4"
#define DEVICE_REQUEST do_sbpcd4_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#endif /* MAJOR_NR == whatever */

#if (MAJOR_NR != SCSI_TAPE_MAJOR) && !defined(IDE_DRIVER)

#ifndef CURRENT
#define CURRENT (blk_dev[MAJOR_NR].current_request)
#endif

#define CURRENT_DEV DEVICE_NR(CURRENT->dev)

#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif
#ifdef DEVICE_TIMEOUT

#define SET_TIMER \
((timer_table[DEVICE_TIMEOUT].expires = jiffies + TIMEOUT_VALUE), \
(timer_active |= 1<<DEVICE_TIMEOUT))

#define CLEAR_TIMER \
timer_active &= ~(1<<DEVICE_TIMEOUT)

#define SET_INTR(x) \
if ((DEVICE_INTR = (x)) != NULL) \
	SET_TIMER; \
else \
	CLEAR_TIMER;

#else

#define SET_INTR(x) (DEVICE_INTR = (x))

#endif /* DEVICE_TIMEOUT */

static void (DEVICE_REQUEST)(void);

#ifdef DEVICE_INTR
#define CLEAR_INTR SET_INTR(NULL)
#else
#define CLEAR_INTR
#endif

#define INIT_REQUEST \
	if (!CURRENT) {\
		CLEAR_INTR; \
		return; \
	} \
	if (MAJOR(CURRENT->dev) != MAJOR_NR) \
		panic(DEVICE_NAME ": request list destroyed"); \
	if (CURRENT->bh) { \
		if (!CURRENT->bh->b_lock) \
			panic(DEVICE_NAME ": block not locked"); \
	}

#endif /* (MAJOR_NR != SCSI_TAPE_MAJOR) && !defined(IDE_DRIVER) */

/* end_request() - SCSI devices have their own version */

#if ! SCSI_MAJOR(MAJOR_NR)

#ifdef IDE_DRIVER
static void end_request(byte uptodate, byte hwif) {
	struct request *req = ide_cur_rq[HWIF];
#else
static void end_request(int uptodate) {
	struct request *req = CURRENT;
#endif /* IDE_DRIVER */
	struct buffer_head * bh;

	req->errors = 0;
	if (!uptodate) {
		printk("end_request: I/O error, dev %04lX, sector %lu\n",
		       (unsigned long)req->dev, req->sector);
		req->nr_sectors--;
		req->nr_sectors &= ~SECTOR_MASK;
		req->sector += (BLOCK_SIZE / 512);
		req->sector &= ~SECTOR_MASK;		
	}

	if ((bh = req->bh) != NULL) {
		req->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		bh->b_uptodate = uptodate;		
		if (!uptodate) bh->b_req = 0; /* So no "Weird" errors */
		unlock_buffer(bh);
		if ((bh = req->bh) != NULL) {
			req->current_nr_sectors = bh->b_size >> 9;
			if (req->nr_sectors < req->current_nr_sectors) {
				req->nr_sectors = req->current_nr_sectors;
				printk("end_request: buffer-list destroyed\n");
			}
			req->buffer = bh->b_data;
			return;
		}
	}
#ifdef IDE_DRIVER
	ide_cur_rq[HWIF] = NULL;
#else
	DEVICE_OFF(req->dev);
	CURRENT = req->next;
#endif /* IDE_DRIVER */
	if (req->sem != NULL)
		up(req->sem);
	req->dev = -1;
	wake_up(&wait_for_request);
}
#endif /* ! SCSI_MAJOR(MAJOR_NR) */

#endif /* defined(MAJOR_NR) || defined(IDE_DRIVER) */

#endif /* _BLK_H */

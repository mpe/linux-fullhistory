/*
 * linux/drivers/block/ide-cd.c  (ALPHA)
 *
 * 1.00  Oct 31, 1994 -- Initial version.
 * 1.01  Nov  2, 1994 -- Fixed problem with starting request in
 *                       cdrom_check_status.
 * 1.03  Nov 25, 1994 -- leaving unmask_intr[] as a user-setting (as for disks)
 * (from mlord)       -- minor changes to cdrom_setup()
 *                    -- renamed ide_dev_s to ide_dev_t, enable irq on command
 * 2.00  Nov 27, 1994 -- Generalize packet command interface;
 *                       add audio ioctls.
 * 2.01  Dec  3, 1994 -- Rework packet command interface to handle devices
 *                       which send an interrupt when ready for a command.
 * 2.02  Dec 11, 1994 -- Cache the TOC in the driver.
 *                       Don't use SCMD_PLAYAUDIO_TI; it's not included
 *                       in the current version of ATAPI.
 *                       Try to use LBA instead of track or MSF addressing
 *                       when possible.
 *                       Don't wait for READY_STAT.
 *
 * ATAPI cd-rom driver.  To be used with ide.c.
 *
 * Copyright (C) 1994  scott snyder  <snyder@fnald0.fnal.gov>
 */

#include <linux/cdrom.h>

#define BLOCKS_PER_FRAME (CD_FRAMESIZE / 512)

#define OUT_WORDS(b,n)  outsw (IDE_PORT (HD_DATA, dev->hwif), (b), (n))
#define IN_WORDS(b,n)   insw  (IDE_PORT (HD_DATA, dev->hwif), (b), (n))

/* special command codes for strategy routine. */
#define PACKET_COMMAND 4315

#define WIN_PACKETCMD 0xa0  /* Send a packet command. */

/* Some ATAPI command opcodes (just like SCSI).
   (Some other cdrom-specific codes are in cdrom.h.) */
#define TEST_UNIT_READY         0x00
#define REQUEST_SENSE           0x03
#define START_STOP              0x1b
#define ALLOW_MEDIUM_REMOVAL    0x1e
#define READ_10                 0x28
#define MODE_SENSE_10           0x5a
#define MODE_SELECT_10          0x55

struct packet_command {
  char *buffer;
  int buflen;
  int stat;
  unsigned char c[12];
};


struct atapi_request_sense {
  unsigned char error_code : 7;
  unsigned char valid      : 1;
  byte reserved1;
  unsigned char sense_key  : 4;
  unsigned char reserved2  : 1;
  unsigned char ili        : 1;
  unsigned char reserved3  : 2;
  byte info[4];
  byte sense_len;
  byte command_info[4];
  byte asc;
  byte ascq;
  byte fru;
  byte sense_key_specific[3];
};

/* We want some additional flags for cd-rom drives.
   To save space in the ide_dev_t struct, use one of the fields which
   doesn't make sense for cd-roms -- `bios_sect'. */

struct ide_cd_flags {
  unsigned drq_interrupt : 1; /* Device sends an interrupt when ready
                                 for a packet command. */
  unsigned no_playaudio12: 1; /* The PLAYAUDIO12 command is not supported. */

  unsigned media_changed : 1; /* Driver has noticed a media change. */
  unsigned toc_valid     : 1; /* Saved TOC information is current. */
  unsigned reserved : 4;
};

#define CDROM_FLAGS(dev) ((struct ide_cd_flags *)&((dev)->bios_sect))


/* Space to hold the disk TOC. */

#define MAX_TRACKS 99
struct atapi_toc_header {
  unsigned short toc_length;
  byte first_track;
  byte last_track;
};

struct atapi_toc_entry {
  byte reserved1;
  unsigned control : 4;
  unsigned adr     : 4;
  byte track;
  byte reserved2;
  unsigned lba;
};

struct atapi_toc {
  struct atapi_toc_header hdr;
  struct atapi_toc_entry  ent[MAX_TRACKS+1];
};


static struct atapi_toc *cdrom_toc[2][MAX_DRIVES];



/****************************************************************************
 * Generic packet command support routines.
 */

/* Returns 0 if the request should be continued.
   Returns 1 if the request was ended. */
static int cdrom_decode_status (ide_dev_t *dev, int good_stat, int *stat_ret)
{
  struct request *rq = ide_cur_rq[dev->hwif];
  int stat, err;

  /* Check for errors. */
  stat = GET_STAT (dev->hwif);
  *stat_ret = stat;

  if (OK_STAT (stat, good_stat, BAD_RW_STAT))
    return 0;

  /* Got an error. */
  err = IN_BYTE (HD_ERROR, dev->hwif);

  /* Check for tray open */
  if ((err & 0xf0) == 0x20)
    {
      struct packet_command *pc;
      CDROM_FLAGS (dev)->media_changed = 1;
      CDROM_FLAGS (dev)->toc_valid = 0;

      /* Fail the request if this is a read command. */
      if (rq->cmd == READ)
        {
          printk ("%s : tray open\n", dev->name);
          end_request (0, dev->hwif);
        }

      else
        {
          /* Otherwise, it's some other packet command.
             Print an error message to the syslog.
             Exception: don't print anything if this is a read subchannel
             command.  This is because workman constantly polls the drive
             with this command, and we don't want to uselessly fill up
             the syslog. */
          pc = (struct packet_command *)rq->buffer;
          if (pc->c[0] != SCMD_READ_SUBCHANNEL)
            printk ("%s : tray open\n", dev->name);

          /* Set the error flag and complete the request. */
          pc->stat = 1;
          end_request (1, dev->hwif);
        }
    }

  /* Check for media change. */
  else if ((err & 0xf0) == 0x60)
    {
      CDROM_FLAGS (dev)->media_changed = 1;
      CDROM_FLAGS (dev)->toc_valid = 0;
      printk ("%s: media changed\n", dev->name);

      /* We're going to retry this command.
         But be sure to give up if we've retried too many times. */
      if ((++rq->errors > ERROR_MAX))
        {
          end_request (0, dev->hwif);
        }
    }

  /* Don't attempt to retry if this was a packet command. */
  else if (rq->cmd == PACKET_COMMAND)
    {
      struct packet_command *pc = (struct packet_command *)rq->buffer;
      dump_status (dev->hwif, "packet command error", stat);
      pc->stat = 1;  /* signal error */
      end_request (1, dev->hwif);
    }

  /* If there were other errors, go to the default handler. */
  else if ((err & ~ABRT_ERR) != 0)
    {
      ide_error (dev, "cdrom_read_intr", stat);
    }

  /* Else, abort if we've racked up too many retries. */
  else if ((++rq->errors > ERROR_MAX))
    {
      end_request (0, dev->hwif);
    }

  /* Retry, or handle the next request. */
  DO_REQUEST;
  return 1;
}


/* Set up the device registers for transferring a packet command on DEV,
   expecting to later transfer XFERLEN bytes.  This should be followed
   by a call to cdrom_transfer_packet_command; however, if this is a
   drq_interrupt device, one must wait for an interrupt first. */
static int cdrom_start_packet_command (ide_dev_t *dev, int xferlen)
{
  /* Wait for the controller to be idle. */
  if (wait_stat (dev, 0, BUSY_STAT, WAIT_READY)) return 1;

  /* Set up the controller registers. */
  OUT_BYTE (0, HD_FEATURE);
  OUT_BYTE (0, HD_NSECTOR);
  OUT_BYTE (0, HD_SECTOR);

  OUT_BYTE (xferlen & 0xff, HD_LCYL);
  OUT_BYTE (xferlen >> 8  , HD_HCYL);
  OUT_BYTE (dev->ctl, HD_CMD);
  OUT_BYTE (WIN_PACKETCMD, HD_COMMAND); /* packet command */

  return 0;
}


/* Send a packet command to DEV described by CMD_BUF and CMD_LEN.
   The device registers must have already been prepared
   by cdrom_start_packet_command. */
static int cdrom_transfer_packet_command (ide_dev_t *dev,
                                          char *cmd_buf, int cmd_len)
{
  if (CDROM_FLAGS (dev)->drq_interrupt)
    {
      /* Here we should have been called after receiving an interrupt
         from the device.  DRQ should how be set. */
      int stat_dum;

      /* Check for errors. */
      if (cdrom_decode_status (dev, DRQ_STAT, &stat_dum)) return 1;
    }
  else
    {
      /* Otherwise, we must wait for DRQ to get set. */
      if (wait_stat (dev, DRQ_STAT, BAD_STAT, WAIT_READY)) return 1;
    }

  /* Send the command to the device. */
  OUT_WORDS (cmd_buf, cmd_len/2);

  return 0;
}



/****************************************************************************
 * Block read functions.
 */

static int cdrom_start_read (ide_dev_t *dev, unsigned int block);


/*
 * Interrupt routine to read the final status from a transfer.
 */
static void cdrom_read_intr_2 (ide_dev_t *dev)
{
  int stat;
  struct request *rq = ide_cur_rq[dev->hwif];

  stat = GET_STAT (dev->hwif);

  if (OK_STAT (stat, 0, BAD_STAT))
    {
      if (rq->current_nr_sectors <= 0)
        {
          end_request (1, dev->hwif);
        }

      if (rq->current_nr_sectors > 0)
        {
          cdrom_start_read (dev, rq->sector);
          return;
        }
    }
  else
    ide_error (dev, "cdrom_read_intr_2", stat);

  DO_REQUEST;
}


/*
 * Interrupt routine.  Called when a read request has completed.
 */
static void cdrom_read_intr (ide_dev_t *dev)
{
  int stat_dum, len;
  struct request *rq = ide_cur_rq[dev->hwif];

  /* Check for errors. */
  if (cdrom_decode_status (dev, DRQ_STAT, &stat_dum)) return;

  /* Error bit not set.
     Read the device registers to see how much data is waiting. */
  len = IN_BYTE (HD_LCYL, dev->hwif) + 256 * IN_BYTE (HD_HCYL, dev->hwif);

  if (len != CD_FRAMESIZE)
    {
      printk ("cdrom_read_intr: funny value for read length %d\n", len);
      if (len > CD_FRAMESIZE) len = CD_FRAMESIZE;
    }

  IN_WORDS (rq->buffer, len/2);

  rq->current_nr_sectors -= BLOCKS_PER_FRAME;
  rq->nr_sectors -= BLOCKS_PER_FRAME;
  rq->sector += BLOCKS_PER_FRAME;
  rq->buffer += CD_FRAMESIZE;

  /* Wait for another interrupt with the final status. */
  ide_handler[dev->hwif] = cdrom_read_intr_2;
}


/*
 * Routine to send a read packet command to the drive.
 * This is usually called directly from cdrom_start_read.
 * However, for drq_interrupt devices, it is called from an interrupt
 * when the drive is ready to accept the command.
 */
static int cdrom_start_read_continuation (ide_dev_t *dev)
{
  struct packet_command pc;
  struct request *rq = ide_cur_rq[dev->hwif];

  /* Set up the command */
  memset (&pc.c, 0, sizeof (pc.c));
  pc.c[0] = READ_10;
  pc.c[8] = 1;  /* lsb of transfer length */

  /* Write the sector address into the command image. */
  {
    union {
      struct {unsigned char b0, b1, b2, b3;} b;
      struct {unsigned long l0;} l;
    } conv;
    conv.l.l0 = rq->sector / BLOCKS_PER_FRAME;
    pc.c[2] = conv.b.b3;
    pc.c[3] = conv.b.b2;
    pc.c[4] = conv.b.b1;
    pc.c[5] = conv.b.b0;
  }

  if (cdrom_transfer_packet_command (dev, pc.c, sizeof (pc.c)))
    return 1;

  /* Set up our interrupt handler and return. */
  ide_handler[dev->hwif] = cdrom_read_intr;

  return 0;
}


/*
 * Start a read request from the CD-ROM.
 * Returns 0 if the request was started successfully,
 *  1 if there was an error and we should either retry or move on to the
 *  next request.
 */
static int cdrom_start_read (ide_dev_t *dev, unsigned int block)
{
  struct request *rq = ide_cur_rq[dev->hwif];

  if (rq->cmd == READ &&
      (rq->current_nr_sectors != BLOCKS_PER_FRAME ||
       (rq->sector & (BLOCKS_PER_FRAME-1)) != 0))
        {
          printk ("cdrom_start_read: funny request 0x%lx 0x%lx\n",
                  rq->current_nr_sectors, rq->sector);
          end_request (0, dev->hwif);
          return 1;
        }

  if (cdrom_start_packet_command (dev, CD_FRAMESIZE))
    return 1;

  if (CDROM_FLAGS (dev)->drq_interrupt)
    ide_handler[dev->hwif] = (void (*)(ide_dev_t *))cdrom_start_read_continuation;
  else
    {
      if (cdrom_start_read_continuation (dev))
        return 1;
    }

  return 0;
}



/****************************************************************************
 * Execute all other packet commands.
 */

static void cdrom_pc_intr (ide_dev_t *dev)
{
  int ireason, len, stat, thislen;
  struct request *rq = ide_cur_rq[dev->hwif];
  struct packet_command *pc = (struct packet_command *)rq->buffer;

  /* Check for errors. */
  if (cdrom_decode_status (dev, 0, &stat)) return;

  /* Read the interrupt reason and the transfer length. */
  ireason = IN_BYTE (HD_NSECTOR, dev->hwif);
  len = IN_BYTE (HD_LCYL, dev->hwif) + 256 * IN_BYTE (HD_HCYL, dev->hwif);

  /* If DRQ is clear, the command has completed.
     Complain if we still have data left to transfer. */
  if ((stat & DRQ_STAT) == 0)
    {
      if (pc->buflen == 0)
        end_request (1, dev->hwif);
      else
        {
          printk ("%s: cdrom_pc_intr: data underrun %d\n",
                  dev->name, pc->buflen);
          pc->stat = 1;
          end_request (1, dev->hwif);
        }
      DO_REQUEST;
      return;
    }

  /* Figure out how much data to transfer. */
  thislen = pc->buflen;
  if (thislen < 0) thislen = -thislen;
  if (thislen > len) thislen = len;

  /* The drive wants to be written to. */
  if ((ireason & 3) == 0)
    {
      /* Check that we want to write. */
      if (pc->buflen > 0)
        {
          printk ("%s: cdrom_pc_intr: Drive wants to transfer data the wrong way!\n",
                  dev->name);
          pc->stat = 1;
          thislen = 0;
        }

      /* Transfer the data. */
      OUT_WORDS (pc->buffer, thislen / 2);

      /* If we haven't moved enough data to satisfy the drive,
         add some padding. */
      while (len > thislen)
        {
          short dum = 0;
          OUT_WORDS (&dum, 1);
          len -= 2;
        }

      /* Keep count of how much data we've moved. */
      pc->buffer += thislen;
      pc->buflen += thislen;
    }

  /* Same drill for reading. */
  else if ((ireason & 3) == 2)
    {
      /* Check that we want to read. */
      if (pc->buflen < 0)
        {
          printk ("%s: cdrom_pc_intr: Drive wants to transfer data the wrong way!\n",
                  dev->name);
          pc->stat = 1;
          thislen = 0;
        }

      /* Transfer the data. */
      IN_WORDS (pc->buffer, thislen / 2);

      /* If we haven't moved enough data to satisfy the drive,
         add some padding. */
      while (len > thislen)
        {
          short dum = 0;
          IN_WORDS (&dum, 1);
          len -= 2;
        }

      /* Keep count of how much data we've moved. */
      pc->buffer += thislen;
      pc->buflen -= thislen;
    }

  else
    {
      printk ("%s: cdrom_pc_intr: The drive appears confused (ireason = 0x%2x)\n",
              dev->name, ireason);
      pc->stat = 1;
    }

  /* Now we wait for another interrupt. */
  ide_handler[dev->hwif] = cdrom_pc_intr;
}


static int cdrom_do_pc_continuation (ide_dev_t *dev)
{
  struct request *rq = ide_cur_rq[dev->hwif];
  struct packet_command *pc = (struct packet_command *)rq->buffer;

  if (cdrom_transfer_packet_command (dev, pc->c, sizeof (pc->c)))
    return 1;

  /* Set up our interrupt handler and return. */
  ide_handler[dev->hwif] = cdrom_pc_intr;

  return 0;
}


static int cdrom_do_packet_command (ide_dev_t *dev)
{
  int len;
  struct request *rq = ide_cur_rq[dev->hwif];
  struct packet_command *pc = (struct packet_command *)rq->buffer;

  len = pc->buflen;
  if (len < 0) len = -len;

  pc->stat = 0;

  if (cdrom_start_packet_command (dev, len))
    return 1;

  if (CDROM_FLAGS (dev)->drq_interrupt)
    ide_handler[dev->hwif] = (void (*)(ide_dev_t *))cdrom_do_pc_continuation;
  else
    {
      if (cdrom_do_pc_continuation (dev))
        return 1;
    }

  return 0;
}


static
int cdrom_queue_packet_command (ide_dev_t *dev, struct packet_command *pc)
{
  unsigned long flags;
  struct request req, **p, **pfirst;
  struct semaphore sem = MUTEX_LOCKED;
  int major = ide_major[dev->hwif];

  req.dev = MKDEV (major, (dev->select.b.drive) << PARTN_BITS);
  req.cmd = PACKET_COMMAND;
  req.errors = 0;
  req.sector = 0;
  req.nr_sectors = 0;
  req.current_nr_sectors = 0;
  req.buffer = (char *)pc;
  req.sem = &sem;
  req.bh = NULL;
  req.bhtail = NULL;
  req.next = NULL;

  save_flags (flags);
  cli ();

  p = &blk_dev[major].current_request;
  pfirst = p;
  while ((*p) != NULL)
    {
      p = &((*p)->next);
    }
  *p = &req;
  if (p == pfirst)
    blk_dev[major].request_fn ();

  restore_flags (flags);

  down (&sem);

  if (pc->stat != 0)
    return -EIO;
  else
    return 0;
}



/****************************************************************************
 * cdrom driver request routine.
 */

static int do_rw_cdrom (ide_dev_t *dev, unsigned long block)
{
  struct request *rq = ide_cur_rq[dev->hwif];

  if (rq -> cmd == PACKET_COMMAND)
    return cdrom_do_packet_command (dev);

  if (rq -> cmd != READ)
    {
      printk ("ide-cd: bad cmd %d\n", rq -> cmd);
      end_request (0, dev->hwif);
      return 1;
    }

  if (rq->cmd == READ)
    {
      /* This can happen if there was an I/O error on the previous
         buffer in this request. */
      if ((rq->nr_sectors & (BLOCKS_PER_FRAME-1)) == BLOCKS_PER_FRAME-1 &&
          (rq->sector & (BLOCKS_PER_FRAME-1)) == 1 &&
          (rq->current_nr_sectors & (BLOCKS_PER_FRAME-1)) == 0)
        {
          rq->nr_sectors &= (BLOCKS_PER_FRAME-1);
          rq->sector = (rq->sector+BLOCKS_PER_FRAME) & (BLOCKS_PER_FRAME-1);
        }
    }

  return cdrom_start_read (dev, block);
}



/****************************************************************************
 * ioctl handling.
 */

static inline
void byte_swap_word (unsigned short *x)
{
  char *c = (char *)x;
  char d = c[0];
  c[0] = c[1];
  c[1] = d;
}


static inline
void byte_swap_long (unsigned *x)
{
  char *c = (char *)x;
  char d = c[0];
  c[0] = c[3];
  c[3] = d;
  d = c[1];
  c[1] = c[2];
  c[2] = d;
}


static
int bin2bcd (int x)
{
  return (x%10) | ((x/10) << 4);
}


static inline
void lba_to_msf (int lba, byte *m, byte *s, byte *f)
{
  lba += CD_BLOCK_OFFSET;
  lba &= 0xffffff;  /* negative lbas use only 24 bits */
  *m = lba / (CD_SECS * CD_FRAMES);
  lba %= (CD_SECS * CD_FRAMES);
  *s = lba / CD_FRAMES;
  *f = lba % CD_FRAMES;
}


static inline
int msf_to_lba (byte m, byte s, byte f)
{
  return (((m * CD_SECS) + s) * CD_FRAMES + f) - CD_BLOCK_OFFSET;
}


static void
cdrom_check_status (ide_dev_t  *dev)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = TEST_UNIT_READY;

  (void) cdrom_queue_packet_command (dev, &pc);
}


static int
cdrom_request_sense (ide_dev_t *dev, struct atapi_request_sense *reqbuf)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = REQUEST_SENSE;
  pc.c[4] = sizeof (*reqbuf);
  pc.buffer = (char *)reqbuf;
  pc.buflen = sizeof (*reqbuf);

  return cdrom_queue_packet_command (dev, &pc);
}


#if 0
/* Lock the door if LOCKFLAG is nonzero; unlock it otherwise. */
static int
cdrom_lockdoor (ide_dev_t *dev, int lockflag)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = ALLOW_MEDIUM_REMOVAL;
  pc.c[4] = (lockflag != 0);
  return cdrom_queue_packet_command (dev, &pc);
}
#endif


/* Eject the disk if EJECTFLAG is 0.
   If EJECTFLAG is 1, try to reload the disk. */
static int
cdrom_eject (ide_dev_t *dev, int ejectflag)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = START_STOP;
  pc.c[4] = 2 + (ejectflag != 0);
  return cdrom_queue_packet_command (dev, &pc);
}


static int
cdrom_pause (ide_dev_t *dev, int pauseflag)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = SCMD_PAUSE_RESUME;
  pc.c[8] = !pauseflag;
  return cdrom_queue_packet_command (dev, &pc);
}


static int
cdrom_startstop (ide_dev_t *dev, int startflag)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = START_STOP;
  pc.c[1] = 1;
  pc.c[4] = startflag;
  return cdrom_queue_packet_command (dev, &pc);
}


static int
cdrom_read_tocentry (ide_dev_t *dev, int trackno, int msf_flag,
                     char *buf, int buflen)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.buffer =  buf;
  pc.buflen = buflen;
  pc.c[0] = SCMD_READ_TOC;
  pc.c[6] = trackno;
  pc.c[7] = (buflen >> 8);
  pc.c[8] = (buflen & 0xff);
  if (msf_flag) pc.c[1] = 2;
  return cdrom_queue_packet_command (dev, &pc);
}


/* Try to read the entire TOC for the disk into our internal buffer. */
static int
cdrom_read_toc (ide_dev_t *dev)
{
  int stat, ntracks, i;
  struct atapi_toc *toc = cdrom_toc[dev->hwif][dev->select.b.drive];

  if (toc == NULL)
    {
      /* Try to allocate space. */
      toc = (struct atapi_toc *) kmalloc (sizeof (struct atapi_toc),
                                          GFP_KERNEL);
      cdrom_toc[dev->hwif][dev->select.b.drive] = toc;
    }

  if (toc == NULL)
    {
      printk ("%s: No cdrom TOC buffer!\n", dev->name);
      return -EIO;
    }

  /* Check to see if the existing data is still valid.
     If it is, just return. */
  if (CDROM_FLAGS (dev)->toc_valid)
    cdrom_check_status (dev);

  if (CDROM_FLAGS (dev)->toc_valid) return 0;

  /* First read just the header, so we know how long the TOC is. */
  stat = cdrom_read_tocentry (dev, 0, 0, (char *)toc,
                              sizeof (struct atapi_toc_header) +
                              sizeof (struct atapi_toc_entry));
  if (stat) return stat;

  ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;
  if (ntracks <= 0) return -EIO;
  if (ntracks > MAX_TRACKS) ntracks = MAX_TRACKS;

  /* Now read the whole schmeer. */
  stat = cdrom_read_tocentry (dev, 0, 0, (char *)toc,
                              sizeof (struct atapi_toc_header) +
                              (ntracks+1) * sizeof (struct atapi_toc_entry));
  if (stat) return stat;
  byte_swap_word (&toc->hdr.toc_length);
  for (i=0; i<=ntracks; i++)
    byte_swap_long (&toc->ent[i].lba);

  /* Remember that we've read this stuff. */
  CDROM_FLAGS (dev)->toc_valid = 1;

  return 0;
}


static int
cdrom_read_subchannel (ide_dev_t *dev,
                       char *buf, int buflen)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.buffer =  buf;
  pc.buflen = buflen;
  pc.c[0] = SCMD_READ_SUBCHANNEL;
  pc.c[2] = 0x40;  /* request subQ data */
  pc.c[3] = 0x01;  /* Format 1: current position */
  pc.c[7] = (buflen >> 8);
  pc.c[8] = (buflen & 0xff);
  return cdrom_queue_packet_command (dev, &pc);
}


/* modeflag: 0 = current, 1 = changeable mask, 2 = default, 3 = saved */
static int
cdrom_mode_sense (ide_dev_t *dev, int pageno, int modeflag,
                  char *buf, int buflen)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.buffer =  buf;
  pc.buflen = buflen;
  pc.c[0] = MODE_SENSE_10;
  pc.c[2] = pageno | (modeflag << 6);
  pc.c[7] = (buflen >> 8);
  pc.c[8] = (buflen & 0xff);
  return cdrom_queue_packet_command (dev, &pc);
}


static int
cdrom_mode_select (ide_dev_t *dev, char *buf, int buflen)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.buffer =  buf;
  pc.buflen = - buflen;
  pc.c[0] = MODE_SELECT_10;
  pc.c[1] = 0x10;
  pc.c[7] = (buflen >> 8);
  pc.c[8] = (buflen & 0xff);
  return cdrom_queue_packet_command (dev, &pc);
}


static int
cdrom_play_lba_range_play12 (ide_dev_t *dev, int lba_start, int lba_end)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = SCMD_PLAYAUDIO12;
  *(int *)(&pc.c[2]) = lba_start;
  *(int *)(&pc.c[6]) = lba_end - lba_start;
  byte_swap_long ((int *)(&pc.c[2]));
  byte_swap_long ((int *)(&pc.c[6]));

  return cdrom_queue_packet_command (dev, &pc);
}


static int
cdrom_play_lba_range_msf (ide_dev_t *dev, int lba_start, int lba_end)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = SCMD_PLAYAUDIO_MSF;
  lba_to_msf (lba_start, &pc.c[3], &pc.c[4], &pc.c[5]);
  lba_to_msf (lba_end-1, &pc.c[6], &pc.c[7], &pc.c[8]);

  pc.c[3] = bin2bcd (pc.c[3]);
  pc.c[4] = bin2bcd (pc.c[4]);
  pc.c[5] = bin2bcd (pc.c[5]);
  pc.c[6] = bin2bcd (pc.c[6]);
  pc.c[7] = bin2bcd (pc.c[7]);
  pc.c[8] = bin2bcd (pc.c[8]);

  return cdrom_queue_packet_command (dev, &pc);
}


/* Play audio starting at LBA LBA_START and finishing with the
   LBA before LBA_END. */
static int
cdrom_play_lba_range (ide_dev_t *dev, int lba_start, int lba_end)
{
  /* This is rather annoying.
     My NEC-260 won't recognize group 5 commands such as PLAYAUDIO12;
     the only way to get it to play more than 64k of blocks at once
     seems to be the PLAYAUDIO_MSF command.  However, the parameters
     the NEC 260 wants for the PLAYMSF command are incompatible with
     the new version of the spec.

     So what i'll try is this.  First try for PLAYAUDIO12.  If it works,
     great.  Otherwise, if the drive reports an illegal command code,
     try PLAYAUDIO_MSF using the NEC 260-style bcd parameters. */

  if (CDROM_FLAGS (dev)->no_playaudio12)
    return cdrom_play_lba_range_msf (dev, lba_start, lba_end);
  else
    {
      int stat, stat2;
      struct atapi_request_sense reqbuf;

      stat = cdrom_play_lba_range_play12 (dev, lba_start, lba_end);
      if (stat == 0) return 0;

      /* It failed.  Try to find out why. */
      stat2 = cdrom_request_sense (dev, &reqbuf);
      if (stat2) return stat;

      if (reqbuf.sense_key == 0x05 && reqbuf.asc == 0x20)
        {
          /* The drive didn't recognize the command.
             Retry with the MSF variant. */
          printk ("%s: Drive does not support PLAYAUDIO12; "
                  "trying PLAYAUDIO_MSF\n", dev->name);
          CDROM_FLAGS (dev)->no_playaudio12 = 1;
          return cdrom_play_lba_range_msf (dev, lba_start, lba_end);
        }

      /* Failed for some other reason.  Give up. */
      return stat;
    }
}


static
int cdrom_get_toc_entry (ide_dev_t *dev, int track,
                         struct atapi_toc_entry **ent)
{
  int stat, ntracks;
  struct atapi_toc *toc;

  /* Make sure our saved TOC is valid. */
  stat = cdrom_read_toc (dev);
  if (stat) return stat;

  toc = cdrom_toc[dev->hwif][dev->select.b.drive];

  /* Check validity of requested track number. */
  ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;
  if (track == CDROM_LEADOUT)
    *ent = &toc->ent[ntracks];
  else if (track < toc->hdr.first_track ||
           track > toc->hdr.last_track)
    return -EINVAL;
  else
    *ent = &toc->ent[track - toc->hdr.first_track];

  return 0;
}


static int ide_cdrom_ioctl (ide_dev_t *dev, struct inode *inode,
                        struct file *file, unsigned int cmd, unsigned long arg)
{
  switch (cmd)
    {
    case CDROMEJECT:
      return cdrom_eject (dev, 0);

    case CDROMPAUSE:
      return cdrom_pause (dev, 1);

    case CDROMRESUME:
      return cdrom_pause (dev, 0);

    case CDROMSTART:
      return cdrom_startstop (dev, 1);

    case CDROMSTOP:
      return cdrom_startstop (dev, 0);

    case CDROMPLAYMSF:
      {
        struct cdrom_msf msf;
        int stat, lba_start, lba_end;

        stat = verify_area (VERIFY_READ, (void *)arg, sizeof (msf));
        if (stat) return stat;

        memcpy_fromfs (&msf, (void *) arg, sizeof(msf));

        lba_start = msf_to_lba (msf.cdmsf_min0, msf.cdmsf_sec0,
                                msf.cdmsf_frame0);
        lba_end = msf_to_lba (msf.cdmsf_min1, msf.cdmsf_sec1,
                              msf.cdmsf_frame1) + 1;

        if (lba_end <= lba_start) return -EINVAL;

        return cdrom_play_lba_range (dev, lba_start, lba_end);
      }

    /* Like just about every other Linux cdrom driver, we ignore the
       index part of the request here. */
    case CDROMPLAYTRKIND:
      {
        int stat, lba_start, lba_end;
        struct cdrom_ti ti;
        struct atapi_toc_entry *first_toc, *last_toc;

        stat = verify_area (VERIFY_READ, (void *)arg, sizeof (ti));
        if (stat) return stat;

        memcpy_fromfs (&ti, (void *) arg, sizeof(ti));

        stat = cdrom_get_toc_entry (dev, ti.cdti_trk0, &first_toc);
        if (stat) return stat;
        stat = cdrom_get_toc_entry (dev, ti.cdti_trk1, &last_toc);
        if (stat) return stat;

        if (ti.cdti_trk1 != CDROM_LEADOUT) ++last_toc;
        lba_start = first_toc->lba;
        lba_end   = last_toc->lba;

        if (lba_end <= lba_start) return -EINVAL;

        return cdrom_play_lba_range (dev, lba_start, lba_end);
      }

    case CDROMREADTOCHDR:
      {
        int stat;
        struct cdrom_tochdr tochdr;
        struct atapi_toc *toc;

        stat = verify_area (VERIFY_WRITE, (void *) arg, sizeof (tochdr));
        if (stat) return stat;

        /* Make sure our saved TOC is valid. */
        stat = cdrom_read_toc (dev);
        if (stat) return stat;

        toc = cdrom_toc[dev->hwif][dev->select.b.drive];
        tochdr.cdth_trk0 = toc->hdr.first_track;
        tochdr.cdth_trk1 = toc->hdr.last_track;

        memcpy_tofs ((void *) arg, &tochdr, sizeof (tochdr));

        return stat;
      }

    case CDROMREADTOCENTRY:
      {
        int stat;
        struct cdrom_tocentry tocentry;
        struct atapi_toc_entry *toce;

        stat = verify_area (VERIFY_READ, (void *) arg, sizeof (tocentry));
        if (stat) return stat;
        stat = verify_area (VERIFY_WRITE, (void *) arg, sizeof (tocentry));
        if (stat) return stat;

        memcpy_fromfs (&tocentry, (void *) arg, sizeof (tocentry));

        stat = cdrom_get_toc_entry (dev, tocentry.cdte_track, &toce);
        if (stat) return stat;

        tocentry.cdte_ctrl = toce->control;
        tocentry.cdte_adr  = toce->adr;

        if (tocentry.cdte_format == CDROM_MSF)
          {
            /* convert to MSF */
            lba_to_msf (toce->lba,
                        &tocentry.cdte_addr.msf.minute,
                        &tocentry.cdte_addr.msf.second,
                        &tocentry.cdte_addr.msf.frame);
          }
        else
          tocentry.cdte_addr.lba = toce->lba;

        memcpy_tofs ((void *) arg, &tocentry, sizeof (tocentry));

        return stat;
      }

    case CDROMSUBCHNL:
      {
        char buffer[16];
        int stat, abs_lba, rel_lba;
        struct cdrom_subchnl subchnl;

        stat = verify_area (VERIFY_WRITE, (void *) arg, sizeof (subchnl));
        if (stat) return stat;
        stat = verify_area (VERIFY_READ, (void *) arg, sizeof (subchnl));
        if (stat) return stat;

        memcpy_fromfs (&subchnl, (void *) arg, sizeof (subchnl));

        stat = cdrom_read_subchannel (dev, buffer, sizeof (buffer));
        if (stat) return stat;

        abs_lba = *(int *)&buffer[8];
        rel_lba = *(int *)&buffer[12];
        byte_swap_long (&abs_lba);
        byte_swap_long (&rel_lba);

        if (subchnl.cdsc_format == CDROM_MSF)
          {
            lba_to_msf (abs_lba,
                        &subchnl.cdsc_absaddr.msf.minute,
                        &subchnl.cdsc_absaddr.msf.second,
                        &subchnl.cdsc_absaddr.msf.frame);
            lba_to_msf (rel_lba,
                        &subchnl.cdsc_reladdr.msf.minute,
                        &subchnl.cdsc_reladdr.msf.second,
                        &subchnl.cdsc_reladdr.msf.frame);
          }
        else
          {
            subchnl.cdsc_absaddr.lba = abs_lba;
            subchnl.cdsc_reladdr.lba = rel_lba;
          }

        subchnl.cdsc_audiostatus = buffer[1];
        subchnl.cdsc_ctrl = buffer[5] & 0xf;
        subchnl.cdsc_trk = buffer[6];
        subchnl.cdsc_ind = buffer[7];

        memcpy_tofs ((void *) arg, &subchnl, sizeof (subchnl));

        return stat;
      }

    case CDROMVOLCTRL:
      {
        struct cdrom_volctrl volctrl;
        char buffer[24], mask[24];
        int stat;

        stat = verify_area (VERIFY_READ, (void *) arg, sizeof (volctrl));
        if (stat) return stat;
        memcpy_fromfs (&volctrl, (void *) arg, sizeof (volctrl));

        stat = cdrom_mode_sense (dev, 0x0e, 0, buffer, sizeof (buffer));
        if (stat) return stat;
        stat = cdrom_mode_sense (dev, 0x0e, 1, mask  , sizeof (buffer));
        if (stat) return stat;

        buffer[1] = buffer[2] = 0;

        buffer[17] = volctrl.channel0 & mask[17];
        buffer[19] = volctrl.channel1 & mask[19];
        buffer[21] = volctrl.channel2 & mask[21];
        buffer[23] = volctrl.channel3 & mask[23];

        return cdrom_mode_select (dev, buffer, sizeof (buffer));
      }

#ifdef TEST
    case 0x1234:
      {
        int stat;
        struct packet_command pc;

        memset (&pc, 0, sizeof (pc));

        stat = verify_area (VERIFY_READ, (void *) arg, sizeof (pc.c));
        if (stat) return stat;
        memcpy_fromfs (&pc.c, (void *) arg, sizeof (pc.c));

        return cdrom_queue_packet_command (dev, &pc);
      }

    case 0x1235:
      {
        int stat;
        struct atapi_request_sense reqbuf;

        stat = verify_area (VERIFY_WRITE, (void *) arg, sizeof (reqbuf));
        if (stat) return stat;

        stat = cdrom_request_sense (dev, &reqbuf);

        memcpy_tofs ((void *) arg, &reqbuf, sizeof (reqbuf));

        return stat;
      }
#endif

    default:
      return -EPERM;
    }

}



/****************************************************************************
 * Other driver requests (open, close, check media change).
 */

static int cdrom_check_media_change (ide_dev_t *dev)
{
  int retval;

  cdrom_check_status (dev);

  retval = CDROM_FLAGS (dev)->media_changed;
  CDROM_FLAGS (dev)->media_changed = 0;

  return retval;
}


static int
cdrom_open (struct inode *ip, struct file *fp, ide_dev_t *dev)
{
  /* no write access */
  if (fp->f_mode & 2) return -EROFS;

#if 0 /* With this, one cannot eject a disk with workman */
  /* If this is the first open, lock the door. */
  if (dev->usage == 1)
    (void) cdrom_lockdoor (dev, 1);
#endif

  /* Should check that there's a disk in the drive? */
  return 0;
}


/*
 * Close down the device.  Invalidate all cached blocks.
 */

static void
cdrom_release (struct inode *inode, struct file *file, ide_dev_t *dev)
{
  if (dev->usage == 0)
    {
      invalidate_buffers (inode->i_rdev);

#if 0
      /* Unlock the door. */
      (void) cdrom_lockdoor (dev, 0);
#endif
    }
}



/****************************************************************************
 * Device initialization.
 */

static void cdrom_setup (ide_dev_t *dev)
{
  /* Just guess at capacity for now. */
  ide_capacity[dev->hwif][dev->select.b.drive] = 0x1fffff;

  ide_blksizes[dev->hwif][dev->select.b.drive << PARTN_BITS] = CD_FRAMESIZE;

  dev->special.all = 0;

  CDROM_FLAGS (dev)->media_changed = 0;
  CDROM_FLAGS (dev)->toc_valid     = 0;

  CDROM_FLAGS (dev)->no_playaudio12 = 0;
  CDROM_FLAGS (dev)->drq_interrupt = ((dev->id->config & 0x0060) == 0x20);

  cdrom_toc[dev->hwif][dev->select.b.drive] = NULL;
}




/*
 * TODO:
 *  Retrieve and interpret extended ATAPI error codes.
 *  Transfer multiple sectors at once.
 *  Read actual disk capacity.
 *  Support demand-paged executables (1k block sizes?).
 *  Multisession support.
 *  Direct reading of audio data.
 *  Eject-on-dismount.
 *  Lock door while there's a mounted volume.
 */


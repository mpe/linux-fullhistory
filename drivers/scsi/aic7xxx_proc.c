/*+M*************************************************************************
 * Adaptec AIC7xxx device driver proc support for Linux.
 *
 * Copyright (c) 1995, 1996 Dean W. Gehnert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * ----------------------------------------------------------------
 *  o Modified from the EATA-DMA /proc support.
 *  o Additional support for device block statistics provided by
 *    Matthew Jacob.
 *  o Correction of overflow by Heinz Mauelshagen
 *  o Adittional corrections by Doug Ledford
 *
 *  Dean W. Gehnert, deang@teleport.com, 05/01/96
 *
 *  $Id: aic7xxx_proc.c,v 4.1 1997/06/97 08:23:42 deang Exp $
 *-M*************************************************************************/

#define	BLS	(&aic7xxx_buffer[size])
#define HDRB \
"        < 512 512-1K   1-2K   2-4K   4-8K  8-16K 16-32K 32-64K 64-128K >128K"

#ifdef PROC_DEBUG
extern int vsprintf(char *, const char *, va_list);

static void
proc_debug(const char *fmt, ...)
{
  va_list ap;
  char buf[256];

  va_start(ap, fmt);
  vsprintf(buf, fmt, ap);
  printk(buf);
  va_end(ap);
}
#else /* PROC_DEBUG */
#  define proc_debug(fmt, args...)
#endif /* PROC_DEBUG */

static int aic7xxx_buffer_size = 0;
static char *aic7xxx_buffer = NULL;
static const char *bus_names[] = { "Single", "Twin", "Wide" };
static const char *chip_names[] = { "AIC-777x", "AIC-785x", "AIC-786x",
   "AIC-787x", "AIC-788x" };


/*+F*************************************************************************
 * Function:
 *   aic7xxx_set_info
 *
 * Description:
 *   Set parameters for the driver from the /proc filesystem.
 *-F*************************************************************************/
int
aic7xxx_set_info(char *buffer, int length, struct Scsi_Host *HBAptr)
{
  proc_debug("aic7xxx_set_info(): %s\n", buffer);
  return (-ENOSYS);  /* Currently this is a no-op */
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_proc_info
 *
 * Description:
 *   Return information to handle /proc support for the driver.
 *-F*************************************************************************/
int
aic7xxx_proc_info ( char *buffer, char **start, off_t offset, int length, 
                    int hostno, int inout)
{
  struct Scsi_Host *HBAptr;
  struct aic7xxx_host *p;
  int    found = FALSE;
  int    size = 0;
  unsigned char i;
#ifdef AIC7XXX_PROC_STATS
  struct aic7xxx_xferstats *sp;
  unsigned char target, lun;
#endif

  HBAptr = NULL;
  for (i=0; i < NUMBER(aic7xxx_boards); i++)
  {
    if ((HBAptr = aic7xxx_boards[i]) != NULL)
    {
      if (HBAptr->host_no == hostno)
      {
        break;
      }

      while ((HBAptr->hostdata != NULL) && !found &&
          ((HBAptr = ((struct aic7xxx_host *) HBAptr->hostdata)->next) != NULL))
      {
        if (HBAptr->host_no == hostno)
        {
          found = TRUE;
        }
      }

      if (!found)
      {
        HBAptr = NULL;
      }
      else
      {
        break;
      }
    }
  }

  if (HBAptr == NULL)
  {
    size += sprintf(buffer, "Can't find adapter for host number %d\n", hostno);
    if (size > length)
    {
      return (size);
    }
    else
    {
      return (length);
    }
  }

  if (inout == TRUE) /* Has data been written to the file? */ 
  {
    return (aic7xxx_set_info(buffer, length, HBAptr));
  }

  p = (struct aic7xxx_host *) HBAptr->hostdata;

  /*
   * It takes roughly 1K of space to hold all relevant card info, not
   * counting any proc stats, so we start out with a 1.5k buffer size and
   * if proc_stats is defined, then we sweep the stats structure to see
   * how many drives we will be printing out for and add 384 bytes per
   * device with active stats.
   */

  size = 1536;
#ifdef AIC7XXX_PROC_STATS
  for (target = 0; target < MAX_TARGETS; target++)
  {
    for (lun = 0; lun < MAX_LUNS; lun++)
    {
      if (p->stats[target][lun].xfers != 0)
        size += 384;
    }
  }
#endif
  if (aic7xxx_buffer_size != size)
  {
    if (aic7xxx_buffer != NULL) 
    {
      kfree(aic7xxx_buffer);
      aic7xxx_buffer_size = 0;
    }
    aic7xxx_buffer = kmalloc(size, GFP_KERNEL);
  }
  if (aic7xxx_buffer == NULL)
  {
    size = sprintf(buffer, "AIC7xxx - kmalloc error at line %d\n",
        __LINE__);
    return size;
  }
  aic7xxx_buffer_size = size;

  size = 0;
  size += sprintf(BLS, "Adaptec AIC7xxx driver version: ");
  size += sprintf(BLS, "%s/", rcs_version(AIC7XXX_C_VERSION));
  size += sprintf(BLS, "%s", rcs_version(AIC7XXX_H_VERSION));
#if 0
  size += sprintf(BLS, "%s\n", rcs_version(AIC7XXX_SEQ_VER));
#endif
  size += sprintf(BLS, "\n");
  size += sprintf(BLS, "Compile Options:\n");
#ifdef AIC7XXX_RESET_DELAY
  size += sprintf(BLS, "  AIC7XXX_RESET_DELAY    : %d\n", AIC7XXX_RESET_DELAY);
#endif
#ifdef AIC7XXX_CMDS_PER_LUN
  size += sprintf(BLS, "  AIC7XXX_CMDS_PER_LUN   : %d\n", AIC7XXX_CMDS_PER_LUN);
#endif
#ifdef AIC7XXX_TAGGED_QUEUEING
  size += sprintf(BLS, "  AIC7XXX_TAGGED_QUEUEING: Enabled\n");
#else
  size += sprintf(BLS, "  AIC7XXX_TAGGED_QUEUEING: Disabled\n");
#endif
#ifdef AIC7XXX_PAGE_ENABLE
  size += sprintf(BLS, "  AIC7XXX_PAGE_ENABLE    : Enabled\n");
#else
  size += sprintf(BLS, "  AIC7XXX_PAGE_ENABLE    : Disabled\n");
#endif
#ifdef AIC7XXX_PROC_STATS
  size += sprintf(BLS, "  AIC7XXX_PROC_STATS     : Enabled\n");
#else
  size += sprintf(BLS, "  AIC7XXX_PROC_STATS     : Disabled\n");
#endif
  size += sprintf(BLS, "\n");
  size += sprintf(BLS, "Adapter Configuration:\n");
  size += sprintf(BLS, "           SCSI Adapter: %s\n",
      board_names[p->chip_type]);
  size += sprintf(BLS, "                         (%s chipset)\n",
      chip_names[p->chip_class]);
  size += sprintf(BLS, "               Host Bus: %s\n", bus_names[p->bus_type]);
  size += sprintf(BLS, "                Base IO: %#.4x\n", p->base);
  size += sprintf(BLS, "         Base IO Memory: 0x%x\n", p->mbase);
  size += sprintf(BLS, "                    IRQ: %d\n", HBAptr->irq);
  size += sprintf(BLS, "                   SCBs: Used %d, HW %d, Page %d\n",
      p->scb_data->numscbs, p->scb_data->maxhscbs, p->scb_data->maxscbs);
  size += sprintf(BLS, "             Interrupts: %d", p->isr_count);
  if (p->chip_class == AIC_777x)
  {
    size += sprintf(BLS, " %s\n",
        (p->pause & IRQMS) ? "(Level Sensitive)" : "(Edge Triggered)");
  }
  else
  {
    size += sprintf(BLS, "\n");
  }
  size += sprintf(BLS, "          Serial EEPROM: %s\n",
      (p->flags & HAVE_SEEPROM) ? "True" : "False");
  size += sprintf(BLS, "   Extended Translation: %sabled\n",
      (p->flags & EXTENDED_TRANSLATION) ? "En" : "Dis");
  size += sprintf(BLS, "         SCSI Bus Reset: %sabled\n",
      aic7xxx_no_reset ? "Dis" : "En");
  size += sprintf(BLS, "             Ultra SCSI: %sabled\n",
      (p->flags & ULTRA_ENABLED) ? "En" : "Dis");
  size += sprintf(BLS, "Disconnect Enable Flags: 0x%x\n", p->discenable);
  
#ifdef AIC7XXX_PROC_STATS
  size += sprintf(BLS, "\n");
  size += sprintf(BLS, "Statistics:\n");
  for (target = 0; target < MAX_TARGETS; target++)
  {
    for (lun = 0; lun < MAX_LUNS; lun++)
    {
      sp = &p->stats[target][lun];
      if (sp->xfers == 0)
      {
        continue;
      }
      if (p->bus_type == AIC_TWIN)
      {
        size += sprintf(BLS, "CHAN#%c (TGT %d LUN %d):\n",
            'A' + (target >> 3), (target & 0x7), lun);
      }
      else
      {
        size += sprintf(BLS, "CHAN#%c (TGT %d LUN %d):\n",
            'A', target, lun);
      }
      size += sprintf(BLS, "nxfers %ld (%ld read;%ld written)\n",
          sp->xfers, sp->r_total, sp->w_total);
      size += sprintf(BLS, "blks(512) rd=%ld; blks(512) wr=%ld\n",
          sp->r_total512, sp->w_total512);
      size += sprintf(BLS, "%s\n", HDRB);
      size += sprintf(BLS, " Reads:");
      for (i = 0; i < NUMBER(sp->r_bins); i++)
      {
        size += sprintf(BLS, "%6ld ", sp->r_bins[i]);
      }
      size += sprintf(BLS, "\n");
      size += sprintf(BLS, "Writes:");
      for (i = 0; i < NUMBER(sp->w_bins); i++)
      {
        size += sprintf(BLS, "%6ld ", sp->w_bins[i]);
      }
      size += sprintf(BLS, "\n\n");
    }
  }
#endif /* AIC7XXX_PROC_STATS */

  if (size >= aic7xxx_buffer_size)
  {
    printk(KERN_WARNING "aic7xxx: Overflow in aic7xxx_proc.c\n");
  }

  if (offset > size - 1)
  {
    kfree(aic7xxx_buffer);
    aic7xxx_buffer = NULL;
    aic7xxx_buffer_size = length = 0;
    *start = NULL;
  }
  else
  {
    *start = &aic7xxx_buffer[offset];   /* Start of wanted data */
    if (size - offset < length)
    {
      length = size - offset;
    }
  }

  return (length);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

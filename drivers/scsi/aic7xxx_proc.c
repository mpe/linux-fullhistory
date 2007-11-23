/*+M*************************************************************************
 * Adaptec AIC7xxx device driver proc support for Linux.
 *
 * Copyright (c) 1995 Dean W. Gehnert
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
 *  o Modified from the EATA /proc support.
 *  o Additional support for device block statistics provided by
 *    Matthew Jacob.
 *
 *  Dean W. Gehnert, deang@teleport.com, 08/30/95
 *
 *  $Id: aic7xxx_proc.c,v 3.0 1996/04/16 08:52:23 deang Exp $
 *-M*************************************************************************/

#define BLS buffer + len + size
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
aic7xxx_proc_info(char *buffer, char **start, off_t offset, int length, 
    int hostno, int inout)
{
  struct Scsi_Host *HBAptr;
  struct aic7xxx_host *p;
  static u8 buff[512];
  int   i; 
  int   size = 0;
  int   len = 0;
  off_t begin = 0;
  off_t pos = 0;
  static char *bus_names[] = { "Single", "Twin", "Wide" };
  static char *chip_names[] = { "AIC-777x", "AIC-785x", "AIC-787x", "AIC-788x" };

  HBAptr = NULL;
  for (i = 0; i < NUMBER(aic7xxx_boards); i++)
  {
    if ((HBAptr = aic7xxx_boards[i]) != NULL)
    {
      if (HBAptr->host_no == hostno)
      {
        break;
      }

      while ((HBAptr->hostdata != NULL) &&
          ((HBAptr = ((struct aic7xxx_host *) HBAptr->hostdata)->next) != NULL))
      {
        if (HBAptr->host_no == hostno)
        {
          break; break;
        }
      }

      HBAptr = NULL;
    }
  }

  if (HBAptr == NULL)
  {
    size += sprintf(BLS, "Can't find adapter for host number %d\n", hostno);
    len += size; pos = begin + len; size = 0;
    goto stop_output;
  }

  if (inout == TRUE) /* Has data been written to the file? */ 
  {
    return (aic7xxx_set_info(buffer, length, HBAptr));
  }

  if (offset == 0)
  {
    memset(buff, 0, sizeof(buff));
  }

  p = (struct aic7xxx_host *) HBAptr->hostdata;

  size += sprintf(BLS, "Adaptec AIC7xxx driver version: ");
  size += sprintf(BLS, "%s/", rcs_version(AIC7XXX_C_VERSION));
  size += sprintf(BLS, "%s/", rcs_version(AIC7XXX_H_VERSION));
  size += sprintf(BLS, "%s\n", rcs_version(AIC7XXX_SEQ_VER));
  len += size; pos = begin + len; size = 0;

  size += sprintf(BLS, "\n");
  size += sprintf(BLS, "Compile Options:\n");
#ifdef AIC7XXX_RESET_DELAY
  size += sprintf(BLS, "  AIC7XXX_RESET_DELAY    : %d\n", AIC7XXX_RESET_DELAY);
#endif
#ifdef AIC7XXX_CMDS_PER_LUN
  size += sprintf(BLS, "  AIC7XXX_CMDS_PER_LUN   : %d\n", AIC7XXX_CMDS_PER_LUN);
#endif
#ifdef AIC7XXX_TWIN_SUPPORT
  size += sprintf(BLS, "  AIC7XXX_TWIN_SUPPORT   : Enabled\n");
#else
  size += sprintf(BLS, "  AIC7XXX_TWIN_SUPPORT   : Disabled\n");
#endif
#ifdef AIC7XXX_TAGGED_QUEUEING
  size += sprintf(BLS, "  AIC7XXX_TAGGED_QUEUEING: Enabled\n");
#else
  size += sprintf(BLS, "  AIC7XXX_TAGGED_QUEUEING: Disabled\n");
#endif
#ifdef AIC7XXX_SHARE_IRQS
  size += sprintf(BLS, "  AIC7XXX_SHARE_IRQS     : Enabled\n");
#else
  size += sprintf(BLS, "  AIC7XXX_SHARE_IRQS     : Disabled\n");
#endif
#ifdef AIC7XXX_PROC_STATS
  size += sprintf(BLS, "  AIC7XXX_PROC_STATS     : Enabled\n");
#else
  size += sprintf(BLS, "  AIC7XXX_PROC_STATS     : Disabled\n");
#endif
  len += size; pos = begin + len; size = 0;

  size += sprintf(BLS, "\n");
  size += sprintf(BLS, "Adapter Configuration:\n");
  size += sprintf(BLS, "          SCSI Adapter: %s\n", board_names[p->type]);
  size += sprintf(BLS, "                        (%s chipset)\n",
      chip_names[p->chip_type]);
  size += sprintf(BLS, "              Host Bus: %s\n", bus_names[p->bus_type]);
  size += sprintf(BLS, "               Base IO: %#.4x\n", p->base);
  size += sprintf(BLS, "                   IRQ: %d\n", HBAptr->irq);
  size += sprintf(BLS, "                   SCB: %d (%d)\n", p->numscb, p->maxscb);
  size += sprintf(BLS, "            Interrupts: %d", p->isr_count);
  if (p->chip_type == AIC_777x)
  {
    size += sprintf(BLS, " %s\n",
        (p->pause & IRQMS) ? "(Level Sensitive)" : "(Edge Triggered)");
  }
  else
  {
    size += sprintf(BLS, "\n");
  }
  size += sprintf(BLS, "         Serial EEPROM: %s\n",
      p->have_seeprom ? "True" : "False");
  size += sprintf(BLS, "  Extended Translation: %sabled\n",
      p->extended ? "En" : "Dis");
  size += sprintf(BLS, "        SCSI Bus Reset: %sabled\n",
      aic7xxx_no_reset ? "Dis" : "En");
  size += sprintf(BLS, "            Ultra SCSI: %sabled\n",
      p->ultra_enabled ? "En" : "Dis");
  size += sprintf(BLS, "     Target Disconnect: %sabled\n",
      p->discenable ? "En" : "Dis");
  len += size; pos = begin + len; size = 0;

#ifdef AIC7XXX_PROC_STATS
  {
    struct aic7xxx_xferstats *sp;
    int channel, target, lun;

    /*
     * XXX: Need to fix this to avoid overflow...
     */
    size += sprintf(BLS, "\n");
    size += sprintf(BLS, "Statistics:\n");
    for (channel = 0; channel < 2; channel++)
    {
      for (target = 0; target < 16; target++)
      {
        for (lun = 0; lun < 8; lun++)
        {
          sp = &p->stats[channel][target][lun];
          if (sp->xfers == 0)
          {
            continue;
          }
          size += sprintf(BLS, "CHAN#%c (TGT %d LUN %d):\n",
              'A' + channel, target, lun);
          size += sprintf(BLS, "nxfers %ld (%ld read;%ld written)\n",
              sp->xfers, sp->r_total, sp->w_total);
          size += sprintf(BLS, "blks(512) rd=%ld; blks(512) wr=%ld\n",
              sp->r_total512, sp->w_total512);
          size += sprintf(BLS, "%s\n", HDRB);
          size += sprintf(BLS, " Reads:");
          size += sprintf(BLS, "%6ld %6ld %6ld %6ld ", sp->r_bins[0],
              sp->r_bins[1], sp->r_bins[2], sp->r_bins[3]);
          size += sprintf(BLS, "%6ld %6ld %6ld %6ld ", sp->r_bins[4],
              sp->r_bins[5], sp->r_bins[6], sp->r_bins[7]);
          size += sprintf(BLS, "%6ld %6ld\n", sp->r_bins[8],
              sp->r_bins[9]);
          size += sprintf(BLS, "Writes:");
          size += sprintf(BLS, "%6ld %6ld %6ld %6ld ", sp->w_bins[0],
              sp->w_bins[1], sp->w_bins[2], sp->w_bins[3]);
          size += sprintf(BLS, "%6ld %6ld %6ld %6ld ", sp->w_bins[4],
              sp->w_bins[5], sp->w_bins[6], sp->w_bins[7]);
          size += sprintf(BLS, "%6ld %6ld\n", sp->w_bins[8],
              sp->w_bins[9]);
          size += sprintf(BLS, "\n");
        }
      }
    }
    len += size; pos = begin + len; size = 0;
  }
#endif /* AIC7XXX_PROC_STATS */

stop_output:
  proc_debug("2pos: %ld offset: %ld len: %d\n", pos, offset, len);
  *start = buffer + (offset - begin);   /* Start of wanted data */
  len -= (offset - begin);      /* Start slop */
  if (len > length)
  {
    len = length;               /* Ending slop */
  }
  proc_debug("3pos: %ld offset: %ld len: %d\n", pos, offset, len);
  
  return (len);     
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

/* gdth_proc.c 
 * $Id: gdth_proc.c,v 1.11 1998/12/17 15:52:35 achim Exp $
 */

#include "gdth_ioctl.h"

int gdth_proc_info(char *buffer,char **start,off_t offset,int length,   
                   int hostno,int inout)
{
    int hanum,busnum,i;

    TRACE2(("gdth_proc_info() length %d ha %d offs %d inout %d\n",
            length,hostno,(int)offset,inout));

    for (i=0; i<gdth_ctr_vcount; ++i) {
        if (gdth_ctr_vtab[i]->host_no == hostno)
            break;
    }
    if (i==gdth_ctr_vcount)
        return(-EINVAL);

    hanum = NUMDATA(gdth_ctr_vtab[i])->hanum;
    busnum= NUMDATA(gdth_ctr_vtab[i])->busnum;

    if (inout)
        return(gdth_set_info(buffer,length,i,hanum,busnum));
    else
        return(gdth_get_info(buffer,start,offset,length,i,hanum,busnum));
}

static int gdth_set_info(char *buffer,int length,int vh,int hanum,int busnum)
{
    int             ret_val;
    Scsi_Cmnd       scp;
    Scsi_Device     sdev;
    gdth_iowr_str   *piowr;

    TRACE2(("gdth_set_info() ha %d bus %d\n",hanum,busnum));
    piowr = (gdth_iowr_str *)buffer;

    memset(&sdev,0,sizeof(Scsi_Device));
    memset(&scp, 0,sizeof(Scsi_Cmnd));
    sdev.host = gdth_ctr_vtab[vh];
    sdev.id = sdev.host->this_id;
    scp.cmd_len = 12;
    scp.host = gdth_ctr_vtab[vh];
    scp.target = sdev.host->this_id;
    scp.device = &sdev;
    scp.use_sg = 0;

    if (length >= 4) {
        if (strncmp(buffer,"gdth",4) == 0) {
            buffer += 5;
            length -= 5;
            ret_val = gdth_set_asc_info( buffer, length, hanum, scp );
        } else if (piowr->magic == GDTIOCTL_MAGIC) {
            ret_val = gdth_set_bin_info( buffer, length, hanum, scp );
        } else {
            printk("GDT: Wrong signature: %6s\n",buffer);
            ret_val = -EINVAL;
        }
    } else {
        ret_val = -EINVAL;
    }
    return ret_val;
}
         
static int gdth_set_asc_info(char *buffer,int length,int hanum,Scsi_Cmnd scp)
{
    int             orig_length, drive, wb_mode;
    char            cmnd[12];
    int             i, found;
    gdth_ha_str     *ha;
    gdth_cmd_str    gdtcmd;
    gdth_cpar_str   *pcpar;

    TRACE2(("gdth_set_asc_info() ha %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);
    memset(cmnd, 0,10);
    orig_length = length + 5;
    drive = -1;
    wb_mode = 0;
    found = FALSE;

    if (length >= 5 && strncmp(buffer,"flush",5)==0) {
        buffer += 6;
        length -= 6;
        if (length && *buffer>='0' && *buffer<='9') {
            drive = (int)(*buffer-'0');
            ++buffer; --length;
            if (length && *buffer>='0' && *buffer<='9') {
                drive = drive*10 + (int)(*buffer-'0');
                ++buffer; --length;
            }
            printk("GDT: Flushing host drive %d .. ",drive);
        } else {
            printk("GDT: Flushing all host drives .. ");
        }
        for (i = 0; i < MAX_HDRIVES; ++i) {
            if (ha->hdr[i].present) {
                if (drive != -1 && i != drive)
                    continue;
                found = TRUE;
                gdtcmd.BoardNode = LOCALBOARD;
                gdtcmd.Service = CACHESERVICE;
                gdtcmd.OpCode = GDT_FLUSH;
                gdtcmd.u.cache.DeviceNo = i;
                gdtcmd.u.cache.BlockNo = 1;
                gdtcmd.u.cache.sg_canz = 0;
                {
                    struct semaphore sem = MUTEX_LOCKED;
                    scp.request.rq_status = RQ_SCSI_BUSY;
                    scp.request.sem = &sem;
                    scp.SCp.this_residual = IOCTL_PRI;
                    GDTH_LOCK_SCSI_DOCMD();
                    scsi_do_cmd(&scp, cmnd, &gdtcmd,
                                sizeof(gdth_cmd_str), gdth_scsi_done,
                                30*HZ, 1);
                    GDTH_UNLOCK_SCSI_DOCMD();
                    down(&sem);
                }
            }
        }
        if (!found)
            printk("\nNo host drive found !\n");
        else
            printk("Done.\n");
        return(orig_length);
    }

    if (length >= 7 && strncmp(buffer,"wbp_off",7)==0) {
        buffer += 8;
        length -= 8;
        printk("GDT: Disabling write back permanently .. ");
        wb_mode = 1;
    } else if (length >= 6 && strncmp(buffer,"wbp_on",6)==0) {
        buffer += 7;
        length -= 7;
        printk("GDT: Enabling write back permanently .. ");
        wb_mode = 2;
    } else if (length >= 6 && strncmp(buffer,"wb_off",6)==0) {
        buffer += 7;
        length -= 7;
        printk("GDT: Disabling write back commands .. ");
        if (ha->cache_feat & GDT_WR_THROUGH) {
            gdth_write_through = TRUE;
            printk("Done.\n");
        } else {
            printk("Not supported !\n");
        }
        return(orig_length);
    } else if (length >= 5 && strncmp(buffer,"wb_on",5)==0) {
        buffer += 6;
        length -= 6;
        printk("GDT: Enabling write back commands .. ");
        gdth_write_through = FALSE;
        printk("Done.\n");
        return(orig_length);
    }

    if (wb_mode) {
        pcpar = (gdth_cpar_str *)kmalloc( sizeof(gdth_cpar_str),
            GFP_ATOMIC | GFP_DMA );
        if (pcpar == NULL) {
            TRACE2(("gdth_set_info(): Unable to allocate memory.\n"));
            printk("Unable to allocate memory.\n");
            return(-EINVAL);
        }
        memcpy( pcpar, &ha->cpar, sizeof(gdth_cpar_str) );
        gdtcmd.BoardNode = LOCALBOARD;
        gdtcmd.Service = CACHESERVICE;
        gdtcmd.OpCode = GDT_IOCTL;
        gdtcmd.u.ioctl.p_param = virt_to_bus(pcpar);
        gdtcmd.u.ioctl.param_size = sizeof(gdth_cpar_str);
        gdtcmd.u.ioctl.subfunc = CACHE_CONFIG;
        gdtcmd.u.ioctl.channel = INVALID_CHANNEL;
        pcpar->write_back = wb_mode==1 ? 0:1;
        {
            struct semaphore sem = MUTEX_LOCKED;
            scp.request.rq_status = RQ_SCSI_BUSY;
            scp.request.sem = &sem;
            scp.SCp.this_residual = IOCTL_PRI;
            GDTH_LOCK_SCSI_DOCMD();
            scsi_do_cmd(&scp, cmnd, &gdtcmd, sizeof(gdth_cmd_str),
                        gdth_scsi_done, 30*HZ, 1);
            GDTH_UNLOCK_SCSI_DOCMD();
            down(&sem);
        }
        kfree( pcpar );
        printk("Done.\n");
        return(orig_length);
    }

    printk("GDT: Unknown command: %s  Length: %d\n",buffer,length);
    return(-EINVAL);
}

static int gdth_set_bin_info(char *buffer,int length,int hanum,Scsi_Cmnd scp)
{
    char            cmnd[12];
    unchar          i, j;
    gdth_ha_str     *ha;
    gdth_iowr_str   *piowr;
    gdth_iord_str   *piord;
    gdth_cmd_str    *pcmd;
    ulong32         *ppadd, add_size;
    ulong           flags;

    TRACE2(("gdth_set_bin_info() ha %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);
    memset(cmnd, 0,10);
    piowr = (gdth_iowr_str *)buffer;
    piord = NULL;
    pcmd = NULL;

    if (length < GDTOFFSOF(gdth_iowr_str,iu))
        return(-EINVAL);

    switch (piowr->ioctl) {
      case GDTIOCTL_GENERAL:
        if (length < GDTOFFSOF(gdth_iowr_str,iu.general.data[0]))
            return(-EINVAL);
        pcmd = (gdth_cmd_str *)piowr->iu.general.command;
        pcmd->Service = piowr->service;
        if (pcmd->OpCode == GDT_IOCTL) {
            ppadd = &pcmd->u.ioctl.p_param;
            add_size = pcmd->u.ioctl.param_size;
        } else if (piowr->service == CACHESERVICE) {
            add_size = pcmd->u.cache.BlockCnt * SECTOR_SIZE;
            if (ha->cache_feat & SCATTER_GATHER) {
                ppadd = &pcmd->u.cache.sg_lst[0].sg_ptr;
                pcmd->u.cache.DestAddr = 0xffffffff;
                pcmd->u.cache.sg_lst[0].sg_len = add_size;
                pcmd->u.cache.sg_canz = 1;
            } else {
                ppadd = &pcmd->u.cache.DestAddr;
                pcmd->u.cache.sg_canz = 0;
            }
        } else if (piowr->service == SCSIRAWSERVICE) {
            add_size = pcmd->u.raw.sdlen;
            if (ha->raw_feat & SCATTER_GATHER) {
                ppadd = &pcmd->u.raw.sg_lst[0].sg_ptr;
                pcmd->u.raw.sdata = 0xffffffff;
                pcmd->u.raw.sg_lst[0].sg_len = add_size;
                pcmd->u.raw.sg_ranz = 1;
            } else {
                ppadd = &pcmd->u.raw.sdata;
                pcmd->u.raw.sg_ranz = 0;
            }
        } else {
            return(-EINVAL);
        }
        if (!gdth_ioctl_alloc( hanum, sizeof(gdth_iord_str) + add_size ))
            return(-EBUSY);
        piord = (gdth_iord_str *)ha->pscratch;

        piord->size = sizeof(gdth_iord_str) + add_size;
        if (add_size > 0) {
            memcpy(piord->iu.general.data, piowr->iu.general.data, add_size);
            *ppadd = virt_to_bus(piord->iu.general.data);
        }
        /* do IOCTL */
        {
            struct semaphore sem = MUTEX_LOCKED;
            scp.request.rq_status = RQ_SCSI_BUSY;
            scp.request.sem = &sem;
            scp.SCp.this_residual = IOCTL_PRI;
            GDTH_LOCK_SCSI_DOCMD();
            scsi_do_cmd(&scp, cmnd, pcmd,
                        sizeof(gdth_cmd_str), gdth_scsi_done,
                        piowr->timeout*HZ, 1);
            GDTH_UNLOCK_SCSI_DOCMD();
            down(&sem);
            piord->status = (ulong32)scp.SCp.Message;
        }
        break;

      case GDTIOCTL_DRVERS:
        if (!gdth_ioctl_alloc( hanum, sizeof(gdth_iord_str) ))
            return(-EBUSY);
        piord = (gdth_iord_str *)ha->pscratch;
        piord->size = sizeof(gdth_iord_str);
        piord->status = S_OK;
        piord->iu.drvers.version = (GDTH_VERSION<<8) | GDTH_SUBVERSION;
        break;

      case GDTIOCTL_CTRTYPE:
        if (!gdth_ioctl_alloc( hanum, sizeof(gdth_iord_str) ))
            return(-EBUSY);
        piord = (gdth_iord_str *)ha->pscratch;
        piord->size = sizeof(gdth_iord_str);
        piord->status = S_OK;
        if (ha->type == GDT_ISA || ha->type == GDT_EISA) {
            piord->iu.ctrtype.type = (unchar)((ha->stype>>20) - 0x10);
        } else if (ha->type != GDT_PCIMPR) {
            piord->iu.ctrtype.type = (unchar)((ha->stype<<8) + 6);
        } else {
            piord->iu.ctrtype.type = 0xfe;
            piord->iu.ctrtype.ext_type = 0x6000 | ha->stype;
        }
        piord->iu.ctrtype.info = ha->brd_phys;
        piord->iu.ctrtype.oem_id = (ushort)GDT3_ID;
        break;

      case GDTIOCTL_CTRCNT:
        if (!gdth_ioctl_alloc( hanum, sizeof(gdth_iord_str) ))
            return(-EBUSY);
        piord = (gdth_iord_str *)ha->pscratch;
        piord->size = sizeof(gdth_iord_str);
        piord->status = S_OK;
        piord->iu.ctrcnt.count = (ushort)gdth_ctr_count;
        break;

      case GDTIOCTL_OSVERS:
        if (!gdth_ioctl_alloc( hanum, sizeof(gdth_iord_str) ))
            return(-EBUSY);
        piord = (gdth_iord_str *)ha->pscratch;
        piord->size = sizeof(gdth_iord_str);
        piord->status = S_OK;
        piord->iu.osvers.version = (unchar)(LINUX_VERSION_CODE >> 16);
        piord->iu.osvers.subversion = (unchar)(LINUX_VERSION_CODE >> 8);
        piord->iu.osvers.revision = (ushort)(LINUX_VERSION_CODE & 0xff);
        break;

      case GDTIOCTL_LOCKDRV:
        if (!gdth_ioctl_alloc( hanum, sizeof(gdth_iord_str) ))
            return(-EBUSY);
        piord = (gdth_iord_str *)ha->pscratch;
        for (i = 0; i < piowr->iu.lockdrv.drive_cnt; ++i) {
            j = piowr->iu.lockdrv.drives[i];
            if (j >= MAX_HDRIVES || !ha->hdr[j].present) 
                continue;
            if (piowr->iu.lockdrv.lock) {
                GDTH_LOCK_HA(ha, flags);
                ha->hdr[j].lock = 1;
                GDTH_UNLOCK_HA(ha, flags);
                gdth_wait_completion( hanum, ha->bus_cnt, j );
                gdth_stop_timeout( hanum, ha->bus_cnt, j );
            } else {
                GDTH_LOCK_HA(ha, flags);
                ha->hdr[j].lock = 0;
                GDTH_UNLOCK_HA(ha, flags);
                gdth_start_timeout( hanum, ha->bus_cnt, j );
                gdth_next( hanum );
            }
        }
        piord->size = sizeof(gdth_iord_str);
        piord->status = S_OK;
        break;

      case GDTIOCTL_LOCKCHN:
        if (!gdth_ioctl_alloc( hanum, sizeof(gdth_iord_str) ))
            return(-EBUSY);
        i = piowr->iu.lockchn.channel;
        if (i < ha->bus_cnt) {
            if (piowr->iu.lockchn.lock) {
                GDTH_LOCK_HA(ha, flags);
                ha->raw[i].lock = 1;
                GDTH_UNLOCK_HA(ha, flags);
                for (j = 0; j < ha->tid_cnt; ++j) {
                    gdth_wait_completion( hanum, i, j );
                    gdth_stop_timeout( hanum, i, j );
                }
            } else {
                GDTH_LOCK_HA(ha, flags);
                ha->raw[i].lock = 0;
                GDTH_UNLOCK_HA(ha, flags);
                for (j = 0; j < ha->tid_cnt; ++j) {
                    gdth_start_timeout( hanum, i, j );
                    gdth_next( hanum );
                }
            }
        }
        piord = (gdth_iord_str *)ha->pscratch;
        piord->size = sizeof(gdth_iord_str);
        piord->status = S_OK;
        break;

      case GDTIOCTL_EVENT:
        if (!gdth_ioctl_alloc( hanum, sizeof(gdth_iord_str) ))
            return(-EBUSY);
        piord = (gdth_iord_str *)ha->pscratch;
        if (piowr->iu.event.erase == 0xff) {
            gdth_store_event(ha, 
                             ((gdth_evt_str *)piowr->iu.event.evt)->event_source,
                             ((gdth_evt_str *)piowr->iu.event.evt)->event_idx,
                             &((gdth_evt_str *)piowr->iu.event.evt)->event_data);
            if (((gdth_evt_str *)piowr->iu.event.evt)->event_source == ES_ASYNC)
                gdth_log_event(&((gdth_evt_str *)piowr->iu.event.evt)->event_data);
        } else if (piowr->iu.event.erase == 0xfe) {
            gdth_clear_events();
        } else if (piowr->iu.event.erase == 0) {
            piord->iu.event.handle = 
                gdth_read_event(ha,piowr->iu.event.handle,
                                (gdth_evt_str *)piord->iu.event.evt);
        } else {
            piord->iu.event.handle = piowr->iu.event.handle;
            gdth_readapp_event(ha, (unchar)piowr->iu.event.erase,
                               (gdth_evt_str *)piord->iu.event.evt);
        }
        piord->size = sizeof(gdth_iord_str);
        piord->status = S_OK;
        break;

      default:
        return(-EINVAL);
    }
    /* we return a buffer ID to detect the right buffer during READ-IOCTL */
    return 1;
}

static int gdth_get_info(char *buffer,char **start,off_t offset,
                         int length,int vh,int hanum,int busnum)
{
    int size = 0,len = 0;
    off_t begin = 0,pos = 0;
    gdth_ha_str *ha;
    gdth_iord_str *piord;
    int id;

    TRACE2(("gdth_get_info() ha %d bus %d\n",hanum,busnum));
    ha = HADATA(gdth_ctr_tab[hanum]);
    id = length;

    /* look for buffer ID in length */
    if (id > 1) {
#if LINUX_VERSION_CODE >= 0x020000
        size = sprintf(buffer+len,
                       "%s Disk Array Controller\n",
                       ha->ctr_name);
#else
        size = sprintf(buffer+len,
                       "%s Disk Array Controller (Bus %d)\n",
                       ha->ctr_name,busnum);
#endif
        len += size;  pos = begin + len;
        size = sprintf(buffer+len,
                       "Firmware Version: %d.%2d\tDriver Version: %s\n",
                       (unchar)(ha->cpar.version>>8),
                       (unchar)(ha->cpar.version),GDTH_VERSION_STR);
        len += size;  pos = begin + len;
 
        if (pos < offset) {
            len = 0;
            begin = pos;
        }
        if (pos > offset + length)
            goto stop_output;

    } else {
        piord = (gdth_iord_str *)ha->pscratch;
        if (piord == NULL)
            goto stop_output;
        length = piord->size;
        memcpy(buffer+len, (char *)piord, length);
        gdth_ioctl_free(hanum);
        len += length; pos = begin + len;

        if (pos < offset) {
            len = 0;
            begin = pos;
        }
        if (pos > offset + length)
            goto stop_output;
    }

stop_output:
    *start = buffer +(offset-begin);
    len -= (offset-begin);
    if (len > length)
        len = length;
    TRACE2(("get_info() len %d pos %d begin %d offset %d length %d size %d\n",
            len,(int)pos,(int)begin,(int)offset,length,size));
    return(len);
}


void gdth_scsi_done(Scsi_Cmnd *scp)
{
    TRACE2(("gdth_scsi_done()\n"));

    scp->request.rq_status = RQ_SCSI_DONE;

    if (scp->request.sem != NULL)
        up(scp->request.sem);
}

static int gdth_ioctl_alloc(int hanum, ushort size)
{
    gdth_ha_str *ha;
    ulong flags;
    int ret_val;

    if (size == 0 || size > GDTH_SCRATCH)
        return -1;

    ha = HADATA(gdth_ctr_tab[hanum]);
    GDTH_LOCK_HA(ha, flags);

    if (!ha->scratch_busy) {
        ha->scratch_busy = TRUE;
        ret_val = TRUE;
    } else
        ret_val = FALSE;

    GDTH_UNLOCK_HA(ha, flags);
    return ret_val;
}

static void gdth_ioctl_free(int hanum)
{
    gdth_ha_str *ha;
    ulong flags;

    ha = HADATA(gdth_ctr_tab[hanum]);
    GDTH_LOCK_HA(ha, flags);

    ha->scratch_busy = FALSE;

    GDTH_UNLOCK_HA(ha, flags);
}

static void gdth_wait_completion(int hanum, int busnum, int id)
{
    gdth_ha_str *ha;
    ulong flags;
    int i;
    Scsi_Cmnd *scp;

    ha = HADATA(gdth_ctr_tab[hanum]);
    GDTH_LOCK_HA(ha, flags);

    for (i = 0; i < GDTH_MAXCMDS; ++i) {
        scp = ha->cmd_tab[i].cmnd;
#if LINUX_VERSION_CODE >= 0x020000
        if (!SPECIAL_SCP(scp) && scp->target == (unchar)id &&
            scp->channel == (unchar)busnum)
#else
        if (!SPECIAL_SCP(scp) && scp->target == (unchar)id &&
            NUMDATA(scp->host)->busnum == (unchar)busnum)
#endif
        {
            scp->SCp.have_data_in = 0;
            GDTH_UNLOCK_HA(ha, flags);
            while (!scp->SCp.have_data_in)
                barrier();
            GDTH_LOCK_SCSI_DONE(flags);
            scp->scsi_done(scp);
            GDTH_UNLOCK_SCSI_DONE(flags);
        GDTH_LOCK_HA(ha, flags);
        }
    }
    GDTH_UNLOCK_HA(ha, flags);
}

static void gdth_stop_timeout(int hanum, int busnum, int id)
{
    gdth_ha_str *ha;
    ulong flags;
    Scsi_Cmnd *scp;

    ha = HADATA(gdth_ctr_tab[hanum]);
    GDTH_LOCK_HA(ha, flags);

    for (scp = ha->req_first; scp; scp = (Scsi_Cmnd *)scp->SCp.ptr) {
#if LINUX_VERSION_CODE >= 0x020000
        if (scp->target == (unchar)id &&
            scp->channel == (unchar)busnum)
#else
        if (scp->target == (unchar)id &&
            NUMDATA(scp->host)->busnum == (unchar)busnum)
#endif
        {
            TRACE2(("gdth_stop_timeout(): update_timeout()\n"));
            scp->SCp.buffers_residual = gdth_update_timeout(hanum, scp, 0);
        }
    }
    GDTH_UNLOCK_HA(ha, flags);
}

static void gdth_start_timeout(int hanum, int busnum, int id)
{
    gdth_ha_str *ha;
    ulong flags;
    Scsi_Cmnd *scp;

    ha = HADATA(gdth_ctr_tab[hanum]);
    GDTH_LOCK_HA(ha, flags);

    for (scp = ha->req_first; scp; scp = (Scsi_Cmnd *)scp->SCp.ptr) {
#if LINUX_VERSION_CODE >= 0x020000
        if (scp->target == (unchar)id &&
            scp->channel == (unchar)busnum)
#else
        if (scp->target == (unchar)id &&
            NUMDATA(scp->host)->busnum == (unchar)busnum)
#endif
        {
            TRACE2(("gdth_start_timeout(): update_timeout()\n"));
            gdth_update_timeout(hanum, scp, scp->SCp.buffers_residual);
        }
    }
    GDTH_UNLOCK_HA(ha, flags);
}

static int gdth_update_timeout(int hanum, Scsi_Cmnd *scp, int timeout)
{
    int oldto;

    oldto = scp->timeout_per_command;
    scp->timeout_per_command = timeout;

#if LINUX_VERSION_CODE >= 0x02014B
    if (timeout == 0) {
        del_timer(&scp->eh_timeout);
        scp->eh_timeout.data = (unsigned long) NULL;
        scp->eh_timeout.expires = 0;
    } else {
        if (scp->eh_timeout.data != (unsigned long) NULL) 
            del_timer(&scp->eh_timeout);
        scp->eh_timeout.data = (unsigned long) scp;
        scp->eh_timeout.expires = jiffies + timeout;
        add_timer(&scp->eh_timeout);
    }
#else
    if (timeout > 0) {
        if (timer_table[SCSI_TIMER].expires == 0) {
            timer_table[SCSI_TIMER].expires = jiffies + timeout;
            timer_active |= 1 << SCSI_TIMER;
        } else {
            if (jiffies + timeout < timer_table[SCSI_TIMER].expires)
                timer_table[SCSI_TIMER].expires = jiffies + timeout;
        }
    }
#endif

    return oldto;
}

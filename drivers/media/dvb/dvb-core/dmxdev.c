/* 
 * dmxdev.c - DVB demultiplexer device 
 *
 * Copyright (C) 2000 Ralph  Metzler <ralph@convergence.de>
 *                  & Marcus Metzler <marcus@convergence.de>
                      for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/videodev.h>
#include <asm/uaccess.h>

#include "dmxdev.h"


//MODULE_DESCRIPTION("");
//MODULE_AUTHOR("Ralph Metzler, Marcus Metzler");
//#ifdef MODULE_LICENSE
//MODULE_LICENSE("GPL");
//#endif
MODULE_PARM(debug,"i");
static int debug = 0;

#define dprintk	if (debug) printk

inline dmxdev_filter_t *
dvb_dmxdev_file_to_filter(struct file *file)
{
        return (dmxdev_filter_t *) file->private_data;
}

inline dmxdev_dvr_t *
dvb_dmxdev_file_to_dvr(dmxdev_t *dmxdev, struct file *file)
{
        return (dmxdev_dvr_t *) file->private_data;
}

static inline void 
dvb_dmxdev_buffer_init(dmxdev_buffer_t *buffer) 
{
        buffer->data=0;
        buffer->size=8192;
        buffer->pread=0;
        buffer->pwrite=0;
        buffer->error=0;
        init_waitqueue_head(&buffer->queue);
}

static inline int 
dvb_dmxdev_buffer_write(dmxdev_buffer_t *buf, uint8_t *src, int len) 
{
        int split;
        int free;
        int todo;

	if (!len)
	        return 0;
	if (!buf->data)
	        return 0;
	
        free=buf->pread-buf->pwrite;
        split=0;
        if (free<=0) {
                free+=buf->size;
                split=buf->size-buf->pwrite;
        }
        if (len>=free) {
		dprintk("dmxdev: buffer overflow\n");
                return -1;
	}
        if (split>=len)
                split=0;
        todo=len;
        if (split) {
                memcpy(buf->data + buf->pwrite, src, split);
                todo-=split;
                buf->pwrite=0;
        }
        memcpy(buf->data + buf->pwrite, src+split, todo);
        buf->pwrite=(buf->pwrite+todo)%buf->size;
        return len;
}

static ssize_t 
dvb_dmxdev_buffer_read(dmxdev_buffer_t *src, int non_blocking, 
		 char *buf, size_t count, loff_t *ppos)
{
        unsigned long todo=count;
        int split, avail, error;
	
	if (!src->data)
	        return 0;

	if ((error=src->error)) {
		src->pwrite=src->pread;
	        src->error=0;
		return error; 
	}

	if (non_blocking && (src->pwrite==src->pread))
	        return -EWOULDBLOCK;

        while (todo>0) {
	        if (non_blocking && (src->pwrite==src->pread))
		        return (count-todo) ? (count-todo) : -EWOULDBLOCK;

	        if (wait_event_interruptible(src->queue,
					     (src->pread!=src->pwrite) ||
					     (src->error))<0)
		        return count-todo;

		if ((error=src->error)) {
			src->pwrite=src->pread;
		        src->error=0;
			return error; 
		}
		
                split=src->size;
                avail=src->pwrite - src->pread;
                if (avail<0) {
                        avail+=src->size;
                        split=src->size - src->pread;
                }
                if (avail>todo)
                        avail=todo;
                if (split<avail) {
                        if (copy_to_user(buf, src->data+src->pread, split))
                                  return -EFAULT;
                        buf+=split;
                        src->pread=0;
                        todo-=split;
                        avail-=split;
                }
                if (avail) {
                        if (copy_to_user(buf, src->data+src->pread, avail))
                                return -EFAULT;
                        src->pread = (src->pread + avail) % src->size;
                        todo-=avail;
                        buf+=avail;
                }
        }
        return count;
}

static dmx_frontend_t *
get_fe(dmx_demux_t *demux, int type)
{
        struct list_head *head, *pos;

        head=demux->get_frontends(demux);
	if (!head)
	        return 0;
	list_for_each(pos, head)
	        if (DMX_FE_ENTRY(pos)->source==type)
		        return DMX_FE_ENTRY(pos);
	
	return 0;
}

static inline void 
dvb_dmxdev_dvr_state_set(dmxdev_dvr_t *dmxdevdvr, int state)
{
        spin_lock_irq(&dmxdevdvr->dev->lock);
        dmxdevdvr->state=state;
	spin_unlock_irq(&dmxdevdvr->dev->lock);
}

static int dvb_dvr_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
	dmxdev_t *dmxdev=(dmxdev_t *) dvbdev->priv;
        dmx_frontend_t *front;

        dprintk ("function : %s\n", __FUNCTION__);

        if (down_interruptible (&dmxdev->mutex))
		return -ERESTARTSYS;
	
	if ((file->f_flags&O_ACCMODE)==O_RDWR) {
	        if (!(dmxdev->capabilities&DMXDEV_CAP_DUPLEX)) {
			up(&dmxdev->mutex);
		        return -EOPNOTSUPP;
		}
	}

	if ((file->f_flags&O_ACCMODE)==O_RDONLY) {
	      dvb_dmxdev_buffer_init(&dmxdev->dvr_buffer);
	      dmxdev->dvr_buffer.size=DVR_BUFFER_SIZE;
	      dmxdev->dvr_buffer.data=vmalloc(DVR_BUFFER_SIZE);
	      if (!dmxdev->dvr_buffer.data) {
		      up(&dmxdev->mutex);
	              return -ENOMEM;
	      }
	}

	if ((file->f_flags&O_ACCMODE)==O_WRONLY) {
	        dmxdev->dvr_orig_fe=dmxdev->demux->frontend;
		
		if (!dmxdev->demux->write) {
			up(&dmxdev->mutex);
			return -EOPNOTSUPP;
		}
		
		front=get_fe(dmxdev->demux, DMX_MEMORY_FE);
		
		if (!front) {
			up(&dmxdev->mutex);
		        return -EINVAL;
		}
		dmxdev->demux->disconnect_frontend(dmxdev->demux);	
		dmxdev->demux->connect_frontend(dmxdev->demux, front);	
	}
        up(&dmxdev->mutex);
        return 0;
}

static int dvb_dvr_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
	dmxdev_t *dmxdev=(dmxdev_t *) dvbdev->priv;

        if (down_interruptible (&dmxdev->mutex))
		return -ERESTARTSYS;

	if ((file->f_flags&O_ACCMODE)==O_WRONLY) {
	        dmxdev->demux->disconnect_frontend(dmxdev->demux);	
		dmxdev->demux->connect_frontend(dmxdev->demux, 
						dmxdev->dvr_orig_fe);
	}
	if ((file->f_flags&O_ACCMODE)==O_RDONLY) {
		if (dmxdev->dvr_buffer.data) {
		        void *mem=dmxdev->dvr_buffer.data;
			mb();
			spin_lock_irq(&dmxdev->lock);
			dmxdev->dvr_buffer.data=0;
			spin_unlock_irq(&dmxdev->lock);
		        vfree(mem);
		}
	}
        up(&dmxdev->mutex);
	return 0;
}

static ssize_t 
dvb_dvr_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
	dmxdev_t *dmxdev=(dmxdev_t *) dvbdev->priv;
	int ret;

        if (!dmxdev->demux->write)
	        return -EOPNOTSUPP;
	if ((file->f_flags&O_ACCMODE)!=O_WRONLY)
	        return -EINVAL;
        if (down_interruptible (&dmxdev->mutex))
		return -ERESTARTSYS;
        ret=dmxdev->demux->write(dmxdev->demux, buf, count);
        up(&dmxdev->mutex);
	return ret;
}

static ssize_t 
dvb_dvr_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
	dmxdev_t *dmxdev=(dmxdev_t *) dvbdev->priv;
	int ret;

        //down(&dmxdev->mutex);
        ret= dvb_dmxdev_buffer_read(&dmxdev->dvr_buffer, 
			      file->f_flags&O_NONBLOCK, 
			      buf, count, ppos);
        //up(&dmxdev->mutex);
	return ret;
}

static inline void 
dvb_dmxdev_filter_state_set(dmxdev_filter_t *dmxdevfilter, int state)
{
        spin_lock_irq(&dmxdevfilter->dev->lock);
        dmxdevfilter->state=state;
	spin_unlock_irq(&dmxdevfilter->dev->lock);
}

static int 
dvb_dmxdev_set_buffer_size(dmxdev_filter_t *dmxdevfilter, unsigned long size)
{
	dmxdev_buffer_t *buf=&dmxdevfilter->buffer;
	void *mem;

	if (buf->size==size)
	        return 0;
        if (dmxdevfilter->state>=DMXDEV_STATE_GO)
                return -EBUSY;
	spin_lock_irq(&dmxdevfilter->dev->lock);
	mem=buf->data;
	buf->data=0;
	buf->size=size;
	buf->pwrite=buf->pread=0;    
	spin_unlock_irq(&dmxdevfilter->dev->lock);
        if (mem) 
		vfree(mem);
		
        if (buf->size) {
	        mem=vmalloc(dmxdevfilter->buffer.size);
		if (!mem)
		        return -ENOMEM;
	        spin_lock_irq(&dmxdevfilter->dev->lock);
                buf->data=mem;
	        spin_unlock_irq(&dmxdevfilter->dev->lock);
	}
	return 0;
}

static void
dvb_dmxdev_filter_timeout(unsigned long data)
{
        dmxdev_filter_t *dmxdevfilter=(dmxdev_filter_t *)data;
	
	dmxdevfilter->buffer.error=-ETIMEDOUT;
        spin_lock_irq(&dmxdevfilter->dev->lock);
	dmxdevfilter->state=DMXDEV_STATE_TIMEDOUT;
        spin_unlock_irq(&dmxdevfilter->dev->lock);
	wake_up(&dmxdevfilter->buffer.queue);
}

static void
dvb_dmxdev_filter_timer(dmxdev_filter_t *dmxdevfilter)
{
        struct dmx_sct_filter_params *para=&dmxdevfilter->params.sec;
  
	del_timer(&dmxdevfilter->timer);
	if (para->timeout) {
	        dmxdevfilter->timer.function=dvb_dmxdev_filter_timeout;
		dmxdevfilter->timer.data=(unsigned long) dmxdevfilter;
		dmxdevfilter->timer.expires=jiffies+1+(HZ/2+HZ*para->timeout)/1000;
		add_timer(&dmxdevfilter->timer);
	}
}

static int 
dvb_dmxdev_section_callback(u8 *buffer1, size_t buffer1_len,
		      u8 *buffer2, size_t buffer2_len,
		      dmx_section_filter_t *filter,
		      dmx_success_t success)
{
        dmxdev_filter_t *dmxdevfilter=(dmxdev_filter_t *) filter->priv;
        int ret;
        
	if (dmxdevfilter->buffer.error) {
		wake_up(&dmxdevfilter->buffer.queue);
	        return 0;
	}
	spin_lock(&dmxdevfilter->dev->lock);
	if (dmxdevfilter->state!=DMXDEV_STATE_GO) {
		spin_unlock(&dmxdevfilter->dev->lock);
	        return 0;
	}
	del_timer(&dmxdevfilter->timer);
	dprintk("dmxdev: section callback %02x %02x %02x %02x %02x %02x\n", 
		buffer1[0], buffer1[1], 
		buffer1[2], buffer1[3], 
		buffer1[4], buffer1[5]);
        ret=dvb_dmxdev_buffer_write(&dmxdevfilter->buffer, buffer1, buffer1_len);
        if (ret==buffer1_len) {
	        ret=dvb_dmxdev_buffer_write(&dmxdevfilter->buffer, buffer2, buffer2_len);
	}
        if (ret<0) {
	        dmxdevfilter->buffer.pwrite=dmxdevfilter->buffer.pread;    
	        dmxdevfilter->buffer.error=-EOVERFLOW;
	}
	if (dmxdevfilter->params.sec.flags&DMX_ONESHOT)
	        dmxdevfilter->state=DMXDEV_STATE_DONE;
	spin_unlock(&dmxdevfilter->dev->lock);
	wake_up(&dmxdevfilter->buffer.queue);
	return 0;
}

static int 
dvb_dmxdev_ts_callback(u8 *buffer1, size_t buffer1_len,
		 u8 *buffer2, size_t buffer2_len,
		 dmx_ts_feed_t *feed,
		 dmx_success_t success)
{
        dmxdev_filter_t *dmxdevfilter=(dmxdev_filter_t *) feed->priv;
	dmxdev_buffer_t *buffer;
        int ret;
        
	spin_lock(&dmxdevfilter->dev->lock);
	if (dmxdevfilter->params.pes.output==DMX_OUT_DECODER) {
		spin_unlock(&dmxdevfilter->dev->lock);
		return 0;
	}

	if (dmxdevfilter->params.pes.output==DMX_OUT_TAP)
	        buffer=&dmxdevfilter->buffer;
	else
	        buffer=&dmxdevfilter->dev->dvr_buffer;
	if (buffer->error) {
		spin_unlock(&dmxdevfilter->dev->lock);
		wake_up(&buffer->queue);
	        return 0;
	}
        ret=dvb_dmxdev_buffer_write(buffer, buffer1, buffer1_len);
        if (ret==buffer1_len) 
	        ret=dvb_dmxdev_buffer_write(buffer, buffer2, buffer2_len);
        if (ret<0) {
	        buffer->pwrite=buffer->pread;    
	        buffer->error=-EOVERFLOW;
	}
	spin_unlock(&dmxdevfilter->dev->lock);
	wake_up(&buffer->queue);
	return 0;
}


/* stop feed but only mark the specified filter as stopped (state set) */

static int 
dvb_dmxdev_feed_stop(dmxdev_filter_t *dmxdevfilter)
{
	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_SET);

	switch (dmxdevfilter->type) {
	case DMXDEV_TYPE_SEC:
	        del_timer(&dmxdevfilter->timer);
	        dmxdevfilter->feed.sec->stop_filtering(dmxdevfilter->feed.sec);
		break;
	case DMXDEV_TYPE_PES:
	        dmxdevfilter->feed.ts->stop_filtering(dmxdevfilter->feed.ts);
		break;
	default:
	        return -EINVAL;
	}
        return 0;
}


/* start feed associated with the specified filter */

static int 
dvb_dmxdev_feed_start(dmxdev_filter_t *dmxdevfilter)
{
	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_GO);

	switch (dmxdevfilter->type) {
	case DMXDEV_TYPE_SEC:
	        dmxdevfilter->feed.sec->start_filtering(dmxdevfilter->feed.sec);
		break;
	case DMXDEV_TYPE_PES:
	        dmxdevfilter->feed.ts->start_filtering(dmxdevfilter->feed.ts);
		break;
	default:
	        return -EINVAL;
	}
        return 0;
}


/* restart section feed if it has filters left associated with it, 
   otherwise release the feed */

static int 
dvb_dmxdev_feed_restart(dmxdev_filter_t *dmxdevfilter)
{
	int i;
	dmxdev_t *dmxdev=dmxdevfilter->dev;
	uint16_t pid=dmxdevfilter->params.sec.pid;
	
	for (i=0; i<dmxdev->filternum; i++) 
		if (dmxdev->filter[i].state>=DMXDEV_STATE_GO &&
		    dmxdev->filter[i].type==DMXDEV_TYPE_SEC &&
		    dmxdev->filter[i].pid==pid) {
			dvb_dmxdev_feed_start(&dmxdev->filter[i]);
			return 0;
		}
	
	dmxdevfilter->dev->demux->
		release_section_feed(dmxdev->demux,
				     dmxdevfilter->feed.sec);

        return 0;
}

static int 
dvb_dmxdev_filter_stop(dmxdev_filter_t *dmxdevfilter)
{
        if (dmxdevfilter->state<DMXDEV_STATE_GO) 
	        return 0;

	switch (dmxdevfilter->type) {
	case DMXDEV_TYPE_SEC:
	        if (!dmxdevfilter->feed.sec)
		        break;
	        dvb_dmxdev_feed_stop(dmxdevfilter);
	        if (dmxdevfilter->filter.sec)
		        dmxdevfilter->feed.sec->
				release_filter(dmxdevfilter->feed.sec,
					       dmxdevfilter->filter.sec);
	        dvb_dmxdev_feed_restart(dmxdevfilter);
		dmxdevfilter->feed.sec=0;
		break;
	case DMXDEV_TYPE_PES:
	        if (!dmxdevfilter->feed.ts)
		        break;
	        dvb_dmxdev_feed_stop(dmxdevfilter);
	        dmxdevfilter->dev->demux->
			release_ts_feed(dmxdevfilter->dev->demux,
					dmxdevfilter->feed.ts);
		dmxdevfilter->feed.ts=0;
		break;
	default:
		if (dmxdevfilter->state==DMXDEV_STATE_ALLOCATED) 
			return 0;
	        return -EINVAL;
	}
	dmxdevfilter->buffer.pwrite=dmxdevfilter->buffer.pread=0;    
        return 0;
}

static inline int 
dvb_dmxdev_filter_reset(dmxdev_filter_t *dmxdevfilter)
{
	if (dmxdevfilter->state<DMXDEV_STATE_SET) 
		return 0;

	dmxdevfilter->type=DMXDEV_TYPE_NONE;
	dmxdevfilter->pid=0xffff;
	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_ALLOCATED);
        return 0;
}

static int 
dvb_dmxdev_filter_start(dmxdev_filter_t *dmxdevfilter)
{
	dmxdev_t *dmxdev=dmxdevfilter->dev;
	void *mem;
	int ret, i;

        if (dmxdevfilter->state<DMXDEV_STATE_SET) 
	        return -EINVAL;
        if (dmxdevfilter->state>=DMXDEV_STATE_GO)
	        dvb_dmxdev_filter_stop(dmxdevfilter); 

	mem=dmxdevfilter->buffer.data;
	if (!mem) {
                mem=vmalloc(dmxdevfilter->buffer.size);
	        spin_lock_irq(&dmxdevfilter->dev->lock);
		dmxdevfilter->buffer.data=mem;
	        spin_unlock_irq(&dmxdevfilter->dev->lock);
                if (!dmxdevfilter->buffer.data)
                        return -ENOMEM;
	}

	dmxdevfilter->buffer.pwrite=dmxdevfilter->buffer.pread=0;

	switch (dmxdevfilter->type) {
	case DMXDEV_TYPE_SEC:
	{
		struct dmx_sct_filter_params *para=&dmxdevfilter->params.sec;
		dmx_section_filter_t **secfilter=&dmxdevfilter->filter.sec;
		dmx_section_feed_t **secfeed=&dmxdevfilter->feed.sec;

		*secfilter=0;
		*secfeed=0;

		/* find active filter/feed with same PID */
		for (i=0; i<dmxdev->filternum; i++) 
			if (dmxdev->filter[i].state>=DMXDEV_STATE_GO &&
			    dmxdev->filter[i].pid==para->pid) {
				if (dmxdev->filter[i].type!=DMXDEV_TYPE_SEC)
					return -EBUSY;
				*secfeed=dmxdev->filter[i].feed.sec;
				break;
			}

		/* if no feed found, try to allocate new one */ 
		if (!*secfeed) {
			ret=dmxdev->demux->allocate_section_feed(dmxdev->demux, 
						      secfeed, 
					           dvb_dmxdev_section_callback);
			if (ret<0) {
				printk ("DVB (%s): could not alloc feed\n",
					__FUNCTION__);
				return ret;
			}
			
			ret=(*secfeed)->set(*secfeed, para->pid, 32768, 0, 
					    (para->flags & DMX_CHECK_CRC) ? 1 : 0);
			
			if (ret<0) {
				printk ("DVB (%s): could not set feed\n",
					__FUNCTION__);
				dvb_dmxdev_feed_restart(dmxdevfilter);
				return ret;
			}
		}
	        else 
			dvb_dmxdev_feed_stop(dmxdevfilter);
			
		ret=(*secfeed)->allocate_filter(*secfeed, secfilter);
		if (ret<0) {
			dvb_dmxdev_feed_restart(dmxdevfilter);
			dmxdevfilter->feed.sec->start_filtering(*secfeed);
			dprintk ("could not get filter\n");
			return ret;
		}

		(*secfilter)->priv=(void *) dmxdevfilter;

		memcpy(&((*secfilter)->filter_value[3]), 
		       &(para->filter.filter[1]), DMX_FILTER_SIZE-1);
		memcpy(&(*secfilter)->filter_mask[3], 
		       &para->filter.mask[1], DMX_FILTER_SIZE-1);
		memcpy(&(*secfilter)->filter_mode[3], 
		       &para->filter.mode[1], DMX_FILTER_SIZE-1);

		(*secfilter)->filter_value[0]=para->filter.filter[0];
		(*secfilter)->filter_mask[0]=para->filter.mask[0];
		(*secfilter)->filter_mode[0]=para->filter.mode[0];
		(*secfilter)->filter_mask[1]=0;
		(*secfilter)->filter_mask[2]=0;
		
	        dmxdevfilter->todo=0;
	        dmxdevfilter->feed.sec->start_filtering(dmxdevfilter->feed.sec);
	        dvb_dmxdev_filter_timer(dmxdevfilter);
		break;
	}

	case DMXDEV_TYPE_PES: 
	{
		struct timespec timeout = { 0 };
		struct dmx_pes_filter_params *para=&dmxdevfilter->params.pes;
		dmx_output_t otype;
		int ret;
		int ts_type;
		dmx_ts_pes_t ts_pes;
		dmx_ts_feed_t **tsfeed=&dmxdevfilter->feed.ts;
		
		dmxdevfilter->feed.ts=0;
		otype=para->output;
		
		ts_pes=(dmx_ts_pes_t) para->pes_type;
		
		if (ts_pes<DMX_PES_OTHER) 
			ts_type=TS_DECODER;
		else
			ts_type=0;
		
		if (otype==DMX_OUT_TS_TAP) 
			ts_type|=TS_PACKET;
		
		if (otype==DMX_OUT_TAP) 
			ts_type|=TS_PAYLOAD_ONLY|TS_PACKET;
		
		ret=dmxdev->demux->allocate_ts_feed(dmxdev->demux, 
						    tsfeed, 
						    dvb_dmxdev_ts_callback);
		if (ret<0) 
			return ret;

		(*tsfeed)->priv=(void *) dmxdevfilter;
		ret=(*tsfeed)->set(*tsfeed, para->pid, ts_type, ts_pes, 188, 32768, 0, timeout);
		if (ret<0) {
			dmxdev->demux->release_ts_feed(dmxdev->demux, *tsfeed);
			return ret;
		}
	        dmxdevfilter->feed.ts->start_filtering(dmxdevfilter->feed.ts);
		break;
	}
	default:
	        return -EINVAL;
	}
	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_GO);
        return 0;
}

static int dvb_demux_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
	dmxdev_t *dmxdev=(dmxdev_t *) dvbdev->priv;
        int i;
        dmxdev_filter_t *dmxdevfilter;

	if (!dmxdev->filter)
	        return -EINVAL;
        if (down_interruptible(&dmxdev->mutex))
		return -ERESTARTSYS;
        for (i=0; i<dmxdev->filternum; i++)
                if (dmxdev->filter[i].state==DMXDEV_STATE_FREE)
                        break;
        if (i==dmxdev->filternum) {
	        up(&dmxdev->mutex);
                return -EMFILE;
	}
        dmxdevfilter=&dmxdev->filter[i];
        dmxdevfilter->dvbdev=dmxdev->dvbdev;
	file->private_data=dmxdevfilter;

	dvb_dmxdev_buffer_init(&dmxdevfilter->buffer);
        dmxdevfilter->type=DMXDEV_TYPE_NONE;
	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_ALLOCATED);
	dmxdevfilter->feed.ts=0;
	init_timer(&dmxdevfilter->timer);

        up(&dmxdev->mutex);
        return 0;
}

int 
dvb_dmxdev_filter_free(dmxdev_t *dmxdev, dmxdev_filter_t *dmxdevfilter)
{
        if (down_interruptible(&dmxdev->mutex))
		return -ERESTARTSYS;

        dvb_dmxdev_filter_stop(dmxdevfilter);
	dvb_dmxdev_filter_reset(dmxdevfilter);
        
        if (dmxdevfilter->buffer.data) {
	        void *mem=dmxdevfilter->buffer.data;
		
	        spin_lock_irq(&dmxdev->lock);
		dmxdevfilter->buffer.data=0;
	        spin_unlock_irq(&dmxdev->lock);
		vfree(mem);
	}
	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_FREE);
	wake_up(&dmxdevfilter->buffer.queue);
	up(&dmxdev->mutex);
        return 0;
}

static inline void 
invert_mode(dmx_filter_t *filter)
{
	int i;

	for (i=0; i<DMX_FILTER_SIZE; i++)
		filter->mode[i]^=0xff;
}


static int 
dvb_dmxdev_filter_set(dmxdev_t *dmxdev, 
                dmxdev_filter_t *dmxdevfilter, 
		struct dmx_sct_filter_params *params)
{
        dprintk ("function : %s\n", __FUNCTION__);

	dvb_dmxdev_filter_stop(dmxdevfilter);
        
        dmxdevfilter->type=DMXDEV_TYPE_SEC;
        dmxdevfilter->pid=params->pid;
	memcpy(&dmxdevfilter->params.sec, 
	       params, sizeof(struct dmx_sct_filter_params));
	invert_mode(&dmxdevfilter->params.sec.filter);
	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_SET);
		
        if (params->flags&DMX_IMMEDIATE_START) 
                return dvb_dmxdev_filter_start(dmxdevfilter);

        return 0;
}

static int 
dvb_dmxdev_pes_filter_set(dmxdev_t *dmxdev,
                   dmxdev_filter_t *dmxdevfilter,
                   struct dmx_pes_filter_params *params)
{
	dvb_dmxdev_filter_stop(dmxdevfilter);

	if (params->pes_type>DMX_PES_OTHER || params->pes_type<0)
	        return -EINVAL;

        dmxdevfilter->type=DMXDEV_TYPE_PES;
        dmxdevfilter->pid=params->pid;
	memcpy(&dmxdevfilter->params, params, sizeof(struct dmx_pes_filter_params));

	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_SET);

        if (params->flags&DMX_IMMEDIATE_START) 
                return dvb_dmxdev_filter_start(dmxdevfilter);

        return 0;
}

static ssize_t 
dvb_dmxdev_read_sec(dmxdev_filter_t *dfil, struct file *file, 
	      char *buf, size_t count, loff_t *ppos)
{
        int result, hcount;
	int done=0;
	
	if (dfil->todo<=0) {
	        hcount=3+dfil->todo;
	        if (hcount>count)
		        hcount=count;
		result=dvb_dmxdev_buffer_read(&dfil->buffer, file->f_flags&O_NONBLOCK, 
					buf, hcount, ppos);
		if (result<0) {
			dfil->todo=0;
		        return result;
		}
		if (copy_from_user(dfil->secheader-dfil->todo, buf, result)) 
		        return -EFAULT;
		buf+=result;
		done=result;
		count-=result;
		dfil->todo-=result;
		if (dfil->todo>-3)
		        return done;
		dfil->todo=((dfil->secheader[1]<<8)|dfil->secheader[2])&0xfff;
		if (!count)
		        return done;
	}
	if (count>dfil->todo)
	        count=dfil->todo;
        result=dvb_dmxdev_buffer_read(&dfil->buffer, file->f_flags&O_NONBLOCK, 
				buf, count, ppos);
	if (result<0)
	        return result;
	dfil->todo-=result;
	return (result+done);
}


ssize_t 
dvb_demux_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
        dmxdev_filter_t *dmxdevfilter=dvb_dmxdev_file_to_filter(file);
	//dmxdev_t *dmxdev=dmxdevfilter->dev;
	int ret=0;

	// semaphore should not be necessary (I hope ...)
        //down(&dmxdev->mutex);
	if (dmxdevfilter->type==DMXDEV_TYPE_SEC)
	        ret=dvb_dmxdev_read_sec(dmxdevfilter, file, buf, count, ppos);
	else
	        ret=dvb_dmxdev_buffer_read(&dmxdevfilter->buffer, 
				     file->f_flags&O_NONBLOCK, 
				     buf, count, ppos);
        //up(&dmxdev->mutex);
	return ret;
}


static int dvb_demux_do_ioctl(struct inode *inode, struct file *file,
			      unsigned int cmd, void *parg)
{
        dmxdev_filter_t *dmxdevfilter=dvb_dmxdev_file_to_filter(file);
	dmxdev_t *dmxdev=dmxdevfilter->dev;
        unsigned long arg=(unsigned long) parg;
	int ret=0;
  
        if (down_interruptible (&dmxdev->mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case DMX_START: 
	        if (dmxdevfilter->state<DMXDEV_STATE_SET)
		        ret=-EINVAL;
		else
		        ret=dvb_dmxdev_filter_start(dmxdevfilter);
		break;

	case DMX_STOP: 
		ret=dvb_dmxdev_filter_stop(dmxdevfilter);
		break;

	case DMX_SET_FILTER: 
		ret=dvb_dmxdev_filter_set(dmxdev, dmxdevfilter, 
				    (struct dmx_sct_filter_params *)parg);
		break;

	case DMX_SET_PES_FILTER: 
		ret=dvb_dmxdev_pes_filter_set(dmxdev, dmxdevfilter, 
					       (struct dmx_pes_filter_params *)parg);
		break;

	case DMX_SET_BUFFER_SIZE: 
	        ret=dvb_dmxdev_set_buffer_size(dmxdevfilter, arg);
		break;
        
        case DMX_GET_EVENT: 
	        break;
		
        case DMX_GET_PES_PIDS: 
		if (!dmxdev->demux->get_pes_pids) {
		        ret=-EINVAL;
			break;
		}
		dmxdev->demux->get_pes_pids(dmxdev->demux, (uint16_t *)parg);
	        break;

	default:
	        ret=-EINVAL;
	}
        up(&dmxdev->mutex);
	return ret;
}

static int dvb_demux_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, dvb_demux_do_ioctl);
}


static unsigned int dvb_demux_poll(struct file *file, poll_table *wait)
{
        dmxdev_filter_t *dmxdevfilter=dvb_dmxdev_file_to_filter(file);

	if (!dmxdevfilter)
	        return -EINVAL;

	if (dmxdevfilter->state==DMXDEV_STATE_FREE)
	        return 0;

	if (dmxdevfilter->buffer.error)
	        return (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

	if (dmxdevfilter->buffer.pread!=dmxdevfilter->buffer.pwrite)
	        return (POLLIN | POLLRDNORM | POLLPRI);

	if (dmxdevfilter->state!=DMXDEV_STATE_GO)
	        return 0;

	poll_wait(file, &dmxdevfilter->buffer.queue, wait);
                
	if (dmxdevfilter->state==DMXDEV_STATE_FREE)
	        return 0;

	if (dmxdevfilter->buffer.error)
	        return (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

	if (dmxdevfilter->buffer.pread!=dmxdevfilter->buffer.pwrite)
	        return (POLLIN | POLLRDNORM | POLLPRI);

	return 0;
}

static int dvb_demux_release(struct inode *inode, struct file *file)
{
        dmxdev_filter_t *dmxdevfilter=dvb_dmxdev_file_to_filter(file);
	dmxdev_t *dmxdev=dmxdevfilter->dev;

	return dvb_dmxdev_filter_free(dmxdev, dmxdevfilter);
}

static struct file_operations dvb_demux_fops = {
	.owner		= THIS_MODULE,
	.read		= dvb_demux_read,
	.ioctl		= dvb_demux_ioctl,
	.open		= dvb_demux_open,
	.release	= dvb_demux_release,
	.poll		= dvb_demux_poll,
};

static struct dvb_device dvbdev_demux = {
	.priv		= 0,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_demux_fops
};

static int dvb_dvr_do_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
	dmxdev_t *dmxdev=(dmxdev_t *) dvbdev->priv;

	int ret=0;

        if (down_interruptible (&dmxdev->mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case DMX_SET_BUFFER_SIZE: 
		// FIXME: implement
	        ret=0;
		break;
		
	default:
	        ret=-EINVAL;
	}
        up(&dmxdev->mutex);
	return ret;
}

static int dvb_dvr_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, dvb_dvr_do_ioctl);
}


static unsigned int dvb_dvr_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
	dmxdev_t *dmxdev=(dmxdev_t *) dvbdev->priv;

        dprintk ("function : %s\n", __FUNCTION__);

        if ((file->f_flags&O_ACCMODE)==O_RDONLY) {
		if (dmxdev->dvr_buffer.error)
			return (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

		if (dmxdev->dvr_buffer.pread!=dmxdev->dvr_buffer.pwrite)
			return (POLLIN | POLLRDNORM | POLLPRI);
		
		poll_wait(file, &dmxdev->dvr_buffer.queue, wait);
		
		if (dmxdev->dvr_buffer.error)
			return (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

		if (dmxdev->dvr_buffer.pread!=dmxdev->dvr_buffer.pwrite)
			return (POLLIN | POLLRDNORM | POLLPRI);
		
		return 0;
	} else 
	        return (POLLOUT | POLLWRNORM | POLLPRI);
}

static struct file_operations dvb_dvr_fops = {
	.owner		= THIS_MODULE,
	.read		= dvb_dvr_read,
	.write		= dvb_dvr_write,
	.ioctl		= dvb_dvr_ioctl,
	.open		= dvb_dvr_open,
	.release	= dvb_dvr_release,
	.poll		=dvb_dvr_poll,
};

static struct dvb_device dvbdev_dvr = {
	.priv		= 0,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_dvr_fops
};

int 
dvb_dmxdev_init(dmxdev_t *dmxdev, struct dvb_adapter *dvb_adapter)
{
        int i;

	if (dmxdev->demux->open(dmxdev->demux)<0)
		return -EUSERS;
	
	dmxdev->filter=vmalloc(dmxdev->filternum*sizeof(dmxdev_filter_t));
	if (!dmxdev->filter)
	        return -ENOMEM;

	dmxdev->dvr=vmalloc(dmxdev->filternum*sizeof(dmxdev_dvr_t));
	if (!dmxdev->dvr) {
		vfree(dmxdev->filter);
		dmxdev->filter=0;
	        return -ENOMEM;
	}
        sema_init(&dmxdev->mutex, 1);
	spin_lock_init(&dmxdev->lock);
	for (i=0; i<dmxdev->filternum; i++) {
                dmxdev->filter[i].dev=dmxdev;
                dmxdev->filter[i].buffer.data=0;
	        dvb_dmxdev_filter_state_set(&dmxdev->filter[i], DMXDEV_STATE_FREE);
                dmxdev->dvr[i].dev=dmxdev;
                dmxdev->dvr[i].buffer.data=0;
	        dvb_dmxdev_filter_state_set(&dmxdev->filter[i], DMXDEV_STATE_FREE);
	        dvb_dmxdev_dvr_state_set(&dmxdev->dvr[i], DMXDEV_STATE_FREE);
	}
	dvb_register_device(dvb_adapter, &dmxdev->dvbdev, &dvbdev_demux, dmxdev, DVB_DEVICE_DEMUX);
	dvb_register_device(dvb_adapter, &dmxdev->dvr_dvbdev, &dvbdev_dvr, dmxdev, DVB_DEVICE_DVR);

	dvb_dmxdev_buffer_init(&dmxdev->dvr_buffer);
	MOD_INC_USE_COUNT;
	return 0;
}

void 
dvb_dmxdev_release(dmxdev_t *dmxdev)
{
	dvb_unregister_device(dmxdev->dvbdev);
	dvb_unregister_device(dmxdev->dvr_dvbdev);
	if (dmxdev->filter) {
	        vfree(dmxdev->filter);
		dmxdev->filter=0;
	}
	if (dmxdev->dvr) {
	        vfree(dmxdev->dvr);
		dmxdev->dvr=0;
	}
        dmxdev->demux->close(dmxdev->demux);
	MOD_DEC_USE_COUNT;
}



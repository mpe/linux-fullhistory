/*****************************************************************************/

/*
 *      modem.c  --  Linux soundcard HF FSK driver,
 *                   Modem code.
 *
 *      Copyright (C) 1997  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *        Swiss Federal Institute of Technology (ETH), Electronics Lab
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */

/*****************************************************************************/
      

#include <linux/wait.h>
#include <linux/malloc.h>
#include <linux/hfmodem.h>

/* --------------------------------------------------------------------- */

/*
 * currently this module is supposed to support both module styles, i.e.
 * the old one present up to about 2.1.9, and the new one functioning
 * starting with 2.1.21. The reason is I have a kit allowing to compile
 * this module also under 2.0.x which was requested by several people.
 * This will go in 2.2
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE >= 0x20100
#include <asm/uaccess.h>
#else
#include <asm/segment.h>
#include <linux/mm.h>

#undef put_user
#undef get_user

#define put_user(x,ptr) ({ __put_user((unsigned long)(x),(ptr),sizeof(*(ptr))); 0; })
#define get_user(x,ptr) ({ x = ((__typeof__(*(ptr)))__get_user((ptr),sizeof(*(ptr)))); 0; })

extern inline int copy_from_user(void *to, const void *from, unsigned long n)
{
        int i = verify_area(VERIFY_READ, from, n);
        if (i)
                return i;
        memcpy_fromfs(to, from, n);
        return 0;
}

extern inline int copy_to_user(void *to, const void *from, unsigned long n)
{
        int i = verify_area(VERIFY_WRITE, to, n);
        if (i)
                return i;
        memcpy_tofs(to, from, n);
        return 0;
}
#endif

#if LINUX_VERSION_CODE >= 0x20123
#include <linux/init.h>
#else
#define __init
#define __initdata
#define __initfunc(x) x
#endif

/* --------------------------------------------------------------------- */

struct hfmodem_correlator_cache hfmodem_correlator_cache[HFMODEM_CORRELATOR_CACHE];

/* --------------------------------------------------------------------- */

#include "tables.h"

#define M_PI        3.14159265358979323846      /* pi */

/* --------------------------------------------------------------------- */

extern __inline__ int isimplecos(unsigned int arg)
{
	return isintab[((arg+0x4000) >> (16-SINTABBITS)) & (SINTABSIZE-1)];
}

extern __inline__ int isimplesin(unsigned int arg)
{
	return isintab[(arg >> (16-SINTABBITS)) & (SINTABSIZE-1)];
}

/* --------------------------------------------------------------------- */

extern __inline__ int itblcos(unsigned int arg)
{
	unsigned int x;
	int dx;
	int s, c;

	x = (arg + (0x8000 >> SINTABBITS)) & (0xffffu & (0xffffu << (16-SINTABBITS)));
	dx = arg - x;
	x >>= (16-SINTABBITS);
	c = isintab[x+(0x4000 >> (16-SINTABBITS))];
	s = isintab[x];
	return c - ((s * dx * (int)(M_PI*64.0)) >> 21);
}

/* --------------------------------------------------------------------- */

extern __inline__ void itblcossin(unsigned int arg, int *cos, int *sin)
{
	unsigned int x;
	int dx;
	int s, c;

	x = (arg + (0x8000 >> SINTABBITS)) & (0xffffu & (0xffffu << (16-SINTABBITS)));
	dx = arg - x;
	x >>= (16-SINTABBITS);
	c = isintab[x+(0x4000 >> (16-SINTABBITS))];
	s = isintab[x];
	*cos = c - ((s * dx * (int)(M_PI*64.0)) >> 21);
	*sin = s + ((c * dx * (int)(M_PI*64.0)) >> 21);
}

/* --------------------------------------------------------------------- */

static unsigned short random_seed;

extern __inline__ unsigned short random_num(void)
{
        random_seed = 28629 * random_seed + 157;
        return random_seed;
}

/* --------------------------------------------------------------------- */
/*
 * correlator cache routines
 */

extern __inline__ void cc_lock(unsigned int u)
{
	if (u >= HFMODEM_CORRELATOR_CACHE)
		return;
	hfmodem_correlator_cache[u].refcnt++;
}

extern __inline__ void cc_unlock(unsigned int u)
{
	if (u >= HFMODEM_CORRELATOR_CACHE)
		return;
	if ((--hfmodem_correlator_cache[u].refcnt) <= 0) {
		unsigned int i;

		for (i = 0; i < HFMODEM_CORRELATOR_CACHE; i++) 
			if (hfmodem_correlator_cache[i].lru < 32767)
				hfmodem_correlator_cache[i].lru++;
		hfmodem_correlator_cache[u].lru = 0;
		hfmodem_correlator_cache[u].refcnt = 0;
	}
}


/* --------------------------------------------------------------------- */

extern __inline__ unsigned int cc_lookup(unsigned short phinc0, unsigned short phinc1)
{
	unsigned int j;

	/* find correlator cache entry */
	for (j = 0; j < HFMODEM_CORRELATOR_CACHE; j++) 
		if (hfmodem_correlator_cache[j].phase_incs[0] == phinc0 &&
		    hfmodem_correlator_cache[j].phase_incs[1] == phinc1)
			return j;
	return ~0;
}

/* --------------------------------------------------------------------- */

extern __inline__ unsigned int cc_replace(void)
{
	unsigned int j, k = HFMODEM_CORRELATOR_CACHE;
	int l = -1;

	for (j = 0; j < HFMODEM_CORRELATOR_CACHE; j++)
		if (hfmodem_correlator_cache[j].refcnt <= 0 && hfmodem_correlator_cache[j].lru > l) {
			k = j;
			l = hfmodem_correlator_cache[j].lru;
		}
	if (k < HFMODEM_CORRELATOR_CACHE)
		return k;
	printk(KERN_ERR "%s: modem: out of filter coefficient cache entries\n", hfmodem_drvname);
	return random_num() % HFMODEM_CORRELATOR_CACHE;
}

/* --------------------------------------------------------------------- */

#define SH1  8     /* min. ceil(log2(L1CORR_LEN)) - (31-(2*15-1)) */
#define SH2  (3*15-2*SH1)

#ifdef __i386__

extern __inline__ int icorr(int n, const int *coeff, const short *inp)
{
	int ret, rethi, tmp1 = 0, tmp2 = 0;

	__asm__("\n1:\n\t"
		"movswl (%0),%%eax\n\t"
		"imull (%1)\n\t"
		"subl $2,%0\n\t"
		"addl $4,%1\n\t"
		"addl %%eax,%3\n\t"
		"adcl %%edx,%4\n\t"
		"decl %2\n\t"
		"jne 1b\n\t"
		: "=&S" (inp), "=&D" (coeff), "=&c" (n), "=m" (tmp1), "=m" (tmp2)
		: "0" (inp), "1" (coeff), "2" (n)
		: "ax", "dx");
	__asm__("shrdl %2,%1,%0\n\t"
		"# sarl %2,%1\n\t"
		: "=&r" (ret), "=&r" (rethi)
		: "i" (SH1), "0" (tmp1), "1" (tmp2));


	return ret;
}

#else /* __i386__ */

extern __inline__ int icorr(int n, const int *coeff, const short *inp)
{
	long long sum = 0;
	int i;

	for (i = n; i > 0; i--, coeff++, inp--)
		sum += (*coeff) * (*inp);
	sum >>= SH1;
	return sum;
}

#endif /* __i386__ */

/* --------------------------------------------------------------------- */

extern __inline__ long long isqr(int x) __attribute__ ((const));

extern __inline__ long long isqr(int x)
{
	return ((long long)x) * ((long long)x);
}

/* --------------------------------------------------------------------- */

extern __inline__ hfmodem_soft_t do_filter(struct hfmodem_l1_rxslot *slot, short *s)
{
	unsigned int cc = slot->corr_cache;
	long long ll;

	if (cc >= HFMODEM_CORRELATOR_CACHE) {
		printk(KERN_ERR "do_filter: correlator cache index overrange\n");
		return 0;
	}
	ll = isqr(icorr(slot->corrlen, hfmodem_correlator_cache[cc].correlator[1][0], s)) +
		isqr(icorr(slot->corrlen, hfmodem_correlator_cache[cc].correlator[1][1], s)) -
		isqr(icorr(slot->corrlen, hfmodem_correlator_cache[cc].correlator[0][0], s)) -
		isqr(icorr(slot->corrlen, hfmodem_correlator_cache[cc].correlator[0][1], s));
	ll >>= SH2;
       	return (ll * slot->scale) >> 23;
}

/* --------------------------------------------------------------------- */

static void cc_prepare(struct hfmodem_l1_rxslot *slot, unsigned short phinc0, unsigned short phinc1)
{
	unsigned int j, k, l, ph, phinc;

	slot->scale = (1<<23) / (slot->corrlen*slot->corrlen);

	j = cc_lookup(phinc0, phinc1);
	if (j >= HFMODEM_CORRELATOR_CACHE) {
		j = cc_replace();
		/* calculate the correlator values */
		printk(KERN_DEBUG "%s: corr cache calc: %u  phases: 0x%04x 0x%04x\n", 
		       hfmodem_drvname, j, phinc0, phinc1);
		hfmodem_correlator_cache[j].phase_incs[0] = phinc0;
		hfmodem_correlator_cache[j].phase_incs[1] = phinc1;
		for (k = 0; k < 2; k++) {
			phinc = hfmodem_correlator_cache[j].phase_incs[k];
			for (ph = l = 0; l < HFMODEM_MAXCORRLEN; l++, ph = (ph + phinc) & 0xffff)
				itblcossin(ph, &hfmodem_correlator_cache[j].correlator[k][0][l],
					   &hfmodem_correlator_cache[j].correlator[k][1][l]);
		}
		hfmodem_correlator_cache[j].refcnt = 0;

#if 0
		printk(KERN_DEBUG "%s: corr: %u ph: 0x%04x 0x%04x\n", hfmodem_drvname, j, 
		       hfmodem_correlator_cache[j].phase_incs[0],
		       hfmodem_correlator_cache[j].phase_incs[1]);
		for (l = 0; l < HFMODEM_MAXCORRLEN; l++)
			printk(KERN_DEBUG "%s: corr: %6d %6d %6d %6d\n", hfmodem_drvname, 
			       hfmodem_correlator_cache[j].correlator[0][0][l],
			       hfmodem_correlator_cache[j].correlator[0][1][l],
			       hfmodem_correlator_cache[j].correlator[1][0][l],
			       hfmodem_correlator_cache[j].correlator[1][1][l]);
#endif
	}
	slot->corr_cache = j;
	cc_lock(j);
}

/* --------------------------------------------------------------------- */

void hfmodem_clear_rq(struct hfmodem_state *dev)
{
	unsigned long flags;
	unsigned int i;

	save_flags(flags);
	cli();
	for (i = 0; i < HFMODEM_NUMRXSLOTS; i++) {
		if (dev->l1.rxslots[i].state == ss_unused)
			continue;
		dev->l1.rxslots[i].state = ss_unused;
		kfree_s(dev->l1.rxslots[i].data, dev->l1.rxslots[i].nbits * sizeof(hfmodem_soft_t));
	}
	for (i = 0; i < HFMODEM_NUMTXSLOTS; i++) {
		if (dev->l1.txslots[i].state == ss_unused)
			continue;
		dev->l1.txslots[i].state = ss_unused;
		kfree_s(dev->l1.txslots[i].data, (dev->l1.txslots[i].nbits + 7) >> 3);
	}
	for (i = 0; i < HFMODEM_CORRELATOR_CACHE; i++)
		hfmodem_correlator_cache[i].refcnt = 0;
	restore_flags(flags);
}

/* --------------------------------------------------------------------- */

int hfmodem_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct hfmodem_state *dev = &hfmodem_state[0];
	struct hfmodem_ioctl_fsk_tx_request txrq;
	struct hfmodem_ioctl_fsk_rx_request rxrq;
	struct hfmodem_ioctl_mixer_params mix;
	struct hfmodem_ioctl_sample_params spar;
	unsigned long flags;
	unsigned int len;
	int ret, i, idx;
	void *data, *userdata;
	hfmodem_id_t id;
	hfmodem_time_t tm = 0;
	
	if (!dev->active)
		return -EBUSY;
	switch(cmd) {
	default:
		return -EINVAL;
		
	case HFMODEM_IOCTL_FSKTXREQUEST:
		if ((ret = copy_from_user(&txrq, (void *)arg, sizeof(txrq))))
			return ret;
		if (txrq.nbits > HFMODEM_MAXBITS)
			return -EINVAL;
		len = (txrq.nbits + 7) >> 3;
		if (!(data = kmalloc(len, GFP_KERNEL)))
			return -ENOMEM;
		if (copy_from_user(data, txrq.data, len)) {
			kfree_s(data, len);
			return -EFAULT;
		}
		save_flags(flags);
		cli();
		for (i = 0; i < HFMODEM_NUMTXSLOTS && dev->l1.txslots[i].state != ss_unused; i++);
		if (i >= HFMODEM_NUMTXSLOTS) {
			restore_flags(flags);
			kfree_s(data, len);
			return -EBUSY;
		}
		dev->l1.txslots[i].state = ss_ready;
		dev->l1.txslots[i].tstart = txrq.tstart;
		dev->l1.txslots[i].tinc = txrq.tinc;
		dev->l1.txslots[i].data = data;
		dev->l1.txslots[i].nbits = txrq.nbits;
		dev->l1.txslots[i].cntbits = 0;
		dev->l1.txslots[i].inv = txrq.inv ? 0xff : 0;
		dev->l1.txslots[i].id = txrq.id;
		dev->l1.txslots[i].phase_incs[0] = ((txrq.freq[0]*0x10000+(HFMODEM_SRATE/2))/HFMODEM_SRATE)
			& 0xffff;
		dev->l1.txslots[i].phase_incs[1] = ((txrq.freq[1]*0x10000+(HFMODEM_SRATE/2))/HFMODEM_SRATE)
			& 0xffff;
		restore_flags(flags);
		return 0;
		
	case HFMODEM_IOCTL_FSKRXREQUEST:
		if ((ret = copy_from_user(&rxrq, (void *)arg, sizeof(rxrq))))
			return ret;
		if (rxrq.nbits > HFMODEM_MAXBITS)
			return -EINVAL;
		if (rxrq.baud < HFMODEM_MINBAUD || rxrq.baud > HFMODEM_MAXBAUD)
			return -EINVAL;
		len = rxrq.nbits * sizeof(hfmodem_soft_t);
		if (verify_area(VERIFY_WRITE, rxrq.data, len))
			return -EFAULT;
		if (!(data = kmalloc(len, GFP_KERNEL)))
			return -ENOMEM;
		save_flags(flags);
		cli();
		for (i = 0; i < HFMODEM_NUMRXSLOTS && dev->l1.rxslots[i].state != ss_unused; i++);
		if (i >= HFMODEM_NUMRXSLOTS) {
			restore_flags(flags);
			kfree_s(data, len);
			return -EBUSY;
		}
		dev->l1.rxslots[i].state = ss_ready;
		dev->l1.rxslots[i].tstart = rxrq.tstart;
		dev->l1.rxslots[i].tinc = rxrq.tinc;
		dev->l1.rxslots[i].data = data;
		dev->l1.rxslots[i].userdata = rxrq.data;
		dev->l1.rxslots[i].nbits = rxrq.nbits;
		dev->l1.rxslots[i].cntbits = 0;
		dev->l1.rxslots[i].id = rxrq.id;
		dev->l1.rxslots[i].corrlen = HFMODEM_SRATE/rxrq.baud;
		cc_prepare(dev->l1.rxslots+i, 
			   ((rxrq.freq[0]*0x10000+(HFMODEM_SRATE/2))/HFMODEM_SRATE) & 0xffff,
			   ((rxrq.freq[1]*0x10000+(HFMODEM_SRATE/2))/HFMODEM_SRATE) & 0xffff);
		restore_flags(flags);
		return 0;
		
	case HFMODEM_IOCTL_CLEARRQ:
		hfmodem_clear_rq(dev);
		return 0;
		
	case HFMODEM_IOCTL_GETCURTIME:
		return put_user(dev->l1.last_time + 20000L, (hfmodem_time_t *)arg); /* heuristic */
	
	case HFMODEM_IOCTL_WAITRQ:
		save_flags(flags);
		cli();
		ret = 0;
		for (idx = -1, i = 0; i < HFMODEM_NUMRXSLOTS; i++) {
			if (dev->l1.rxslots[i].state == ss_unused)
				continue;
			if (dev->l1.rxslots[i].state != ss_retired) {
				ret++;
				continue;
			}
			if (idx < 0 || (signed)(tm - dev->l1.rxslots[i].tstart) > 0) {
				idx = i;
				tm = dev->l1.rxslots[i].tstart;
			}
		}
		if (idx >= 0) {
			cc_unlock(dev->l1.rxslots[idx].corr_cache);
			id = dev->l1.rxslots[idx].id;
			data = dev->l1.rxslots[idx].data;
			userdata = dev->l1.rxslots[idx].userdata;
			len = dev->l1.rxslots[idx].nbits * sizeof(hfmodem_soft_t);
			dev->l1.rxslots[idx].state = ss_unused;
			restore_flags(flags);
			ret = copy_to_user(userdata, data, len);
			kfree_s(data, len);
			return (put_user(id, (hfmodem_id_t *)arg)) ? -EFAULT : ret;
		}
		for (idx = -1, i = 0; i < HFMODEM_NUMTXSLOTS; i++) {
			if (dev->l1.txslots[i].state == ss_unused)
				continue;
			if (dev->l1.txslots[i].state != ss_retired) {
				ret++;
				continue;
			}
			if (idx < 0 || (signed)(tm - dev->l1.txslots[i].tstart) > 0) {
				idx = i;
				tm = dev->l1.txslots[i].tstart;
			}
		}
		if (idx >= 0) {
			id = dev->l1.txslots[idx].id;
			data = dev->l1.txslots[idx].data;
			len = (dev->l1.txslots[idx].nbits + 7) >> 3;
			dev->l1.txslots[idx].state = ss_unused;
			restore_flags(flags);
			kfree_s(data, len);
			return put_user(id, (hfmodem_id_t *)arg);
		}
		restore_flags(flags);
		return ret ? -EAGAIN : -EPIPE;

	case HFMODEM_IOCTL_MIXERPARAMS:
		if ((ret = copy_from_user(&mix, (void *)arg, sizeof(mix))))
			return ret;
		dev->scops->mixer(dev, mix.src, mix.igain, mix.ogain);
		return 0;
		
	case HFMODEM_IOCTL_SAMPLESTART:
		save_flags(flags);
		cli();
		if (dev->sbuf.kbuf) 
			kfree_s(dev->sbuf.kbuf, dev->sbuf.size);
		dev->sbuf.kbuf = dev->sbuf.kptr = NULL;
		dev->sbuf.size = dev->sbuf.rem = 0;
		restore_flags(flags);
		if ((ret = copy_from_user(&spar, (void *)arg, sizeof(spar))))
			return ret;
		if (spar.len == 0)
			return 0;
		if (spar.len < 2 || spar.len > 8192)
			return -EINVAL;
		if (verify_area(VERIFY_WRITE, spar.data, spar.len * sizeof(__s16)))
			return -EFAULT;
		if (!(dev->sbuf.kbuf = kmalloc(spar.len * sizeof(__s16), GFP_KERNEL)))
			return -ENOMEM;
		save_flags(flags);
		cli();
		dev->sbuf.kptr = dev->sbuf.kbuf;
		dev->sbuf.size = spar.len * sizeof(__s16);
		dev->sbuf.rem = spar.len;
		dev->sbuf.ubuf = spar.data;
		restore_flags(flags);
		return 0;
		
	case HFMODEM_IOCTL_SAMPLEFINISHED:
		save_flags(flags);
		cli();
		if (dev->sbuf.rem > 0) {
			restore_flags(flags);
			return -EAGAIN;
		}
		if (!dev->sbuf.kbuf || !dev->sbuf.size) {
			restore_flags(flags);
			return -EPIPE;
		}
		restore_flags(flags);
		ret = copy_to_user(dev->sbuf.ubuf, dev->sbuf.kbuf, dev->sbuf.size);
		kfree_s(dev->sbuf.kbuf, dev->sbuf.size);
		dev->sbuf.kbuf = dev->sbuf.kptr = NULL;
		dev->sbuf.size = dev->sbuf.rem = 0;
		return ret;
	}
}

/* --------------------------------------------------------------------- */

#if LINUX_VERSION_CODE >= 0x20100

unsigned int hfmodem_poll(struct file *file, poll_table *wait)
{
	struct hfmodem_state *dev = &hfmodem_state[0];
	unsigned long flags;
	int i, cnt1, cnt2;
	
	poll_wait(file, &dev->wait, wait);
	save_flags(flags);
	cli();
	for (i = cnt1 = cnt2 = 0; i < HFMODEM_NUMTXSLOTS; i++) {
		if (dev->l1.txslots[i].state == ss_retired)
			cnt1++;
		if (dev->l1.txslots[i].state != ss_unused)
			cnt2++;
	}
	for (i = 0; i < HFMODEM_NUMRXSLOTS; i++) {
		if (dev->l1.rxslots[i].state == ss_retired)
			cnt1++;
		if (dev->l1.rxslots[i].state != ss_unused)
			cnt2++;
	}
	restore_flags(flags);
        if (cnt1 || !cnt2)
                return POLLIN | POLLRDNORM;
        return 0;
}

#else 

int hfmodem_select(struct inode *inode, struct file *file, int sel_type, select_table *wait)
{
	struct hfmodem_state *dev = &hfmodem_state[0];
	unsigned long flags;
	int i, cnt1, cnt2;
	
	if (sel_type == SEL_IN) {
		save_flags(flags);
		cli();
		for (i = cnt1 = cnt2 = 0; i < HFMODEM_NUMTXSLOTS; i++) {
			if (dev->l1.txslots[i].state == ss_retired)
				cnt1++;
			if (dev->l1.txslots[i].state != ss_unused)
				cnt2++;
		}
		for (i = 0; i < HFMODEM_NUMRXSLOTS; i++) {
			if (dev->l1.rxslots[i].state == ss_retired)
				cnt1++;
			if (dev->l1.rxslots[i].state != ss_unused)
				cnt2++;
		}
		restore_flags(flags);
		if (cnt1 || !cnt2)
			return 1;
		select_wait(&dev->wait, wait);
    	}
	return 0;
}

#endif

/* --------------------------------------------------------------------- */

extern __inline__ unsigned int l1fsk_phinc(struct hfmodem_l1_txslot *txs, unsigned int nbit)
{
	return txs->phase_incs[!!((txs->data[nbit >> 3] ^ txs->inv) & (1 << (nbit & 7)))];
}

/* --------------------------------------------------------------------- */

void hfmodem_input_samples(struct hfmodem_state *dev, hfmodem_time_t tstart, 
			 hfmodem_time_t tinc, __s16 *samples)
{
	hfmodem_time_t tst, tend;
	__s16 *s;
	int i, j;
	hfmodem_soft_t sample;

	dev->l1.last_time = tstart + (HFMODEM_FRAGSAMPLES-1) * tinc;
	for (i = 0; i < HFMODEM_NUMRXSLOTS; i++) {
		struct hfmodem_l1_rxslot *rxs = dev->l1.rxslots + i;

		if (rxs->state == ss_unused || rxs->state == ss_retired)
			continue;
		tst = tstart - (rxs->corrlen-1) * tinc;
		tend = tst + (HFMODEM_FRAGSAMPLES-1) * tinc;
		if (rxs->state == ss_ready) {
			if ((signed)(rxs->tstart - tend) > 0) 
				continue;
			rxs->state = ss_oper;
		}
		for (s = samples, j = 0; j < HFMODEM_FRAGSAMPLES; j++, s++, tst += tinc)
			if ((signed)(rxs->tstart - tst) <= 0) {
				sample = do_filter(rxs, s);
				while ((signed)(rxs->tstart - tst) <= 0 && 
				       rxs->cntbits < rxs->nbits) {
					rxs->data[rxs->cntbits] = sample;
					rxs->cntbits++;
					rxs->tstart += rxs->tinc;
				}
				if (rxs->cntbits >= rxs->nbits) {
					rxs->state = ss_retired;
					break;
				}
			}
	}
}

/* --------------------------------------------------------------------- */

extern __inline__ unsigned int output_one_sample(struct hfmodem_state *dev, hfmodem_time_t tm)
{
	int i, j, k;
	struct hfmodem_l1_txslot *txs;
	/*
	 * first activate new output slots
	 */
	for (j = -1, i = 0; i < HFMODEM_NUMTXSLOTS; i++) {
		txs = dev->l1.txslots + i;
		if (txs->state == ss_ready && (signed)(txs->tstart - tm) <= 0) {
			for (k = 0; k < HFMODEM_NUMTXSLOTS; k++) {
				if (dev->l1.txslots[k].state != ss_oper)
					continue;
				dev->l1.txslots[k].state = ss_retired;
			}
			txs->state = ss_oper;
			txs->tstart += txs->tinc;
			txs->phinc = l1fsk_phinc(txs, 0);
			txs->cntbits = 1;
		};
		if (txs->state != ss_oper)
			continue;
		j = i;
	}
	if (j < 0 || j >= HFMODEM_NUMTXSLOTS)
		return 0;
	/*
	 * calculate the current slot
	 */
	txs = dev->l1.txslots + j;
	while ((signed)(txs->tstart - tm) <= 0) {
		if (txs->cntbits >= txs->nbits) {
			txs->state = ss_retired;
			return 0;
		}
		txs->tstart += txs->tinc;
		txs->phinc = l1fsk_phinc(txs, txs->cntbits);
		txs->cntbits++;
	}
	return txs->phinc;
}

/* --------------------------------------------------------------------- */

int hfmodem_output_samples(struct hfmodem_state *dev, hfmodem_time_t tstart, 
			   hfmodem_time_t tinc, __s16 *samples)
{
	int i, j;
	hfmodem_time_t tend = tstart + (HFMODEM_FRAGSAMPLES-1) * tinc;

	for (i = 0; i < HFMODEM_NUMTXSLOTS; i++) {
		if (dev->l1.txslots[i].state == ss_oper)
			break;
		if (dev->l1.txslots[i].state == ss_ready && 
		    (signed)(dev->l1.txslots[i].tstart - tend) <= 0)
			break;
	}
	if (i >= HFMODEM_NUMTXSLOTS)
		return 0;
	for (j = 0; j < HFMODEM_FRAGSAMPLES; j++, tstart += tinc, samples++) {
		*samples = isimplecos(dev->l1.tx_phase);
		dev->l1.tx_phase += output_one_sample(dev, tstart);
	}
	return 1;
}

/* --------------------------------------------------------------------- */

long hfmodem_next_tx_event(struct hfmodem_state *dev, hfmodem_time_t curr)
{
	long diff = LONG_MAX, t;
	int i;

	for (i = 0; i < HFMODEM_NUMTXSLOTS; i++) {
		if (dev->l1.txslots[i].state == ss_oper)
			if (diff > 0)
				diff = 0;
		if (dev->l1.txslots[i].state == ss_ready) {
			t = dev->l1.txslots[i].tstart - curr;
			if (t < diff)
				diff = t;
		}
	}
	return diff;
}

/* --------------------------------------------------------------------- */

void hfmodem_finish_pending_rx_requests(struct hfmodem_state *dev)
{
	int i;

	for (i = 0; i < HFMODEM_NUMRXSLOTS; i++) {
		if (dev->l1.rxslots[i].state != ss_oper)
			continue;
		while (dev->l1.rxslots[i].cntbits < dev->l1.rxslots[i].nbits) {
			dev->l1.rxslots[i].data[dev->l1.rxslots[i].cntbits] = 0;
			dev->l1.rxslots[i].cntbits++;
		}
		dev->l1.rxslots[i].state = ss_retired;
	}
}

/* --------------------------------------------------------------------- */

void hfmodem_wakeup(struct hfmodem_state *dev)
{
	int i, cnt1, cnt2;
	
	for (i = cnt1 = cnt2 = 0; i < HFMODEM_NUMTXSLOTS; i++) {
		if (dev->l1.txslots[i].state == ss_retired)
			cnt1++;
		if (dev->l1.txslots[i].state != ss_unused)
			cnt2++;
	}
	for (i = 0; i < HFMODEM_NUMRXSLOTS; i++) {
		if (dev->l1.rxslots[i].state == ss_retired)
			cnt1++;
		if (dev->l1.rxslots[i].state != ss_unused)
			cnt2++;
	}
	if (cnt1 || !cnt2)
		wake_up_interruptible(&dev->wait);
}

/* --------------------------------------------------------------------- */

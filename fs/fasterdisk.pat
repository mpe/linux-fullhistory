From kwhite@csi.uottawa.ca Sun Mar 15 20:01:06 1992
Received: from klaava.Helsinki.FI by kruuna.helsinki.fi with SMTP id AA04345
  (5.65c/IDA-1.4.4 for <torvalds@klaava.Helsinki.FI>); Sun, 15 Mar 1992 20:00:45 +0200
Received: from csi.UOttawa.CA (csi0.csi.uottawa.ca) by klaava.Helsinki.FI (4.1/SMI-4.1)
	id AA21712; Sun, 15 Mar 92 20:00:40 +0200
Received: by csi.UOttawa.CA id AA00608
  (5.65+/IDA-1.3.5 for torvalds@klaava.helsinki.fi); Sun, 15 Mar 92 12:57:47 -0500
From: Keith White <kwhite@csi.uottawa.ca>
Message-Id: <9203151757.AA00608@csi.UOttawa.CA>
Subject: reset-floppy fixed, double your disk speed (linux 0.95)
To: torvalds@cc.helsinki.fi (Linus Torvalds)
Date: Sun, 15 Mar 92 12:57:46 EST
X-Mailer: ELM [version 2.3 PL11]
Status: OR

A couple of things.

1) I must be one of those lucky few who's been able to use linux0.95.
However I was surprised to see that hard disk speed was slower.  This
was until I remembered that I had patched fs/block_dev.c and
include/asm/segment.h to support block moves to and from the fs
segment.  These patches (very minimal it must be admitted) give a
reasonable increase in speed for 'fsck':
	original:	42.62 elapsed
	patched:	22.06 elapsed
These patches have no effect on sequential disk access (dd if=/dev/hda
...)  since most time is spent waiting for those small 2 sector
transfers anyway.  The patches are included below.

2) I don't run DOS so a floppy disk format program is sort of
essential.  Lawrence Foard's (sp?) formatting routines have a few
problems (nits).
	a) The inter-sector gap should be larger for a format than it
is for a read or write.
	b) An interleave is not required (at least on my machine, a
bottom of the line 386SX-16).

3) I seem to have fixed the dreaded "reset-floppy called" problem --
at least it works for me.  The posted fix does not work in my case.
I'd send you these patches as well, but I only have diffs that include
all the mods I did for the floppy format stuff.  The key point (I
think) was to only reset twice during an error loop.  If a track was
bad, the recalibrate would fail and call reset which would call
recalibrate ...  If you're interested, I could try to separate the
formatting stuff and all my debugging "printks" from the reset fix
stuff and send the diffs.

...keith (kwhite@csi.uottawa.ca)

---cut here---
*** 1.1	1992/03/14 16:33:21
--- include/asm/segment.h	1992/03/15 17:10:14
***************
*** 63,65 ****
--- 63,109 ----
  	__asm__("mov %0,%%fs"::"r" ((unsigned short) val));
  }
  
+ /*
+  * these routines are added to use the movs instruction with data
+  * in the fs segment.  No optimizations are done with regards to using
+  * movsw or movsd 
+  *
+  * kwhite@csi.uottawa.ca
+  */
+ 
+ #define memcpy_fromfs(dest,src,n) __asm__ (\
+ 	"mov %%fs,%%ax; movl %%ax,%%ds\n\
+ 	cld; rep; movsb\n\
+ 	mov %%es,%%ax; mov %%ax,%%ds" \
+ 	::"D" ((long)(dest)), "S" ((long)(src)), "c" ((long) (n)) \
+ 	:"di", "si", "cx", "ax");
+ #define memcpy_fromfs_w(dest,src,n) __asm__ (\
+ 	"mov %%fs,%%ax; movl %%ax,%%ds\n\
+ 	cld; rep; movsw\n\
+ 	mov %%es,%%ax; mov %%ax,%%ds" \
+ 	::"D" ((long)(dest)), "S" ((long)(src)), "c" ((long) (n)) \
+ 	:"di", "si", "cx", "ax");
+ #define memcpy_fromfs_l(dest,src,n) __asm__ (\
+ 	"mov %%fs,%%ax; movl %%ax,%%ds\n\
+ 	cld; rep; movsl\n\
+ 	mov %%es,%%ax; mov %%ax,%%ds" \
+ 	::"D" ((long)(dest)), "S" ((long)(src)), "c" ((long) (n)) \
+ 	:"di", "si", "cx", "ax");
+ #define memcpy_tofs(dest,src,n) __asm__ (\
+ 	"mov %%fs,%%ax; mov %%ax,%%es\n\
+ 	cld; rep; movsb\n\
+ 	mov %%ds,%%ax; mov %%ax,%%es" \
+ 	::"D" ((long)(dest)), "S" ((long)(src)), "c" ((long) (n)) \
+ 	:"di", "si", "cx", "ax");
+ #define memcpy_tofs_w(dest,src,n) __asm__ (\
+ 	"mov %%fs,%%ax; mov %%ax,%%es\n\
+ 	cld; rep; movsw\n\
+ 	mov %%ds,%%ax; mov %%ax,%%es" \
+ 	::"D" ((long)(dest)), "S" ((long)(src)), "c" ((long) (n)) \
+ 	:"di", "si", "cx", "ax");
+ #define memcpy_tofs_l(dest,src,n) __asm__ (\
+ 	"mov %%fs,%%ax; mov %%ax,%%es\n\
+ 	cld; rep; movsl\n\
+ 	mov %%ds,%%ax; mov %%ax,%%es"\
+ 	::"D" ((long)(dest)), "S" ((long)(src)), "c" ((long) (n)) \
+ 	:"di", "si", "cx", "ax");
*** 1.1	1992/03/14 16:37:10
--- fs/block_dev.c	1992/03/14 16:38:26
***************
*** 47,54 ****
--- 47,64 ----
  		filp->f_pos += chars;
  		written += chars;
  		count -= chars;
+ #ifdef notdef
  		while (chars-->0)
  			*(p++) = get_fs_byte(buf++);
+ #else
+ 		if ((chars&1) || ((long)p&1) || ((long)buf&1)) {
+ 			memcpy_fromfs(p, buf, chars);
+ 		}
+ 		else {
+ 			memcpy_fromfs_w(p, buf, chars>>1);
+ 		}
+ 		p += chars; buf += chars; chars = 0;
+ #endif
  		bh->b_dirt = 1;
  		brelse(bh);
  	}
***************
*** 85,92 ****
--- 95,112 ----
  		filp->f_pos += chars;
  		read += chars;
  		count -= chars;
+ #ifdef notdef
  		while (chars-->0)
  			put_fs_byte(*(p++),buf++);
+ #else
+ 		if ((chars&1) || ((long)buf&1) || ((long)p&1)) {
+ 			memcpy_tofs(buf, p, chars);
+ 		}
+ 		else {
+ 			memcpy_tofs_w(buf, p, chars>>1);
+ 		}
+ 		p += chars; buf += chars; chars = 0;
+ #endif
  		brelse(bh);
  	}
  	return read;
---cut here---

-- 
BITNET:		kwhite@uotcsi2.bitnet (being phased out)
UUCP:		{...,nrcaer,cunews}!uotcsi2!kwhite
INTERNET:	kwhite@csi.uottawa.ca


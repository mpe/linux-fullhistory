Changes by Matija Nalis (mnalis@jagor.srce.hr) on umsdos dentry fixing
(started by Peter T. Waltenberg <peterw@karaka.chch.cri.nz>)

---------- WARNING --------- WARNING --------- WARNING -----------
THIS IS TRULY EXPERIMENTAL.  IT IS NOT BETA YET.  PLEASE EXCUSE MY
YELLING, BUT ANY USE OF THIS MODULE MAY VERY WELL DESTROY YOUR
UMSDOS FILESYSTEM, AND MAYBE EVEN OTHER FILESYSTEMS IN USE.
YOU'VE BEEN WARNED.
---------- WARNING --------- WARNING --------- WARNING -----------

Current status (980901) - UMSDOS dentry-WIP-Beta 0.82-7:

(1) pure MSDOS (no --linux-.--- EMD file):

+ readdir			- works
+ lookup			- works
+ read file			- works

+ creat file			- works
+ delete file			- works
+ write file			- works
+ rename file (same dir)	- works
+ rename file (dif. dir)	- works
+ rename dir (same dir)		- works
+ rename dir (dif. dir)		- works
+ mkdir				- works
- rmdir 			- QUESTIONABLE. probable problem on non-empty dirs.

Notes: possible very minor problems with dentry/inode/... kernel structures (very rare)


(2) umsdos (with --linux-.--- EMD file):

+ readdir			- works
+ lookup 			- works
+ permissions/owners stuff	- works
+ long file names		- works
+ read file			- works
- switching MSDOS/UMSDOS	- works?
- switching UMSDOS/MSDOS	- works?
- pseudo root things		- COMPLETELY UNTESTED (commented out currently!)
+ resolve symlink		- works
+ dereference symlink		- works
- hard links			- broken again...
+ special files (block/char devices, FIFOs, sockets...)	- seems to work.
- other ioctls			- some UNTESTED
+ dangling symlink		- works

- create symlink		- seems to work both on short & long names now !
- create hardlink		- WARNING: NOT FIXED YET!
- create file			- seems to work both on short & long names now !
- create special file		- seems to work both on short & long names now !
- write to file			- seems to work both on short & long names now !
- rename file (same dir)	- seems to work, but with i_count PROBLEMS
- rename file (dif. dir)	- seems to work, but with i_count PROBLEMS
- rename dir (same dir)		- seems to work, but with i_count PROBLEMS
- rename dir (dif. dir)		- seems to work, but with i_count PROBLEMS
- delete file			- seems to work fully now!
- notify_change (chown,perms)	- seems to work!
- delete hardlink		- WARNING: NOT FIXED YET!
- mkdir				- seems to work both on short & long names now !
- rmdir 			- may work, but readdir blocks linux afterwards. to be FIXED!
- umssyncing			- seems to work, but NEEDS MORE TESTING

+ CVF-FAT stuff (compressed DOS filesystem) - there is some support from Frank
  Gockel <gockel@sent13.uni-duisburg.de> to use it even under umsdosfs, but I
  have no way of testing it -- please let me know if there are problems specific
  to umsdos (for instance, it works under msdosfs, but not under umsdosfs).



Notes:  there is moderate trashing of dentry/inode kernel structures.  Probably
some other kernel structures are compromised.  You should have SysRq support
compiled in, and use Sync/Emergency-remount-RO.  If you don't try mounting
read/write, you should have no big problems. When most things begin to work,
I'll get to finding/fixing those inode/dentry ?_count leakages.

Note 4:  rmdir(2) fails with EBUSY - sdir->i_count > 1 (like 7 ??).  It may be
some error with dir->i_count++, or something related to iput().  See if
number changes if we access the directory in different ways.

Note 5:  there is a problem with unmounting umsdosfs.  It seems to stay
registered or something.  Remounting the same device on any mount point with a
different fstype (such as msdos or vfat) ignores the new fstype and umsdosfs
kicks back in. Should be fixed in 0.82-6 (at least, with nothing in between)!
Much of inode/dentry corruption is fixed in 0.82-7, especially when mounting
read-only.

Note 6:  also we screwed umount(2)-ing the fs at times (EBUSY), by missing
some of those iput/dput's. When most of the rest of the code is fixed, we'll
put them back at the correct places (hopefully). Also much better in 0.82-7.

------------------------------------------------------------------------------

Some general notes:

There is a great amount of kernel log messages.  Use SysRq log-level 5 to turn
most of them off, or 4 to turn all but really fatal ones off.  Probably good
idea to kill klogd/syslogd so it will only go to console. You can also
comment out include/linux/umsdos_fs.h definition of UMS_DEBUG to get rid of
most debugging messages. Please don't turn it off without good reason.

It should work enough to test it, even enough to give you a few chances to
umount/rmmod module, recompile it, and reinsert it again (but probably
screaming about still used inodes on device and stuff).  This is first on
my list of things to get fixed, as it would greatly improve speed of
compile/test/reboot/set_environment/recompile cycle by removing
'reboot/set_environment' component that now occurs every few cycles.

I need some help from someone who knows more than I about the use of dentries
and inodes.  If you can help, please contact me.  I'm mostly worried about
iget/iput and dget/dput, and deallocating temporary dentries we create.
Should we destroy temp dentries? using d_invalidate? using d_drop? just
dput them?

I'm unfortunately somewhat out of time to read linux-kernel, but I do check
for messages having "UMSDOS" in the subject, and read them.  I might miss
some in all that volume, though.  I should reply to any direct e-mail in few
days.  If I don't, probably I never got your message.  You can try
mnalis@voyager.hr; however mnalis@jagor.srce.hr is preferable.


------------------------------------------------------------------------------
some of my notes for myself /mn/:

+ hardlinks/symlinks. test with files in not_the_same_dir
- also test not_the_same_dir for other file operations like rename etc.
- iput: device 00:00 inode 318 still has aliases! problem. Check in iput()
  for device 0,0. Probably null pointer passed around when it shouldn't be ?
- dput/iput problem...
- What about .dotfiles? Are they working? How about multiple dots?
- fix stuff like dir->i_count++ to atomic_inc(&dir->i_count) and similar?

- should check what happen when multiple UMSDOSFS are mounted

- chase down all "FIXME", "DELME", "CNT", check_dentry, check_inode, kill_dentry
  and fix it properly.

- umsdos_create_any - calling msdos_create will create dentry for short name. Hmmmm..?

- what is dir->i_count++ ? locking directory ? should this be lock_parent or
something ?

+ i_binary=2 is for CVF (compressed filesystem).

- SECURITY WARNING: short dentries should be invalidated, or they could be
  accessed instead of proper long names.

- I've put many check_dentry_path(), check_inode() calls to trace down
  problems. those should be removed in final version.

- iput()s with a "FIXME?" comment are uncommented and probably OK. Those with
  "FIXME??" should be tested but probably work. Commented iput()s with
  any "FIXME" comments should probably be uncommented and tested. At some
  places we may need dput() instead of iput(), but that should be checked.

+ as for iput(): (my only pointer so far. anyone else?)

>development I only know about iput. All functions that get an inode as
>argument and don't return it have to call iput on it before exit, i.e. when
>it is no longer needed and the code returns to vfs layer. The rest is quite
>new to me, but it might be similar for dput. Typical side effect of a
>missing iput was a memory runout (but no crash). You also couldn't unmount
>the filesystem later though no process was using it. On the other hand, one
>iput too much lead to serious pointer corruption and crashed the system
>very soon. I used to look at the FAT filesystem and copy those pieces of
>
> Frank

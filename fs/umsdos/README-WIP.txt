Changes by Matija Nalis (mnalis@jagor.srce.hr) on umsdos dentry fixing
(started by Peter T. Waltenberg <peterw@karaka.chch.cri.nz>)

--------- WARNING --------- WARNING --------- WARNING -----------
THIS IS TRULY EXPERIMENTAL. IT IS NOT BETA YET. PLEASE EXCUSE MY
YELLING, BUT ANY USE OF THIS MODULES MAY VERY WELL DESTROY YOUR
UMSDOS FILESYSTEM, AND MAYBE EVEN OTHER FILESYSTEMS IN USE.
YOU'VE BEEN WARNED.
--------- WARNING --------- WARNING --------- WARNING -----------

Current status (980220) - UMSDOS dentry-WIP-Beta 0.82-3:

(1) pure MSDOS (no --linux-.--- EMD file):

- readdir - works
- lookup - works
- read file - works

- creat file - works
- write file - works
- mkdir - works
- rmdir - QUESTIONABLE. probable problem on non-empty dirs.

Notes: possible very minor problems with dentry/inode/... kernel structures (very rare)


(2) umsdos (with --linux-.--- EMD file):

- readdir - works.
- lookup -  works.
- permissions/owners stuff - works
- long file names - works
- read file - works
- switching MSDOS/UMSDOS - works?
- switching UMSDOS/MSDOS - UNTESTED
- pseudo root things - COMMENTED OUT mostly currently. To be fixed when
  dentries stuff is straightened out.
- resolve symlink - seems to work fully now!
- dereference symlink - seems to work fully now!
- hard links - seems to work now
- special files (block/char device, fifos, sockets...) - seems to work ok.
- other ioctls - MOSTLY UNTESTED
- dangling symlink - UNTESTED !

- create symlink		- seems to work both on short & long names now !
- create hardlink		- WARNING: NOT FIXED YET!
- create file			- seems to work both on short & long names now !
- create special file		- seems to work both on short & long names now !
- write to file			- seems to work both on short & long names now !
- rename file (same dir)	- WARNING: NOT FIXED YET!
- rename file (dif. dir)	- WARNING: NOT FIXED YET!
- rename dir (same dir)		- WARNING: NOT FIXED YET!
- rename dir (dif. dir)		- WARNING: NOT FIXED YET!
- delete file			- seems to work fully now!
- notify_change (chown,perms)	- seems to work!
- delete hardlink		- WARNING: NOT FIXED YET!
- mkdir				- seems to work both on short & long names now !
- rmdir 			- WARNING: NOT FIXED YET!
- umssyncing			- seems to work, but NEEDS EXTENSIVE TESTING

- CVF-FAT stuff (compressed DOS filesystem) - there is some support from
  Frank Gockel <gockel@sent13.uni-duisburg.de> to use it even under
  umsdosfs. But I have no way of testing it -- please let me know if there
  are problems that are specific to umsdos (eg. it works under msdosfs, but
  not under umsdosfs)



Notes: moderate dentry/inode kernel structures trashing. Probably some other
kernel structures compromised. Have SysRq support compiled in, and use
Sync/Emergency-remount-RO. And if you don't try mounting read/write -
you should have no big problems...

Notes2: kernel structures trashing seems to be _MUCH_ lower if no
symlinks/hardlink present. (hardlinks seem to be biggest problem)

Notes3: Notes2 is probably somewhat outdated now that hardlink/symlink stuff
is supposed to be fixed enough to work, but I haven't got the time to test
it.

Note5: rmdir(2) fails with EBUSY - sdir->i_count > 1 (like 7 ??). It must be
some error with dir->i_count++, or something related to iput() ? See if
number changes if we access the dir in different ways..

Note6: there is problem with unmounting umsdosfs, it seems to stay
registered or something. Remounting same device on any mount point with
different fstype (like msdos or vfat) ignores fstype and umsdosfs kicks back
in. 

Note7: also we screwed umount(2)-ing the fs at times (EBUSY), by removing
all those iput/dput's. When rest of code is fixed, we'll put them back at
(hopefully) correct places.

------------------------------------------------------------------------------

Some general notes:

There is great amount of kernel log messages. Use SysRq log-level 5 to turn
most of them off, or 4 to turn all but really fatal ones off. Probably good
idea to kill klogd/syslogd so it will only go to console.

It should work enough to test it, even enough to give you few chances to
umount/rmmod module, recompile it, and reinsert it again (but probably
screaming about still used inodes on device and stuff). This is first on
my list of things to get fixed, as it would greatly improve speed of
compile/test/reboot/set_environment/recompile cycle by removing
'reboot/set_environment' component that now occures every few cycles.

But I need some help from someone knowing about dentries/inodes use more
than I. If you can help, please contact me... I'm mostly worried about
iget/iput and dget/dput, and deallocating temporary dentries we create.
should we destroy temp dentries ? using d_invalidate ? using d_drop ? just
dput them ?

I'm unfortunatelly somewhat out of time to read linux-kernel, but I do check
for any messages having UMSDOS in subject, and read them. I might miss it in
all that volume, though. I should reply to any direct Email in few days. If
I don't - probably I never got your message. You can try mnalis@open.hr or
mnalis@voyager.hr; however mnalis@jagor.srce.hr is preferable one.


------------------------------------------------------------------------------
some of my notes for myself /mn/:

+ hardlinks/symlinks. test with files in not_the_same_dir
- also test not_the_same_dir for other file operations like rename etc.
- iput: device 00:00 inode 318 still has aliases! problem. Check in iput()
  for device 0,0. Probably null pointer passed around when it shouldn't be ?
- dput/iput problem...
- what about .dotfiles ? working ? multiple dots ? etc....
- fix stuff like dir->i_count++ to atomic_inc(&dir->i_count) and simular?


- umsdos_create_any - calling msdos_create will create dentry for shor name. Hmmmm..?
- kill_dentry - put it where is needed. Also dput() at needed places.

- when should dput()/iput() be used ?!!

- what is dir->i_count++ ? locking directory ? should this be lock_parent or
something ?

- i_binary=2 is for CVF (compressed filesystem).

- SECURITY WARNING: short dentries should be invalidated, or they could be
  accessed instead of proper long names.

- as for iput() : (my only pointer so far. anyone else ?)

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

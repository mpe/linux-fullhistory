Changes by Matija Nalis (mnalis@jagor.srce.hr) on umsdos dentry fixing
(started by Peter T. Waltenberg <peterw@karaka.chch.cri.nz>)

--------- WARNING --------- WARNING --------- WARNING -----------
THIS IS TRULY EXPERIMENTAL. IT IS NOT BETA YET. PLEASE EXCUSE MY
YELLING, BUT ANY USE OF THIS MODULES MAY VERY WELL DESTROY YOUR
UMSDOS FILESYSTEM, AND MAYBE EVEN OTHER FILESYSTEMS IN USE.
YOU'VE BEEN WARNED.
--------- WARNING --------- WARNING --------- WARNING -----------

Current status (980220) - UMSDOS dentry-WIP-Beta 0.82-1:

(1) pure MSDOS (no --linux-.--- EMD file):

- readdir - works
- lookup - works
- read file - works

- creat file - works
- write file - works
- mkdir - works
- rmdir - questionable. probable problem on non-empty dirs.

Notes: possible very minor problems with dentry/inode/... kernel structures (very rare)


(2) umsdos (with --linux-.--- EMD file):

- readdir - works.
- lookup -  works.
- permissions/owners stuff - works
- long file names - works
- read file - works
- switching MSDOS/UMSDOS - works?
- switching UMSDOS/MSDOS - untested
- pseudo root things - commented out mostly currently. To be fixed when
  dentries stuff is straightened out.
- resolve symlink - seems to work fully now!
- dereference symlink - seems to work fully now!
- hard links - seems to work now
- special files (block/char device, fifos, sockets...) - seems to work ok.
- other ioctls - mostly untested
- dangling symlink - UNTESTED !

- create symlink		- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- create hardlink		- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- create file			- creates, but corrupts. after reboot seem ok ?
- create special file		- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- write to file			- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- rename file (same dir)	- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- rename file (dif. dir)	- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- rename dir (same dir)		- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- rename dir (dif. dir)		- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- delete file			- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- delete hardlink		- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- mkdir				- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- rmdir 			- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- umssyncing			- WARNING: NONE OF WRITE OPERATIONS FIXED YET!

Notes: moderate dentry/inode kernel structures trashing. Probably some other
kernel structures compromised. Have SysRq support compiled in, and use
Sync/Emergency-remount-RO. And don't try mounting read/write yet - and then
you should have no big problems...

Notes2: kernel structures trashing seems to be _MUCH_ lower if no
symlinks/hardlink present. (hardlinks seem to be biggest problem)

Notes3: Notes2 is probably somewhat outdated now that hardlink/symlink stuff
is supposed to be fixed enough to work, but I haven't got the time to test
it.

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
than I. If you can help, please contact me... I'm mostly worries about
iget/iput and dget/dput, and deallocating temporary dentries we create.
should we destroy temp dentries ? using d_invalidate ? using d_drop ? just
dput them ?

I'm unfortunatelly somewhat out of time to read linux-kernel, but I do check
for any messages having UMSDOS in subject, and read them. I should reply to
any direct Email in few days. If I don't - probably I never got your
message. You can try mnalis@open.hr or mnalis@voyager.hr; however
mnalis@jagor.srce.hr is preferable one.

------------------------------------------------------------------------------

some of my notes for myself:

+ hardlinks/symlinks. test with files in not_the_same_dir
- also test not_the_same_dir for other file operations like rename etc.
- iput: device 00:00 inode 318 still has aliases! problem. Check in iput()
  for device 0,0. Probably null pointer passed arount when it shouldn't be ?
- dput/iput problem...
- what about .dotfiles ? working ? multiple dots ? etc....
- fix stuff like dir->i_count++ to atomic_inc(&dir->i_count) and simular?


Changes by Matija Nalis (mnalis@jagor.srce.hr) on umsdos dentry fixing
(started by Peter T. Waltenberg <peterw@karaka.chch.cri.nz>)

Current status (980211) - UMSDOS dentry-WIP-Beta 0.82:

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

- readdir - works. problems with symlink/hardlink resolving. see below.
- lookup -  works. problems with symlink/hardlink resolving. see below.
- permissions/owners stuff - works
- long file names - works
- read file - works
- switching MSDOS/UMSDOS - untested
- switching UMSDOS/MSDOS - untested
- pseudo root things - commented out mostly currently. To be fixed when
  dentries stuff is straightened out.
- resolve symlink - seems to work (ls -l shows both symlink and filename it
  points to, but dereferencing does not work. see below)
- dereference symlink - for some strange reason does not work. Is
  follow_symlink now obligatory (in contrary what is said in
  linux/Documentation/fs/vfs.txt ?) - working on it
- hard links - totally defunct currently.
- special files (block/char device, fifos, sockets...) - seems to work ok.
- other ioctls - mostly untested

- create symlink	- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- create hardlink	- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- creat file		- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- write file		- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- mkdir			- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- rmdir 		- WARNING: NONE OF WRITE OPERATIONS FIXED YET!
- umssyncing		- WARNING: NONE OF WRITE OPERATIONS FIXED YET!

Notes: heavy dentry kernel structures trashing. Probably some other kernel
structures compromised. Have SysRq support compiled in, and use
Sync/Emergency-remount-RO. And don't try mounting read/write yet - and then
you should have no big problems... 

Notes2: kernel structures trashing seems to be _MUCH_ lower if no
symlinks/hardlink present. (hardlinks seem to be biggest problem)

------------------------------------------------------------------------------

Some general notes:

There is great amount of kernel log messages. Use SysRq log-level 5 to turn
most of them off, or 4 to turn all but really fatal ones off. Probably good
idea to kill klogd/syslogd so it will only go to console.

It should work enough to test it, but probably not enough to give you second
chance to umount/rmmod module, recompile it, and reinsert it again. This is
first on my list of things to get fixed, as it would greatly improve speed
of compile/test/reboot/set_environment/recompile cycle by removing
'reboot/set_environment' component.

But I need some help from someone knowing about dentries/inodes use more
than I. If you can help, please contact me...

I'm unfortunatelly totally out of time to read linux-kernel, but I do check
for any messages having UMSDOS in subject, and read them. I should reply to
any direct Email in few days. If I don't - probably I never got your
message. You can try mnalis@open.hr or mnalis@voyager.hr; however
mnalis@jagor.srce.hr is preferable one.

------------------------------------------------------------------------------

some of my notes for myself:

- hardlinks/symlinks. test with files in not_the_same_dir
- also test not_the_same_dir for other file operations like rename etc.
- iput: device 00:00 inode 318 still has aliases! problem. Check in iput()
  for device 0,0. Probably null pointer passed arount when it shouldn't be ?
- dput/iput problem...
- what about .dotfiles ? working ? multiple dots ? etc....


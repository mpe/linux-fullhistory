Changes by Matija Nalis (mnalis@jagor.srce.hr) on umsdos dentry fixing
(started by Peter T. Waltenberg <peterw@karaka.chch.cri.nz>)
Final conversion to dentries Bill Hawes <whawes@star.net>

---------- WARNING --------- WARNING --------- WARNING -----------
There is no warning any more.
Both read-only and read-write stuff is fixed, both in
msdos-compatibile mode, and in umsdos EMD mode, and it seems stable.
There are still few symlink/hardlink nuisances, but those are not fatal.

I'd call it wide beta, and ask for as many people as possible to
come and test it! See notes below for some more information, or if
you are trying to use UMSDOS as root partition.
---------- WARNING --------- WARNING --------- WARNING -----------

Legend: those lines marked with '+' on the beggining of line indicates it
passed all of my tests, and performed perfect in all of them.

Current status (980915) - UMSDOS dentry-Beta 0.83:

(1) pure MSDOS (no --linux-.--- EMD file):

READ:
+ readdir			- works
+ lookup			- works
+ read file			- works

WRITE:
+ creat file			- works
+ delete file			- works
+ write file			- works
+ rename file (same dir)	- works
+ rename file (dif. dir)	- works
+ rename dir (same dir)		- works
+ rename dir (dif. dir)		- works
+ mkdir				- works
+ rmdir 			- works


(2) umsdos (with --linux-.--- EMD file):

READ:
+ readdir			- works
+ lookup 			- works
+ permissions/owners stuff	- works
+ long file names		- works
+ read file			- works
+ switching MSDOS/UMSDOS	- works
+ switching UMSDOS/MSDOS	- works
- pseudo root things		- DOES NOT WORK. COMMENTED OUT. See Notes below.
+ resolve symlink		- works
+ dereference symlink		- works
+ dangling symlink		- works
- hard links			- DOES NOT WORK CORRECTLY ALWAYS.
+ special files (block/char devices, FIFOs, sockets...)	- works
- various umsdos ioctls		- works


WRITE:
- create symlink		- sometimes works, but see WARNING below
- create hardlink		- works
+ create file			- works
+ create special file		- works
+ write to file			- works
+ rename file (same dir)	- works
+ rename file (dif. dir)	- works
- rename hardlink (same dir)	-
- rename hardlink (dif. dir)	-
- rename symlink (same dir)	- 
- rename symlink (dif. dir)	- problems sometimes. see warning below.
+ rename dir (same dir)		- works
+ rename dir (dif. dir)		- works
+ delete file			- works
+ notify_change (chown,perms)	- works
+ delete hardlink		- works
+ mkdir				- works
- rmdir 			- HMMM. see with clean --linux-.--- files...
- umssyncing (many ioctls)	- works


- CVF-FAT stuff (compressed DOS filesystem) - there is some support from Frank
  Gockel <gockel@sent13.uni-duisburg.de> to use it even under umsdosfs, but I
  have no way of testing it -- please let me know if there are problems specific
  to umsdos (for instance, it works under msdosfs, but not under umsdosfs).


Some current notes:

Note: creating and using pseudo-hardlinks is always non-perfect, especially
in filesystems that might be externally modified like umsdos. There is
example is specs file about it.

Warning: moving symlinks around may break them until umount/remount.

Warning: I seem to able to reproduce one problem with creting symlink after
I rm -rf directory: it is manifested as symlink apperantly being regular
file instead of symlink until next umount/mount pair. Tracking this one
down...

Wanted: I am currently looking for volunteers that already have UMSDOS
filesystems in pseudo-root, or are able to get them, to test PSEUDO-ROOT
stuff (and get new patches from me), which I currently can't do. Anyone know
of URL of nice UMSDOS pseudo-root ready image ? As always, any patches or
pointer to things done in wrong way (or ideas of better ways) are greatly
appreciated !

Note: If you are currently trying to use UMSDOS as root partition (with
linux installed in c:\linux) it will not work. Pseudo-root is currently
commented out. See 'wanted' above and contact me if you are interested in
testing it.

------------------------------------------------------------------------------

Some general notes:

Good idea when running development kernels is to have SysRq support compiled
in kernel, and use Sync/Emergency-remount-RO if you bump into problems (like
not being able to umount(2) umsdosfs, and because of it root partition also,
or panics which force you to reboot etc.)

I'm unfortunately somewhat out of time to read linux-kernel@vger, but I do
check for messages having "UMSDOS" in the subject, and read them.  I might
miss some in all that volume, though.  I should reply to any direct e-mail
in few days.  If I don't, probably I never got your message.  You can try
mnalis-umsdos@voyager.hr; however mnalis@jagor.srce.hr is preferable.


Changes by Matija Nalis (mnalis@jagor.srce.hr) on umsdos dentry fixing
(started by Peter T. Waltenberg <peterw@karaka.chch.cri.nz>)
(Final conversion to dentries Bill Hawes <whawes@star.net>)

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

Current status (981018) - UMSDOS dentry-Beta 0.83:

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
- pseudo root things		- works mostly. See notes below.
+ resolve symlink		- works
+ dereference symlink		- works
+ dangling symlink		- works
- hard links			- seems to work mostly
+ special files (block/char devices, FIFOs, sockets...)	- works
- various umsdos ioctls		- works


WRITE:
- create symlink		- works mostly, but see WARNING below
- create hardlink		- works, but see portability WARNING below
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
example is specs file about it. Specifically, moving directory which
contains hardlinks will break them.

Warning: moving symlinks around may break them until umount/remount.

Warning: I seem to able to reproduce one problem with creting symlink after
I rm -rf directory: it is manifested as symlink apperantly being regular
file instead of symlink until next umount/mount pair. Tracking this one
down...

Note: (about pseudoroot) If you are currently trying to use UMSDOS as root
partition (with linux installed in c:\linux) it will boot, but there are
some problems. Volunteers ready to test pseudoroot are needed (preferably
ones with working backups or unimportant data). There are problems with
different interpretation of hard links in normal in pseudo-root modes,
resulting is 'silent delete' of them sometimes. Also, '/DOS' pseudo
directory is only partially re-implemented and buggy. It works most of the
time, though.

Warning: (about creating hardlinks in pseudoroot mode) - hardlinks created in
pseudoroot mode are not compatibile with 'normal' hardlinks, and vice versa.
That is because harlink which is /foo in pseudoroot mode, becomes
/linux/foo in normal mode. I'm thinking about this one. However, since most
people either always use pseudoroot, or always use normal umsdos filesystem,
this is no showstopper.


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


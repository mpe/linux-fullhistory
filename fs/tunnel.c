/*  fs/tunnel.c: utility functions to support VFS tunnelling

    Copyright (C) 1999  Richard Gooch

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
    The postal address is:
      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.

    ChangeLog

    19991121   Richard Gooch <rgooch@atnf.csiro.au>
               Created.
*/
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/fs.h>


/*PUBLIC_FUNCTION*/
struct dentry *vfs_tunnel_lookup (const struct dentry *dentry,
				  const struct dentry *parent,
				  const struct dentry *covered)
/*  [SUMMARY] Lookup the corresponding dentry in the mounted-over FS.
    <dentry> The dentry which is in the overmounting FS.
    <parent> The parent of the dentry in the mounted-over FS. This may be NULL.
    <covered> The dentry covered by the root dentry of the overmounting FS.
    [RETURNS] A dentry on success, else NULL.
*/
{
    struct dentry *root = dentry->d_sb->s_root;

    if (covered == root) return NULL;
    if (parent) return lookup_dentry (dentry->d_name.name, parent, 0);
}   /*  End Function vfs_tunnel_lookup  */

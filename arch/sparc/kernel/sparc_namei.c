/* $Id: sparc_namei.c,v 1.2 1996/12/12 09:39:25 jj Exp $
 * linux/arch/sparc/kernel/sparc_namei.c
 *
 * Routines to handle famous /usr/gnemul/s*.
 * Included from linux/fs/namei.c
 */


#define BSD_EMUL "usr/gnemul/sunos/"
#define SOL_EMUL "usr/gnemul/solaris/"

static int dir_namei(const char *pathname, int *namelen, const char **name,
                     struct inode * base, struct inode **res_inode);
static int _namei(const char * pathname, struct inode * base,
                  int follow_links, struct inode ** res_inode);
int open_namei(const char * pathname, int flag, int mode,
               struct inode ** res_inode, struct inode * base);
                     
static int sparc_namei(const char ** pathname, struct inode ** base,
		int follow_links, struct inode ** res_inode)
{
	struct inode *emul_ino;
	int namelen;
	const char *name;
	int error;
	
	while (**pathname == '/')
		(*pathname)++;
	current->fs->root->i_count++;
	if (dir_namei (current->personality & PER_BSD ? BSD_EMUL : SOL_EMUL, 
		       &namelen, &name, current->fs->root, &emul_ino) >= 0 && emul_ino) {
		*res_inode = NULL;
		if ((error = _namei (*pathname, emul_ino, follow_links, res_inode)) >= 0 && *res_inode) {
			return 0;
		}
	}
	*base = current->fs->root;
	(*base)->i_count++;
	return -ENOTDIR;
}

static int sparc_open_namei(const char ** pathname, int flag, int mode,
		struct inode ** res_inode, struct inode ** base)
{
	struct inode *emul_ino;
	int namelen;
	const char *name;
	
	while (**pathname == '/')
		(*pathname)++;
	current->fs->root->i_count++;
	if (dir_namei (current->personality & PER_BSD ? BSD_EMUL : SOL_EMUL, 
		       &namelen, &name, current->fs->root, &emul_ino) >= 0 && emul_ino) {
		*res_inode = NULL;
		if (open_namei (*pathname, flag /* & ~O_CREAT */, mode, res_inode, emul_ino) >= 0 && *res_inode)
			return 0;
	}
	*base = current->fs->root;
	(*base)->i_count++;
	return -ENOTDIR;
}

/* check.c 30/01/95 22.05.32 */
void check_page_tables (void);
/* dir.c 18/03/95 00.30.50 */
int UMSDOS_dir_read (struct inode *inode,
	 struct file *filp,
	 char *buf,
	 int count);
void umsdos_lookup_patch (struct inode *dir,
	 struct inode *inode,
	 struct umsdos_dirent *entry,
	 off_t emd_pos);
int umsdos_inode2entry (struct inode *dir,
	 struct inode *inode,
	 struct umsdos_dirent *entry);
int umsdos_locate_path (struct inode *inode, char *path);
int umsdos_is_pseudodos (struct inode *dir, const char *name, int len);
int UMSDOS_lookup (struct inode *dir,
	 const char *name,
	 int len,
	 struct inode **result);
int umsdos_hlink2inode (struct inode *hlink, struct inode **result);
/* emd.c 30/01/95 22.05.32 */
int umsdos_readdir_kmem (struct inode *inode,
	 struct file *filp,
	 struct dirent *dirent,
	 int count);
int umsdos_file_read_kmem (struct inode *inode,
	 struct file *filp,
	 char *buf,
	 int count);
int umsdos_file_write_kmem (struct inode *inode,
	 struct file *filp,
	 char *buf,
	 int count);
int umsdos_emd_dir_write (struct inode *emd_dir,
	 struct file *filp,
	 char *buf,
	 int count);
int umsdos_emd_dir_read (struct inode *emd_dir,
	 struct file *filp,
	 char *buf,
	 int count);
struct inode *umsdos_emd_dir_lookup (struct inode *dir, int creat);
int umsdos_emd_dir_readentry (struct inode *emd_dir,
	 struct file *filp,
	 struct umsdos_dirent *entry);
int umsdos_writeentry (struct inode *dir,
	 struct inode *emd_dir,
	 struct umsdos_info *info,
	 int free_entry);
int umsdos_newentry (struct inode *dir, struct umsdos_info *info);
int umsdos_newhidden (struct inode *dir, struct umsdos_info *info);
int umsdos_delentry (struct inode *dir,
	 struct umsdos_info *info,
	 int isdir);
int umsdos_isempty (struct inode *dir);
int umsdos_findentry (struct inode *dir,
	 struct umsdos_info *info,
	 int expect);
/* file.c 30/01/95 22.05.56 */
/* inode.c 25/02/95 09.21.46 */
void UMSDOS_put_inode (struct inode *inode);
void UMSDOS_put_super (struct super_block *sb);
void UMSDOS_statfs (struct super_block *sb, struct statfs *buf);
int umsdos_real_lookup (struct inode *dir,
	 const char *name,
	 int len,
	 struct inode **result);
void umsdos_setup_dir_inode (struct inode *inode);
void umsdos_set_dirinfo (struct inode *inode,
	 struct inode *dir,
	 off_t f_pos);
int umsdos_isinit (struct inode *inode);
void umsdos_patch_inode (struct inode *inode,
	 struct inode *dir,
	 off_t f_pos);
int umsdos_get_dirowner (struct inode *inode, struct inode **result);
void UMSDOS_read_inode (struct inode *inode);
void UMSDOS_write_inode (struct inode *inode);
int UMSDOS_notify_change (struct inode *inode, struct iattr *attr);
struct super_block *UMSDOS_read_super (struct super_block *s,
	 void *data,
	 int silent);
/* ioctl.c 21/02/95 20.58.22 */
int UMSDOS_ioctl_dir (struct inode *dir,
	 struct file *filp,
	 unsigned int cmd,
	 unsigned long data);
/* mangle.c 30/01/95 22.05.56 */
void umsdos_manglename (struct umsdos_info *info);
int umsdos_evalrecsize (int len);
int umsdos_parse (const char *fname, int len, struct umsdos_info *info);
/* namei.c 30/01/95 22.05.56 */
void umsdos_lockcreate (struct inode *dir);
void umsdos_startlookup (struct inode *dir);
void umsdos_unlockcreate (struct inode *dir);
void umsdos_endlookup (struct inode *dir);
int UMSDOS_symlink (struct inode *dir,
	 const char *name,
	 int len,
	 const char *symname);
int UMSDOS_link (struct inode *oldinode,
	 struct inode *dir,
	 const char *name,
	 int len);
int UMSDOS_create (struct inode *dir,
	 const char *name,
	 int len,
	 int mode,
	 struct inode **result);
int UMSDOS_mkdir (struct inode *dir,
	 const char *name,
	 int len,
	 int mode);
int UMSDOS_mknod (struct inode *dir,
	 const char *name,
	 int len,
	 int mode,
	 int rdev);
int UMSDOS_rmdir (struct inode *dir, const char *name, int len);
int UMSDOS_unlink (struct inode *dir, const char *name, int len);
int UMSDOS_rename (struct inode *old_dir,
	 const char *old_name,
	 int old_len,
	 struct inode *new_dir,
	 const char *new_name,
	 int new_len);
/* rdir.c 18/03/95 00.30.18 */
int umsdos_rlookup_x (struct inode *dir,
	 const char *name,
	 int len,
	 struct inode **result,
	 int nopseudo);
int UMSDOS_rlookup (struct inode *dir,
	 const char *name,
	 int len,
	 struct inode **result);
/* symlink.c 30/01/95 22.05.32 */

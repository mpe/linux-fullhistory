/* check.c 23/01/95 03.38.30 */
void check_page_tables (void);
/* dir.c 22/06/95 00.22.12 */
int compat_msdos_create(struct inode *dir,
			const char *name,
			int len,
			int mode,
			struct inode **inode);
int  UMSDOS_dir_read ( struct file *filp,
	 char *buf,
	 size_t size,
	 loff_t *count);
void umsdos_lookup_patch (struct inode *dir,
	 struct inode *inode,
	 struct umsdos_dirent *entry,
	 off_t emd_pos);
int umsdos_inode2entry (struct inode *dir,
	 struct inode *inode,
	 struct umsdos_dirent *entry);
int umsdos_locate_path (struct inode *inode, char *path);
int umsdos_is_pseudodos (struct inode *dir, struct dentry *dentry);
int umsdos_lookup_x (
			    struct inode *dir,
			    struct dentry *dentry,
			    int nopseudo);
int UMSDOS_lookup(struct inode *dir,struct dentry *dentry);
	 
int umsdos_hlink2inode (struct inode *hlink, struct inode **result);
/* emd.c 22/06/95 00.22.04 */
ssize_t umsdos_file_write_kmem_real (struct file *filp,
				const char *buf,
				size_t  count,
				loff_t *offs);

ssize_t umsdos_file_read_kmem (struct inode *emd_dir,
	 struct file *filp,
	 char *buf,
	 size_t count,
	 loff_t *offs);
ssize_t umsdos_file_write_kmem (struct inode *emd_dir,
	 struct file *filp,
	 const char *buf,
	 size_t count,
	 loff_t *offs);
ssize_t umsdos_emd_dir_write (struct inode *emd_dir,
	 struct file *filp,
	 char *buf,
	 size_t count,
	 loff_t *offs);
ssize_t umsdos_emd_dir_read (struct inode *emd_dir,
	 struct file *filp,
	 char *buf,
	 size_t count,
	 loff_t *loffs);
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
/* file.c 25/01/95 02.25.38 */
/* inode.c 12/06/95 09.49.40 */
inline struct dentry *geti_dentry (struct inode *inode);
inline void inc_count (struct inode *inode);
void check_inode (struct inode *inode);
void check_dentry (struct dentry *dentry);
void fill_new_filp (struct file *filp, struct dentry *dentry);
void kill_dentry (struct dentry *dentry);
struct dentry *creat_dentry (const char *name,
			     const int len,
			     struct inode *inode,
			     struct dentry *parent);
void UMSDOS_put_inode (struct inode *inode);
void UMSDOS_put_super (struct super_block *sb);
int UMSDOS_statfs (struct super_block *sb,
	 struct statfs *buf,
	 int bufsiz);
int compat_umsdos_real_lookup (struct inode *dir,
	 const char *name,
	 int len,
	 struct inode **result);
int umsdos_real_lookup(struct inode *inode,struct dentry *dentry);	 
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
int UMSDOS_notify_change (struct dentry *dentry, struct iattr *attr);
struct super_block *UMSDOS_read_super (struct super_block *s,
	 void *data,
	 int silent);
/* ioctl.c 22/06/95 00.22.08 */
int UMSDOS_ioctl_dir (struct inode *dir,
	 struct file *filp,
	 unsigned int cmd,
	 unsigned long data);
/* mangle.c 25/01/95 02.25.38 */
void umsdos_manglename (struct umsdos_info *info);
int umsdos_evalrecsize (int len);
int umsdos_parse (const char *name,int len, struct umsdos_info *info);
/* namei.c 25/01/95 02.25.38 */
void umsdos_lockcreate (struct inode *dir);
void umsdos_startlookup (struct inode *dir);
void umsdos_unlockcreate (struct inode *dir);
void umsdos_endlookup (struct inode *dir);

int UMSDOS_symlink (struct inode *dir,
		    struct dentry *dentry,
		    const char *symname);
int UMSDOS_link (struct dentry *olddentry,
		 struct inode *dir,
		 struct dentry *dentry);
int UMSDOS_create (struct inode *dir,
		   struct dentry *dentry,
		   int mode);

int UMSDOS_mkdir (struct inode *dir,
		  struct dentry *dentry,
		  int mode);
int UMSDOS_mknod (struct inode *dir,
		  struct dentry *dentry,
		  int mode,
		  int rdev);
int UMSDOS_rmdir (struct inode *dir,struct dentry *dentry);
int UMSDOS_unlink (struct inode *dir, struct dentry *dentry);
int UMSDOS_rename (struct inode *old_dir,
		   struct dentry *old_dentry,
		   struct inode *new_dir,
		   struct dentry *new_dentry);
/* rdir.c 22/03/95 03.31.42 */
int umsdos_rlookup_x (struct inode *dir,
	 struct dentry *dentry,
	 int nopseudo);
int UMSDOS_rlookup (struct inode *dir,
		    struct dentry *dentry);
/* symlink.c 23/01/95 03.38.30 */

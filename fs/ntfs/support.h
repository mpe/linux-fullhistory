/*
 *  support.h
 *  Header file for specific support.c
 *
 *  Copyright (C) 1997 Régis Duchesne
 */

/* Debug levels */
#define DEBUG_OTHER	1
#define DEBUG_MALLOC	2

void ntfs_debug(int mask, const char *fmt, ...);
void *ntfs_malloc(int size);
void ntfs_free(void *block);
void ntfs_bzero(void *s, int n);
void *ntfs_memcpy(void *dest, const void *src, ntfs_size_t n);
void *ntfs_memmove(void *dest, const void *src, ntfs_size_t n);
void ntfs_error(const char *fmt,...);
int ntfs_read_mft_record(ntfs_volume *vol, int mftno, char *buf);
int ntfs_getput_clusters(ntfs_volume *pvol, int cluster, ntfs_size_t offs,
        ntfs_io *buf);
ntfs_time64_t ntfs_now(void);
int ntfs_dupuni2map(ntfs_volume *vol, ntfs_u16 *in, int in_len, char **out,
        int *out_len);
int ntfs_dupmap2uni(ntfs_volume *vol, char* in, int in_len, ntfs_u16 **out,
        int *out_len);

/*
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

typedef struct _osheader
{
  unsigned short os_entry;
  unsigned short os_version;
  void *reseth;
  struct _osheader *os_beg;
  void *os_end;
  long os_rsv1;
  void *os_magic;
  long os_date;
  unsigned short os_conf;
  unsigned short os_dosdate;
  char **p_root;
  unsigned char **pkbshift;
  void **p_run;
  char *p_rsv2;
} OSHEADER;

#define phystop    ((unsigned long *)0x42e)
#define _sysbase   ((OSHEADER **)0x4f2)
#define _p_cookies ((unsigned long **)0x5a0)
#define ramtop     ((unsigned long *)0x5a4)

/*
 * CVF extensions for fat-based filesystems
 *
 * written 1997,1998 by Frank Gockel <gockel@sent13.uni-duisburg.de>
 *
 */
 
#include<linux/sched.h>
#include<linux/fs.h>
#include<linux/msdos_fs.h>
#include<linux/msdos_fs_sb.h>
#include<linux/string.h>
#include<linux/fat_cvf.h>

#define MAX_CVF_FORMATS 3

struct cvf_format *cvf_formats[MAX_CVF_FORMATS]={NULL,NULL,NULL};
int cvf_format_use_count[MAX_CVF_FORMATS]={0,0,0};

int register_cvf_format(struct cvf_format*cvf_format)
{ int i,j;

  for(i=0;i<MAX_CVF_FORMATS;++i)
  { if(cvf_formats[i]==NULL)
    { /* free slot found, now check version */
      for(j=0;j<MAX_CVF_FORMATS;++j)
      { if(cvf_formats[j])
        { if(cvf_formats[j]->cvf_version==cvf_format->cvf_version)
          { printk("register_cvf_format: version %d already registered\n",
                   cvf_format->cvf_version);
            return -1;
          }
        }
      }
      cvf_formats[i]=cvf_format;
      cvf_format_use_count[i]=0;
      printk("CVF format %s (version id %d) successfully registered.\n",
             cvf_format->cvf_version_text,cvf_format->cvf_version);
      return 0;
    }
  }
  
  printk("register_cvf_format: too many formats\n");
  return -1;
}

int unregister_cvf_format(struct cvf_format*cvf_format)
{ int i;

  for(i=0;i<MAX_CVF_FORMATS;++i)
  { if(cvf_formats[i])
    { if(cvf_formats[i]->cvf_version==cvf_format->cvf_version)
      { if(cvf_format_use_count[i])
        { printk("unregister_cvf_format: format %d in use, cannot remove!\n",
          cvf_formats[i]->cvf_version);
          return -1;
        }
      
        printk("CVF format %s (version id %d) successfully unregistered.\n",
        cvf_formats[i]->cvf_version_text,cvf_formats[i]->cvf_version);
        cvf_formats[i]=NULL;
        return 0;
      }
    }
  }
  
  printk("unregister_cvf_format: format %d is not registered\n",
         cvf_format->cvf_version);
  return -1;
}

void dec_cvf_format_use_count_by_version(int version)
{ int i;

  for(i=0;i<MAX_CVF_FORMATS;++i)
  { if(cvf_formats[i])
    { if(cvf_formats[i]->cvf_version==version)
      { --cvf_format_use_count[i];
        if(cvf_format_use_count[i]<0)
        { cvf_format_use_count[i]=0;
          printk(KERN_EMERG "FAT FS/CVF: This is a bug in cvf_version_use_count\n");
        }
        return;
      }
    }
  }
  
  printk("dec_cvf_format_use_count_by_version: version %d not found ???\n",
         version);
}

int detect_cvf(struct super_block*sb,char*force)
{ int i;
  int found=0;
  int found_i=-1;

  if(force)
  { if(*force)
    { for(i=0;i<MAX_CVF_FORMATS;++i)
      { if(cvf_formats[i])
        { if(!strcmp(cvf_formats[i]->cvf_version_text,force))
            return i;
        }
      }
    }
  }

  for(i=0;i<MAX_CVF_FORMATS;++i)
  { if(cvf_formats[i])
    { if(cvf_formats[i]->detect_cvf(sb))
      { ++found;
        found_i=i;
      }
    }
  }
  
  if(found==1)return found_i;
  if(found>1)printk("CVF detection ambiguous, use cvf_format=xxx option\n"); 
  return -1;
}

#include <linux/types.h>
#include <asm/string.h>
#include <asm/errno.h>
#include <linux/sched.h>

/*
 * bad data accesses from these functions should be handled specially 
 * since they are to user areas and may or may not be valid.
 * on error -EFAULT should be returned.  -- Cort
 */
int __copy_tofrom_user_failure(void)
{
  current->tss.excount = 0;
  return -EFAULT;
}

int __copy_tofrom_user(unsigned long to, unsigned long from, int size)
{
  /* setup exception handling stuff */
  current->tss.excount++;
  current->tss.expc = (unsigned long )__copy_tofrom_user_failure;
  
  if (memcpy( (void *)to, (void *)from, (size_t) size) == -EFAULT )
  {
    /* take down exception handler stuff */
    current->tss.excount = 0;
    return -EFAULT;
  }
  current->tss.excount = 0;
  return 0; /* successful return */
}

/* Just like strncpy except in the return value:
 *
 * -EFAULT       if an exception occurs before the terminator is copied.
 * N             if the buffer filled.
 *
 * Otherwise the length of the string is returned.
 */
asmlinkage int __strncpy_from_user_failure(void)
{
  current->tss.excount = 0;
  return -EFAULT;
}

int __strncpy_from_user(unsigned long dest, unsigned long src, int count)
{
  int i = 0;
  /* setup exception handling stuff */
  current->tss.excount++;
  current->tss.expc = (unsigned long )__strncpy_from_user_failure;
  
  while ( i != count )
  {
    *(char *)(dest+i) = *(char *)(src+i);
    if ( *(char *)(src+i) == 0 )
    {
      return i;
    }
    i++;
  }
  *(char *)(dest+i) = (char)0;
  /* take down exception handler stuff */
  current->tss.excount = 0;
  return i;
}

int __clear_user_failure(void)
{
  current->tss.excount = 0;
  return -EFAULT;
}
int __clear_user(unsigned long addr, int size)
{
  /* setup exception handling stuff */
  current->tss.excount++;
  current->tss.expc = (unsigned long )__clear_user_failure;

  if ((int)memset((void *)addr,(int)0, (__kernel_size_t)size) == -EFAULT )
    {
      /* take down exception handler stuff */
      current->tss.excount = 0;
      return -EFAULT;
    }
  /* take down exception handler stuff */
  current->tss.excount = 0;
  return size;
}

/*
 * Return the length of the string including the NUL terminator
 * (strlen+1) or zero if an error occured.
 */
size_t strlen_user_failure(void)
{
  current->tss.excount = 0;
  return -EFAULT;
}
size_t strlen_user(char * s)
{
  size_t i;
  /* setup exception handling stuff */
  current->tss.excount++;
  current->tss.expc = (unsigned long )strlen_user_failure;
  
  i = strlen(s)+1;
  
  if ( i == -EFAULT)
    return -EFAULT;
  
  /* take down exception handler stuff */
  current->tss.excount = 0;

  return(i);
}


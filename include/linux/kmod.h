/*
	kmod header
*/

#include <linux/config.h>
#include <linux/errno.h>

#ifdef CONFIG_KMOD
extern int request_module(const char * name);
extern int exec_usermodehelper(char *program_path, char *argv[], char *envp[]);
#else
#define request_module(x) do {} while(0)
extern inline int exec_usermodehelper(char *program_path, char *argv[], char *envp[])
{
	return -EACCES;
}
#endif


/*
	kmod header
*/

#include <linux/config.h>
#include <linux/errno.h>

#ifdef CONFIG_KMOD
extern int request_module(const char * name);
extern int exec_usermodehelper(char *program_path, char *argv[], char *envp[]);
#ifdef CONFIG_HOTPLUG
extern char hotplug_path [];
extern int call_usermodehelper(char *path, char *argv[], char *envp[]);
#endif
#else
static inline int request_module(const char * name) { return -ENOSYS; }
static inline int exec_usermodehelper(char *program_path, char *argv[], char *envp[])
{
	return -EACCES;
}
#endif


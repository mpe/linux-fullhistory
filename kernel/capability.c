/*
 * linux/kernel/capability.c
 *
 * Copyright (C) 1997  Andrew Main <zefram@fysh.org>
 * Integrated into 2.1.97+,  Andrew G. Morgan <morgan@transmeta.com>
 */ 

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/string.h>

#include <asm/uaccess.h>

static inline void cap_fromuser(kernel_cap_t *k, __u32 *u)
{
     copy_from_user(k, u, sizeof(*k));
}


static inline void cap_touser(__u32 *u, const kernel_cap_t *k)
{
     copy_to_user(u, k, sizeof(*k));
}

spinlock_t task_capability_lock;

/*
 * For sys_getproccap() and sys_setproccap(), any of the three
 * capability set pointers may be NULL -- indicating that that set is
 * uninteresting and/or not to be changed.
 */

asmlinkage int sys_capget(cap_user_header_t header, cap_user_data_t data)
{
     int error = -EINVAL, pid;
     __u32 version;
     struct task_struct *target;

     if (!access_ok(VERIFY_WRITE, &header->version, sizeof(*header))) {
             /* not large enough for current header so indicate error */
             if (access_ok(VERIFY_WRITE, &header->version,
                           sizeof(header->version))) {
                     return error;
             }
             goto all_done;
     }

     copy_from_user(&version, &header->version, sizeof(header->version));
     if (version != _LINUX_CAPABILITY_VERSION) {
             /* if enough space for kernel version, write that */

     all_done:
             version = _LINUX_CAPABILITY_VERSION;
             copy_to_user(&header->version, &version,
                          sizeof(header->version));
             return error;
     }

     if (!access_ok(VERIFY_WRITE, data, sizeof(*data))) {
             return error;
     }

     copy_from_user(&pid, &header->pid, sizeof(header->pid));
     if (pid < 0) {
             return error;
     }

     spin_lock(&task_capability_lock);

     if (pid && pid != current->pid) {
             read_lock(&tasklist_lock);
             target = find_task_by_pid(pid);  /* identify target of query */
             if (!target) {
                     error = -ESRCH;
                     goto out;
             }
     } else {
             target = current;
     }

     cap_touser(&data->permitted, &target->cap_permitted);
     cap_touser(&data->inheritable, &target->cap_inheritable);
     cap_touser(&data->effective, &target->cap_effective);

     error = 0;

out:
     if (target != current) {
             read_unlock(&tasklist_lock);
     }
     spin_unlock(&task_capability_lock);
     return error;
}

/* set capabilities for all processes in a given process group */

static void cap_set_pg(int pgrp,
                    kernel_cap_t *effective,
                    kernel_cap_t *inheritable,
                    kernel_cap_t *permitted)
{
     struct task_struct *target;

     /* FIXME: do we need to have a write lock here..? */
     read_lock(&tasklist_lock);
     for_each_task(target) {
             if (target->pgrp != pgrp)
                     continue;
             target->cap_effective   = *effective;
             target->cap_inheritable = *inheritable;
             target->cap_permitted   = *permitted;
     }
     read_unlock(&tasklist_lock);
}

/* set capabilities for all processes other than 1 and self */

static void cap_set_all(kernel_cap_t *effective,
                     kernel_cap_t *inheritable,
                     kernel_cap_t *permitted)
{
     struct task_struct *target;

     /* FIXME: do we need to have a write lock here..? */
     read_lock(&tasklist_lock);
     /* ALL means everyone other than self or 'init' */
     for_each_task(target) {
             if (target == current || target->pid == 1)
                     continue;
             target->cap_effective   = *effective;
             target->cap_inheritable = *inheritable;
             target->cap_permitted   = *permitted;
     }
     read_unlock(&tasklist_lock);
}

/*
 * The restrictions on setting capabilities are specified as:
 *
 * [pid is for the 'target' task.  'current' is the calling task.]
 *
 * I: any raised capabilities must be a subset of the (old current) Permitted
 * P: any raised capabilities must be a subset of the (old current) permitted
 * E: must be set to a subset of (new target) Permitted
 */

asmlinkage int sys_capset(cap_user_header_t header, const cap_user_data_t data)
{
     kernel_cap_t inheritable, permitted, effective;
     __u32 version;
     struct task_struct *target;
     int error = -EINVAL, pid;

     if (!access_ok(VERIFY_WRITE, &header->version, sizeof(*header))) {
             /* not large enough for current header so indicate error */
             if (!access_ok(VERIFY_WRITE, &header->version,
                            sizeof(header->version))) {
                     return error;
             }
             goto all_done;
     }

     copy_from_user(&version, &header->version, sizeof(header->version));
     if (version != _LINUX_CAPABILITY_VERSION) {

     all_done:
             version = _LINUX_CAPABILITY_VERSION;
             copy_to_user(&header->version, &version,
                          sizeof(header->version));
             return error;
     }

     if (!access_ok(VERIFY_READ, data, sizeof(*data))) {
             return error;
     }

     /* may want to set other processes at some point -- for now demand 0 */
     copy_from_user(&pid, &header->pid, sizeof(pid));

     error = -EPERM;
     if (pid && !capable(CAP_SETPCAP))
             return error;

     spin_lock(&task_capability_lock);

     if (pid > 0 && pid != current->pid) {
             read_lock(&tasklist_lock);
             target = find_task_by_pid(pid);  /* identify target of query */
             if (!target) {
                     error = -ESRCH;
                     goto out;
             }
     } else {
             target = current;
     }

     /* copy from userspace */
     cap_fromuser(&effective, &data->effective);
     cap_fromuser(&inheritable, &data->inheritable);
     cap_fromuser(&permitted, &data->permitted);

     /* verify restrictions on target's new Inheritable set */
     if (!cap_issubset(inheritable,
                       cap_combine(target->cap_inheritable,
                                   current->cap_permitted))) {
             goto out;
     }

     /* verify restrictions on target's new Permitted set */
     if (!cap_issubset(permitted,
                       cap_combine(target->cap_permitted,
                                   current->cap_permitted))) {
             goto out;
     }

     /* verify the _new_Effective_ is a subset of the _new_Permitted_ */
     if (!cap_issubset(effective, permitted)) {
             goto out;
     }

     /* having verified that the proposed changes are legal,
           we now put them into effect. */
     error = 0;

     if (pid < 0) {
             if (pid == -1)  /* all procs other than current and init */
                     cap_set_all(&effective, &inheritable, &permitted);

             else            /* all procs in process group */
                     cap_set_pg(-pid, &effective, &inheritable, &permitted);
             goto spin_out;
     } else {
             /* FIXME: do we need to have a write lock here..? */
             target->cap_effective   = effective;
             target->cap_inheritable = inheritable;
             target->cap_permitted   = permitted;
     }

out:
     if (target != current) {
             read_unlock(&tasklist_lock);
     }
spin_out:
     spin_unlock(&task_capability_lock);
     return error;
}

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 *  hcl - SGI's Hardware Graph compatibility layer.
 *
 * Copyright (C) 1992-1997,2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/sched.h>                /* needed for smp_lock.h :( */
#include <linux/smp_lock.h>
#include <asm/sn/sgi.h>
#include <asm/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/hwgfs.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/simulator.h>

#define HCL_NAME "SGI-HWGRAPH COMPATIBILITY DRIVER"
#define HCL_TEMP_NAME "HCL_TEMP_NAME_USED_FOR_HWGRAPH_VERTEX_CREATE"
#define HCL_TEMP_NAME_LEN 44 
#define HCL_VERSION "1.0"

#define vertex_hdl_t hwgfs_handle_t
vertex_hdl_t hwgraph_root;
vertex_hdl_t linux_busnum;

extern void pci_bus_cvlink_init(void);

/*
 * Debug flag definition.
 */
#define OPTION_NONE             0x00
#define HCL_DEBUG_NONE 0x00000
#define HCL_DEBUG_ALL  0x0ffff
#if defined(CONFIG_HCL_DEBUG)
static unsigned int hcl_debug_init __initdata = HCL_DEBUG_NONE;
#endif
static unsigned int hcl_debug = HCL_DEBUG_NONE;
#if defined(CONFIG_HCL_DEBUG) && !defined(MODULE)
static unsigned int boot_options = OPTION_NONE;
#endif

invplace_t invplace_none = {
	GRAPH_VERTEX_NONE,
	GRAPH_VERTEX_PLACE_NONE,
	NULL
};

/*
 * init_hcl() - Boot time initialization.
 *
 */
int __init init_hcl(void)
{
	extern void string_table_init(struct string_table *);
	extern struct string_table label_string_table;
	extern int init_ioconfig_bus(void);
	extern int init_hwgfs_fs(void);
	int rv = 0;

	if (IS_RUNNING_ON_SIMULATOR()) {
		extern u64 klgraph_addr[];
		klgraph_addr[0] = 0xe000003000030000;
	}

	init_hwgfs_fs();

	/*
	 * Create the hwgraph_root.
	 */
	rv = hwgraph_path_add(NULL, EDGE_LBL_HW, &hwgraph_root);
	if (rv)
		printk ("WARNING: init_hcl: Failed to create hwgraph_root. Error = %d.\n", rv);

	/*
	 * Initialize the HCL string table.
	 */

	string_table_init(&label_string_table);

	/*
	 * Create the directory that links Linux bus numbers to our Xwidget.
	 */
	rv = hwgraph_path_add(hwgraph_root, EDGE_LBL_LINUX_BUS, &linux_busnum);
	if (linux_busnum == NULL) {
		panic("HCL: Unable to create %s\n", EDGE_LBL_LINUX_BUS);
		return(0);
	}

	pci_bus_cvlink_init();

	/*
	 * Initialize the ifconfgi_net driver that does network devices 
	 * Persistent Naming.
	 */
	init_ioconfig_bus();

	return(0);

}


/*
 * hcl_setup() - Process boot time parameters if given.
 *	"hcl="
 *	This routine gets called only if "hcl=" is given in the 
 *	boot line and before init_hcl().
 *
 *	We currently do not have any boot options .. when we do, 
 *	functionalities can be added here.
 *
 */
static int __init hcl_setup(char *str)
{
    while ( (*str != '\0') && !isspace (*str) )
    {
#ifdef CONFIG_HCL_DEBUG
        if (strncmp (str, "all", 3) == 0) {
            hcl_debug_init |= HCL_DEBUG_ALL;
            str += 3;
        } else 
        	return 0;
#endif
        if (*str != ',') return 0;
        ++str;
    }

    return 1;

}

__setup("hcl=", hcl_setup);


/*
 * Set device specific "fast information".
 *
 */
void
hwgraph_fastinfo_set(vertex_hdl_t de, arbitrary_info_t fastinfo)
{
	labelcl_info_replace_IDX(de, HWGRAPH_FASTINFO, fastinfo, NULL);
}


/*
 * Get device specific "fast information".
 *
 */
arbitrary_info_t
hwgraph_fastinfo_get(vertex_hdl_t de)
{
	arbitrary_info_t fastinfo;
	int rv;

	if (!de) {
		printk(KERN_WARNING "HCL: hwgraph_fastinfo_get handle given is NULL.\n");
		dump_stack();
		return(-1);
	}

	rv = labelcl_info_get_IDX(de, HWGRAPH_FASTINFO, &fastinfo);
	if (rv == 0)
		return(fastinfo);

	return(0);
}


/*
 * hwgraph_connectpt_set - Sets the connect point handle in de to the 
 *	given connect_de handle.  By default, the connect point of the 
 *	node is the parent.  This effectively changes this assumption.
 */
int
hwgraph_connectpt_set(vertex_hdl_t de, vertex_hdl_t connect_de)
{
	int rv;

	if (!de)
		return(-1);

	rv = labelcl_info_connectpt_set(de, connect_de);

	return(rv);
}


/*
 * hwgraph_connectpt_get: Returns the entry's connect point.
 *
 */
vertex_hdl_t
hwgraph_connectpt_get(vertex_hdl_t de)
{
	int rv;
	arbitrary_info_t info;
	vertex_hdl_t connect;

	rv = labelcl_info_get_IDX(de, HWGRAPH_CONNECTPT, &info);
	if (rv != 0) {
		return(NULL);
	}

	connect = (vertex_hdl_t)info;
	return(connect);

}


/*
 * hwgraph_mk_dir - Creates a directory entry.
 */
vertex_hdl_t
hwgraph_mk_dir(vertex_hdl_t de, const char *name,
                unsigned int namelen, void *info)
{

	int rv;
	labelcl_info_t *labelcl_info = NULL;
	vertex_hdl_t new_handle = NULL;
	vertex_hdl_t parent = NULL;

	/*
	 * Create the device info structure for hwgraph compatiblity support.
	 */
	labelcl_info = labelcl_info_create();
	if (!labelcl_info)
		return(NULL);

	/*
	 * Create an entry.
	 */
	new_handle = hwgfs_mk_dir(de, name, (void *)labelcl_info);
	if (!new_handle) {
		labelcl_info_destroy(labelcl_info);
		return(NULL);
	}

	/*
	 * Get the parent handle.
	 */
	parent = hwgfs_get_parent (new_handle);

	/*
	 * To provide the same semantics as the hwgraph, set the connect point.
	 */
	rv = hwgraph_connectpt_set(new_handle, parent);
	if (!rv) {
		/*
		 * We need to clean up!
		 */
	}

	/*
	 * If the caller provides a private data pointer, save it in the 
	 * labelcl info structure(fastinfo).  This can be retrieved via
	 * hwgraph_fastinfo_get()
	 */
	if (info)
		hwgraph_fastinfo_set(new_handle, (arbitrary_info_t)info);
		
	return(new_handle);

}

/*
 * hwgraph_path_add - Create a directory node with the given path starting 
 * from the given fromv.
 */
int
hwgraph_path_add(vertex_hdl_t  fromv,
		 char *path,
		 vertex_hdl_t *new_de)
{

	unsigned int	namelen = strlen(path);
	int		rv;

	/*
	 * We need to handle the case when fromv is NULL ..
	 * in this case we need to create the path from the 
	 * hwgraph root!
	 */
	if (fromv == NULL)
		fromv = hwgraph_root;

	/*
	 * check the entry doesn't already exist, if it does
	 * then we simply want new_de to point to it (otherwise
	 * we'll overwrite the existing labelcl_info struct)
	 */
	rv = hwgraph_edge_get(fromv, path, new_de);
	if (rv)	{	/* couldn't find entry so we create it */
		*new_de = hwgraph_mk_dir(fromv, path, namelen, NULL);
		if (new_de == NULL)
			return(-1);
		else
			return(0);
	}
	else 
 		return(0);

}

/*
 * hwgraph_register  - Creates a special device file.
 *
 */
vertex_hdl_t
hwgraph_register(vertex_hdl_t de, const char *name,
                unsigned int namelen, unsigned int flags, 
		unsigned int major, unsigned int minor,
                umode_t mode, uid_t uid, gid_t gid, 
		struct file_operations *fops,
                void *info)
{

        vertex_hdl_t new_handle = NULL;

        /*
         * Create an entry.
         */
        new_handle = hwgfs_register(de, name, flags, major,
				minor, mode, fops, info);

        return(new_handle);

}


/*
 * hwgraph_mk_symlink - Create a symbolic link.
 */
int
hwgraph_mk_symlink(vertex_hdl_t de, const char *name, unsigned int namelen,
                unsigned int flags, const char *link, unsigned int linklen, 
		vertex_hdl_t *handle, void *info)
{

	void *labelcl_info = NULL;
	int status = 0;
	vertex_hdl_t new_handle = NULL;

	/*
	 * Create the labelcl info structure for hwgraph compatiblity support.
	 */
	labelcl_info = labelcl_info_create();
	if (!labelcl_info)
		return(-1);

	/*
	 * Create a symbolic link.
	 */
	status = hwgfs_mk_symlink(de, name, flags, link,
				&new_handle, labelcl_info);
	if ( (!new_handle) || (!status) ){
		labelcl_info_destroy((labelcl_info_t *)labelcl_info);
		return(-1);
	}

	/*
	 * If the caller provides a private data pointer, save it in the 
	 * labelcl info structure(fastinfo).  This can be retrieved via
	 * hwgraph_fastinfo_get()
	 */
	if (info)
		hwgraph_fastinfo_set(new_handle, (arbitrary_info_t)info);

	*handle = new_handle;
	return(0);

}

/*
 * hwgraph_vertex_destroy - Destroy the entry
 */
int
hwgraph_vertex_destroy(vertex_hdl_t de)
{

	void *labelcl_info = NULL;

	labelcl_info = hwgfs_get_info(de);
	hwgfs_unregister(de);

	if (labelcl_info)
		labelcl_info_destroy((labelcl_info_t *)labelcl_info);

	return(0);
}

#if 0
/*
 * hwgraph_edge_add - This routines has changed from the original conext.
 * All it does now is to create a symbolic link from "from" to "to".
 */
/* ARGSUSED */
int
hwgraph_edge_add(vertex_hdl_t from, vertex_hdl_t to, char *name)
{

	char *path, *link;
	vertex_hdl_t handle = NULL;
	int rv, i;

	handle = hwgfs_find_handle(from, name, 0, 0, 0, 1);
	if (handle) {
		return(0);
	}

	path = kmalloc(1024, GFP_KERNEL);
	memset(path, 0x0, 1024);
	link = kmalloc(1024, GFP_KERNEL);
	memset(path, 0x0, 1024);
	i = hwgfs_generate_path (to, link, 1024);
	rv = hwgfs_mk_symlink (from, (const char *)name, 
			       DEVFS_FL_DEFAULT, link,
			       &handle, NULL);
	return(0);


}
#endif

int
hwgraph_edge_add(vertex_hdl_t from, vertex_hdl_t to, char *name)
{

	char *path, *link;
	char *s1;
	char *index;
	vertex_hdl_t handle = NULL;
	int rv;
	int i, count;

	path = kmalloc(1024, GFP_KERNEL);
	memset((char *)path, 0x0, 1024);
	link = kmalloc(1024, GFP_KERNEL);
	memset((char *)link, 0x0, 1024);

	i = hwgfs_generate_path (from, path, 1024);
	s1 = (char *)path;
	count = 0;
	while (1) {
		index = strstr (s1, "/");
		if (index) {
			count++;
			s1 = ++index;
		} else {
			count++;
			break;
		}
	}

	for (i = 0; i < count; i++) {
		strcat((char *)link,"../");
	}

	memset(path, 0x0, 1024);
	i = hwgfs_generate_path (to, path, 1024);
	strcat((char *)link, (char *)path);

	/*
	 * Otherwise, just create a symlink to the vertex.
	 * In this case the vertex was previous created with a REAL pathname.
	 */
	rv = hwgfs_mk_symlink (from, (const char *)name,
			       0, link,
			       &handle, NULL);
	kfree(path);
	kfree(link);

	return(rv);


}

/* ARGSUSED */
int
hwgraph_edge_get(vertex_hdl_t from, char *name, vertex_hdl_t *toptr)
{

	vertex_hdl_t target_handle = NULL;

	if (name == NULL)
		return(-1);

	if (toptr == NULL)
		return(-1);

	/*
	 * If the name is "." just return the current entry handle.
	 */
	if (!strcmp(name, HWGRAPH_EDGELBL_DOT)) {
		if (toptr) {
			*toptr = from;
		}
	} else if (!strcmp(name, HWGRAPH_EDGELBL_DOTDOT)) {
		/*
		 * Hmmm .. should we return the connect point or parent ..
		 * see in hwgraph, the concept of parent is the connectpt!
		 *
		 * Maybe we should see whether the connectpt is set .. if 
		 * not just return the parent!
		 */
		target_handle = hwgraph_connectpt_get(from);
		if (target_handle) {
			/*
			 * Just return the connect point.
			 */
			*toptr = target_handle;
			return(0);
		}
		target_handle = hwgfs_get_parent(from);
		*toptr = target_handle;

	} else {
		target_handle = hwgfs_find_handle (from, name, 0, 0,
					0, 1); /* Yes traverse symbolic links */
	}

	if (target_handle == NULL)
		return(-1);
	else
	 *toptr = target_handle;

	return(0);
}

/*
 * hwgraph_info_add_LBL - Adds a new label for the device.  Mark the info_desc
 *	of the label as INFO_DESC_PRIVATE and store the info in the label.
 */
/* ARGSUSED */
int
hwgraph_info_add_LBL( vertex_hdl_t de,
			char *name,
			arbitrary_info_t info)
{
	return(labelcl_info_add_LBL(de, name, INFO_DESC_PRIVATE, info));
}

/*
 * hwgraph_info_remove_LBL - Remove the label entry for the device.
 */
/* ARGSUSED */
int
hwgraph_info_remove_LBL( vertex_hdl_t de,
				char *name,
				arbitrary_info_t *old_info)
{
	return(labelcl_info_remove_LBL(de, name, NULL, old_info));
}

/*
 * hwgraph_info_replace_LBL - replaces an existing label with 
 *	a new label info value.
 */
/* ARGSUSED */
int
hwgraph_info_replace_LBL( vertex_hdl_t de,
				char *name,
				arbitrary_info_t info,
				arbitrary_info_t *old_info)
{
	return(labelcl_info_replace_LBL(de, name,
			INFO_DESC_PRIVATE, info,
			NULL, old_info));
}
/*
 * hwgraph_info_get_LBL - Get and return the info value in the label of the 
 * 	device.
 */
/* ARGSUSED */
int
hwgraph_info_get_LBL(vertex_hdl_t de,
			char *name,
			arbitrary_info_t *infop)
{
	return(labelcl_info_get_LBL(de, name, NULL, infop));
}

/*
 * hwgraph_info_get_exported_LBL - Retrieve the info_desc and info pointer 
 *	of the given label for the device.  The weird thing is that the label 
 *	that matches the name is return irrespective of the info_desc value!
 *	Do not understand why the word "exported" is used!
 */
/* ARGSUSED */
int
hwgraph_info_get_exported_LBL(vertex_hdl_t de,
				char *name,
				int *export_info,
				arbitrary_info_t *infop)
{
	int rc;
	arb_info_desc_t info_desc;

	rc = labelcl_info_get_LBL(de, name, &info_desc, infop);
	if (rc == 0)
		*export_info = (int)info_desc;

	return(rc);
}

/*
 * hwgraph_info_get_next_LBL - Returns the next label info given the 
 *	current label entry in place.
 *
 *	Once again this has no locking or reference count for protection.
 *
 */
/* ARGSUSED */
int
hwgraph_info_get_next_LBL(vertex_hdl_t de,
				char *buf,
				arbitrary_info_t *infop,
				labelcl_info_place_t *place)
{
	return(labelcl_info_get_next_LBL(de, buf, NULL, infop, place));
}

/*
 * hwgraph_info_export_LBL - Retrieve the specified label entry and modify 
 *	the info_desc field with the given value in nbytes.
 */
/* ARGSUSED */
int
hwgraph_info_export_LBL(vertex_hdl_t de, char *name, int nbytes)
{
	arbitrary_info_t info;
	int rc;

	if (nbytes == 0)
		nbytes = INFO_DESC_EXPORT;

	if (nbytes < 0)
		return(-1);

	rc = labelcl_info_get_LBL(de, name, NULL, &info);
	if (rc != 0)
		return(rc);

	rc = labelcl_info_replace_LBL(de, name,
				nbytes, info, NULL, NULL);

	return(rc);
}

/*
 * hwgraph_info_unexport_LBL - Retrieve the given label entry and change the 
 * label info_descr filed to INFO_DESC_PRIVATE.
 */
/* ARGSUSED */
int
hwgraph_info_unexport_LBL(vertex_hdl_t de, char *name)
{
	arbitrary_info_t info;
	int rc;

	rc = labelcl_info_get_LBL(de, name, NULL, &info);
	if (rc != 0)
		return(rc);

	rc = labelcl_info_replace_LBL(de, name,
				INFO_DESC_PRIVATE, info, NULL, NULL);

	return(rc);
}

/*
 * hwgraph_traverse - Find and return the handle starting from de.
 *
 */
graph_error_t
hwgraph_traverse(vertex_hdl_t de, char *path, vertex_hdl_t *found)
{
	/* 
	 * get the directory entry (path should end in a directory)
	 */

	*found = hwgfs_find_handle(de,	/* start dir */
			    path,	/* path */
			    0,		/* major */
			    0,		/* minor */
			    0,		/* char | block */
			    1);		/* traverse symlinks */
	if (*found == NULL)
		return(GRAPH_NOT_FOUND);
	else
		return(GRAPH_SUCCESS);
}

/*
 * hwgraph_path_to_vertex - Return the entry handle for the given 
 *	pathname .. assume traverse symlinks too!.
 */
vertex_hdl_t
hwgraph_path_to_vertex(char *path)
{
	return(hwgfs_find_handle(NULL,	/* start dir */
			path,		/* path */
		    	0,		/* major */
		    	0,		/* minor */
		    	0,		/* char | block */
		    	1));		/* traverse symlinks */
}

/*
 * hwgraph_inventory_remove - Removes an inventory entry.
 *
 *	Remove an inventory item associated with a vertex.   It is the caller's
 *	responsibility to make sure that there are no races between removing
 *	inventory from a vertex and simultaneously removing that vertex.
*/
int
hwgraph_inventory_remove(	vertex_hdl_t de,
				int class,
				int type,
				major_t controller,
				minor_t unit,
				int state)
{
	return(0); /* Just a Stub for IRIX code. */
}

/*
 * Find the canonical name for a given vertex by walking back through
 * connectpt's until we hit the hwgraph root vertex (or until we run
 * out of buffer space or until something goes wrong).
 *
 *	COMPATIBILITY FUNCTIONALITY
 * Walks back through 'parents', not necessarily the same as connectpts.
 *
 * Need to resolve the fact that does not return the path from 
 * "/" but rather it just stops right before /dev ..
 */
int
hwgraph_vertex_name_get(vertex_hdl_t vhdl, char *buf, uint buflen)
{
	char *locbuf;
	int   pos;

	if (buflen < 1)
		return(-1);	/* XXX should be GRAPH_BAD_PARAM ? */

	locbuf = kmalloc(buflen, GFP_KERNEL);

	pos = hwgfs_generate_path(vhdl, locbuf, buflen);
	if (pos < 0) {
		kfree(locbuf);
		return pos;
	}

	strcpy(buf, &locbuf[pos]);
	kfree(locbuf);
	return 0;
}

/*
** vertex_to_name converts a vertex into a canonical name by walking
** back through connect points until we hit the hwgraph root (or until
** we run out of buffer space).
**
** Usually returns a pointer to the original buffer, filled in as
** appropriate.  If the buffer is too small to hold the entire name,
** or if anything goes wrong while determining the name, vertex_to_name
** returns "UnknownDevice".
*/

#define DEVNAME_UNKNOWN "UnknownDevice"

char *
vertex_to_name(vertex_hdl_t vhdl, char *buf, uint buflen)
{
	if (hwgraph_vertex_name_get(vhdl, buf, buflen) == GRAPH_SUCCESS)
		return(buf);
	else
		return(DEVNAME_UNKNOWN);
}

graph_error_t
hwgraph_edge_remove(vertex_hdl_t from, char *name, vertex_hdl_t *toptr)
{
	return(GRAPH_ILLEGAL_REQUEST);
}

graph_error_t
hwgraph_vertex_unref(vertex_hdl_t vhdl)
{
	return(GRAPH_ILLEGAL_REQUEST);
}


EXPORT_SYMBOL(hwgraph_mk_dir);
EXPORT_SYMBOL(hwgraph_path_add);
EXPORT_SYMBOL(hwgraph_register);
EXPORT_SYMBOL(hwgraph_vertex_destroy);
EXPORT_SYMBOL(hwgraph_fastinfo_get);
EXPORT_SYMBOL(hwgraph_fastinfo_set);
EXPORT_SYMBOL(hwgraph_connectpt_set);
EXPORT_SYMBOL(hwgraph_connectpt_get);
EXPORT_SYMBOL(hwgraph_info_add_LBL);
EXPORT_SYMBOL(hwgraph_info_remove_LBL);
EXPORT_SYMBOL(hwgraph_info_replace_LBL);
EXPORT_SYMBOL(hwgraph_info_get_LBL);
EXPORT_SYMBOL(hwgraph_info_get_exported_LBL);
EXPORT_SYMBOL(hwgraph_info_get_next_LBL);
EXPORT_SYMBOL(hwgraph_info_export_LBL);
EXPORT_SYMBOL(hwgraph_info_unexport_LBL);
EXPORT_SYMBOL(hwgraph_traverse);
EXPORT_SYMBOL(hwgraph_vertex_name_get);

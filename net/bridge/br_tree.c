/*
 *	This code is derived from the avl functions in mmap.c
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <net/br.h>
#define _DEBUG_AVL

/*
 * Use an AVL (Adelson-Velskii and Landis) tree to speed up this search
 * from O(n) to O(log n), where n is the number of ULAs.
 * Written by Bruno Haible <haible@ma2s2.mathematik.uni-karlsruhe.de>.
 * Taken from mmap.c, extensively modified by John Hayes 
 * <hayes@netplumbing.com>
 * 98-02 Modified by Jean-Rene Peulve jr.peulve@aix.pacwan.net
 *		update port number when topology change
 *		return oldfdb when updating, for broadcast storm checking
 *		call addr_cmp once per node
 */

static struct fdb fdb_head;
static struct fdb *fhp = &fdb_head;
static struct fdb **fhpp = &fhp;
static int fdb_inited = 0;

#ifdef DEBUG_AVL
static void printk_avl (struct fdb * tree);
#endif

static int addr_cmp(unsigned char *a1, unsigned char *a2);

/*
 * fdb_head is the AVL tree corresponding to fdb
 * or, more exactly, its root.
 * A fdb has the following fields:
 *   fdb_avl_left     left son of a tree node
 *   fdb_avl_right    right son of a tree node
 *   fdb_avl_height   1+max(heightof(left),heightof(right))
 * The empty tree is represented as NULL.
 */
 
#ifndef avl_br_empty
#define avl_br_empty	(struct fdb *) NULL
#endif

/* Since the trees are balanced, their height will never be large. */
#define avl_maxheight	127
#define heightof(tree)	((tree) == avl_br_empty ? 0 : (tree)->fdb_avl_height)
/*
 * Consistency and balancing rules:
 * 1. tree->fdb_avl_height == 1+max(heightof(tree->fdb_avl_left),heightof(tree->fdb_avl_right))
 * 2. abs( heightof(tree->fdb_avl_left) - heightof(tree->fdb_avl_right) ) <= 1
 * 3. foreach node in tree->fdb_avl_left: node->fdb_avl_key <= tree->fdb_avl_key,
 *    foreach node in tree->fdb_avl_right: node->fdb_avl_key >= tree->fdb_avl_key.
 */

static int fdb_init(void)
{
	fdb_head.fdb_avl_height = 0;
	fdb_head.fdb_avl_left = (struct fdb *)0;
	fdb_head.fdb_avl_right = (struct fdb *)0;
	fdb_inited = 1;
	return(0);
}

struct fdb *br_avl_find_addr(unsigned char addr[6])
{
	struct fdb * result = NULL;
	struct fdb * tree;

	if (!fdb_inited)
		fdb_init();
#if (DEBUG_AVL)
	printk("searching for ula %02x:%02x:%02x:%02x:%02x:%02x\n",
		addr[0],
		addr[1],
		addr[2],
		addr[3],
		addr[4],
		addr[5]);
#endif /* DEBUG_AVL */
	for (tree = fhp ; ; ) {
		if (tree == avl_br_empty) {
#if (DEBUG_AVL)
			printk("search failed, returning node 0x%x\n", (unsigned int)result);
#endif /* DEBUG_AVL */
			return result;
		}

#if (DEBUG_AVL)
		printk("node 0x%x: checking ula %02x:%02x:%02x:%02x:%02x:%02x\n",
			(unsigned int)tree,
			tree->ula[0],
			tree->ula[1],
			tree->ula[2],
			tree->ula[3],
			tree->ula[4],
			tree->ula[5]);
#endif /* DEBUG_AVL */
		if (addr_cmp(addr, tree->ula) == 0) {
#if (DEBUG_AVL)
			printk("found node 0x%x\n", (unsigned int)tree);
#endif /* DEBUG_AVL */
			return tree;
		}
		if (addr_cmp(addr, tree->ula) < 0) {
			tree = tree->fdb_avl_left;
		} else {
			tree = tree->fdb_avl_right;
		}
	}
}


/*
 * Rebalance a tree.
 * After inserting or deleting a node of a tree we have a sequence of subtrees
 * nodes[0]..nodes[k-1] such that
 * nodes[0] is the root and nodes[i+1] = nodes[i]->{fdb_avl_left|fdb_avl_right}.
 */
static void br_avl_rebalance (struct fdb *** nodeplaces_ptr, int count)
{
	if (!fdb_inited)
		fdb_init();
	for ( ; count > 0 ; count--) {
		struct fdb ** nodeplace = *--nodeplaces_ptr;
		struct fdb * node = *nodeplace;
		struct fdb * nodeleft = node->fdb_avl_left;
		struct fdb * noderight = node->fdb_avl_right;
		int heightleft = heightof(nodeleft);
		int heightright = heightof(noderight);
		if (heightright + 1 < heightleft) {
			/*                                                      */
			/*                            *                         */
			/*                          /   \                       */
			/*                       n+2      n                     */
			/*                                                      */
			struct fdb * nodeleftleft = nodeleft->fdb_avl_left;
			struct fdb * nodeleftright = nodeleft->fdb_avl_right;
			int heightleftright = heightof(nodeleftright);
			if (heightof(nodeleftleft) >= heightleftright) {
				/*                                                        */
				/*                *                    n+2|n+3            */
				/*              /   \                  /    \             */
				/*           n+2      n      -->      /   n+1|n+2         */
				/*           / \                      |    /    \         */
				/*         n+1 n|n+1                 n+1  n|n+1  n        */
				/*                                                        */
				node->fdb_avl_left = nodeleftright; 
				nodeleft->fdb_avl_right = node;
				nodeleft->fdb_avl_height = 1 + (node->fdb_avl_height = 1 + heightleftright);
				*nodeplace = nodeleft;
			} else {
				/*                                                        */
				/*                *                     n+2               */
				/*              /   \                 /     \             */
				/*           n+2      n      -->    n+1     n+1           */
				/*           / \                    / \     / \           */
				/*          n  n+1                 n   L   R   n          */
				/*             / \                                        */
				/*            L   R                                       */
				/*                                                        */
				nodeleft->fdb_avl_right = nodeleftright->fdb_avl_left;
				node->fdb_avl_left = nodeleftright->fdb_avl_right;
				nodeleftright->fdb_avl_left = nodeleft;
				nodeleftright->fdb_avl_right = node;
				nodeleft->fdb_avl_height = node->fdb_avl_height = heightleftright;
				nodeleftright->fdb_avl_height = heightleft;
				*nodeplace = nodeleftright;
			}
		} else if (heightleft + 1 < heightright) {
			/* similar to the above, just interchange 'left' <--> 'right' */
			struct fdb * noderightright = noderight->fdb_avl_right;
			struct fdb * noderightleft = noderight->fdb_avl_left;
			int heightrightleft = heightof(noderightleft);
			if (heightof(noderightright) >= heightrightleft) {
				node->fdb_avl_right = noderightleft; 
				noderight->fdb_avl_left = node;
				noderight->fdb_avl_height = 1 + (node->fdb_avl_height = 1 + heightrightleft);
				*nodeplace = noderight;
			} else {
				noderight->fdb_avl_left = noderightleft->fdb_avl_right;
				node->fdb_avl_right = noderightleft->fdb_avl_left;
				noderightleft->fdb_avl_right = noderight;
				noderightleft->fdb_avl_left = node;
				noderight->fdb_avl_height = node->fdb_avl_height = heightrightleft;
				noderightleft->fdb_avl_height = heightright;
				*nodeplace = noderightleft;
			}
		} else {
			int height = (heightleft<heightright ? heightright : heightleft) + 1;
			if (height == node->fdb_avl_height)
				break;
			node->fdb_avl_height = height;
		}
	}
#ifdef DEBUG_AVL
	printk_avl(&fdb_head);
#endif /* DEBUG_AVL */
}

/* Insert a node into a tree.
 * Performance improvement:
 *	 call addr_cmp() only once per node and use result in a switch.
 * Return old node address if we knew that MAC address already
 * Return NULL if we insert the new node
 */
struct fdb *br_avl_insert (struct fdb * new_node)
{
	struct fdb ** nodeplace = fhpp;
	struct fdb ** stack[avl_maxheight];
	int stack_count = 0;
	struct fdb *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	if (!fdb_inited)
		fdb_init();
	for (;;) {
		struct fdb *node;
		
		node = *nodeplace;
		if (node == avl_br_empty)
			break;
		*stack_ptr++ = nodeplace; stack_count++;
		switch(addr_cmp(new_node->ula, node->ula)) {
		case 0: /* update */
                   if (node->port == new_node->port) {
			node->flags = new_node->flags;
			node->timer = new_node->timer;	
		   } else if (!(node->flags & FDB_ENT_VALID) &&
				node->port) {
			/* update fdb but never for local interfaces */
#if (DEBUG_AVL)
			printk("node 0x%x:port changed old=%d new=%d\n",
				(unsigned int)node, node->port,new_node->port);
#endif
			/* JRP: update port as well if the topology change !
			 * Don't do this while entry is still valid otherwise
			 * a broadcast that we flooded and is reentered by another
			 * port would mess up the good port number.
			 * The fdb list per port needs to be updated as well.
			 */
			requeue_fdb(node, new_node->port);
			node->flags = new_node->flags;
			node->timer = new_node->timer;	
#if (DEBUG_AVL)
			printk_avl(&fdb_head);
#endif /* DEBUG_AVL */
		   }
		   return node;		/* pass old fdb to caller */

		case 1: /* new_node->ula > node->ula */
		   nodeplace = &node->fdb_avl_right;
		   break;
		default: /* -1 => new_node->ula < node->ula */
		   nodeplace = &node->fdb_avl_left;
		}
	}
#if (DEBUG_AVL)
	printk("node 0x%x: adding ula %02x:%02x:%02x:%02x:%02x:%02x\n",
		(unsigned int)new_node,
		new_node->ula[0],
		new_node->ula[1],
		new_node->ula[2],
		new_node->ula[3],
		new_node->ula[4],
		new_node->ula[5]);
#endif /* (DEBUG_AVL) */
	new_node->fdb_avl_left = avl_br_empty;
	new_node->fdb_avl_right = avl_br_empty;
	new_node->fdb_avl_height = 1;
	*nodeplace = new_node;
	br_avl_rebalance(stack_ptr,stack_count);
#ifdef DEBUG_AVL
	printk_avl(&fdb_head);
#endif /* DEBUG_AVL */
	return NULL;		/* this is a new node */
}


/* Removes a node out of a tree. */
static int br_avl_remove (struct fdb * node_to_delete)
{
	struct fdb ** nodeplace = fhpp;
	struct fdb ** stack[avl_maxheight];
	int stack_count = 0;
	struct fdb *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	struct fdb ** nodeplace_to_delete;
	if (!fdb_inited)
		fdb_init();
	for (;;) {
		struct fdb * node = *nodeplace;
		if (node == avl_br_empty) {
			/* what? node_to_delete not found in tree? */
			printk(KERN_ERR "br: avl_remove: node to delete not found in tree\n");
			return(-1);
		}
		*stack_ptr++ = nodeplace; stack_count++;
		if (addr_cmp(node_to_delete->ula, node->ula) == 0)
				break;
		if (addr_cmp(node_to_delete->ula, node->ula) < 0)
			nodeplace = &node->fdb_avl_left;
		else
			nodeplace = &node->fdb_avl_right;
	}
	nodeplace_to_delete = nodeplace;
	/* Have to remove node_to_delete = *nodeplace_to_delete. */
	if (node_to_delete->fdb_avl_left == avl_br_empty) {
		*nodeplace_to_delete = node_to_delete->fdb_avl_right;
		stack_ptr--; stack_count--;
	} else {
		struct fdb *** stack_ptr_to_delete = stack_ptr;
		struct fdb ** nodeplace = &node_to_delete->fdb_avl_left;
		struct fdb * node;
		for (;;) {
			node = *nodeplace;
			if (node->fdb_avl_right == avl_br_empty)
				break;
			*stack_ptr++ = nodeplace; stack_count++;
			nodeplace = &node->fdb_avl_right;
		}
		*nodeplace = node->fdb_avl_left;
		/* node replaces node_to_delete */
		node->fdb_avl_left = node_to_delete->fdb_avl_left;
		node->fdb_avl_right = node_to_delete->fdb_avl_right;
		node->fdb_avl_height = node_to_delete->fdb_avl_height;
		*nodeplace_to_delete = node; /* replace node_to_delete */
		*stack_ptr_to_delete = &node->fdb_avl_left; /* replace &node_to_delete->fdb_avl_left */
	}
	br_avl_rebalance(stack_ptr,stack_count);
	return(0);
}

#ifdef DEBUG_AVL

/* print a tree */
static void printk_avl (struct fdb * tree)
{
	if (tree != avl_br_empty) {
		printk("(");
		printk("%02x:%02x:%02x:%02x:%02x:%02x(%d)",
			tree->ula[0],
			tree->ula[1],
			tree->ula[2],
			tree->ula[3],
			tree->ula[4],
			tree->ula[5],
			tree->port);
		if (tree->fdb_avl_left != avl_br_empty) {
			printk_avl(tree->fdb_avl_left);
			printk("<");
		}
		if (tree->fdb_avl_right != avl_br_empty) {
			printk(">");
			printk_avl(tree->fdb_avl_right);
		}
		printk(")\n");
	}
}

static char *avl_check_point = "somewhere";

/* check a tree's consistency and balancing */
static void avl_checkheights (struct fdb * tree)
{
	int h, hl, hr;

	if (tree == avl_br_empty)
		return;
	avl_checkheights(tree->fdb_avl_left);
	avl_checkheights(tree->fdb_avl_right);
	h = tree->fdb_avl_height;
	hl = heightof(tree->fdb_avl_left);
	hr = heightof(tree->fdb_avl_right);
	if ((h == hl+1) && (hr <= hl) && (hl <= hr+1))
		return;
	if ((h == hr+1) && (hl <= hr) && (hr <= hl+1))
		return;
	printk("%s: avl_checkheights: heights inconsistent\n",avl_check_point);
}

/* check that all values stored in a tree are < key */
static void avl_checkleft (struct fdb * tree, fdb_avl_key_t key)
{
	if (tree == avl_br_empty)
		return;
	avl_checkleft(tree->fdb_avl_left,key);
	avl_checkleft(tree->fdb_avl_right,key);
	if (tree->fdb_avl_key < key)
		return;
	printk("%s: avl_checkleft: left key %lu >= top key %lu\n",avl_check_point,tree->fdb_avl_key,key);
}

/* check that all values stored in a tree are > key */
static void avl_checkright (struct fdb * tree, fdb_avl_key_t key)
{
	if (tree == avl_br_empty)
		return;
	avl_checkright(tree->fdb_avl_left,key);
	avl_checkright(tree->fdb_avl_right,key);
	if (tree->fdb_avl_key > key)
		return;
	printk("%s: avl_checkright: right key %lu <= top key %lu\n",avl_check_point,tree->fdb_avl_key,key);
}

/* check that all values are properly increasing */
static void avl_checkorder (struct fdb * tree)
{
	if (tree == avl_br_empty)
		return;
	avl_checkorder(tree->fdb_avl_left);
	avl_checkorder(tree->fdb_avl_right);
	avl_checkleft(tree->fdb_avl_left,tree->fdb_avl_key);
	avl_checkright(tree->fdb_avl_right,tree->fdb_avl_key);
}

#endif /* DEBUG_AVL */

static int addr_cmp(unsigned char a1[], unsigned char a2[])
{
	int i;

	for (i=0; i<6; i++) {
		if (a1[i] > a2[i]) return(1);
		if (a1[i] < a2[i]) return(-1);
	}
	return(0);
}

/* Vova Oksman: function for copy tree to the buffer */
void sprintf_avl (char **pbuffer, struct fdb * tree, off_t *pos,
					int* len, off_t offset, int length)
{
	int size;

	if( 0 == *pos){
		if(avl_br_empty == tree)
		/* begin from the root */
			tree = fhp;
		*pos = *len;
	}

	if (*pos >= offset+length)
		return;

	if (tree != avl_br_empty) {
		/* don't write the local device */
		if(tree->port != 0){
			size = sprintf(*pbuffer,
				   "%02x:%02x:%02x:%02x:%02x:%02x     %s       %d         %ld\n",
				   tree->ula[0],tree->ula[1],tree->ula[2],
				   tree->ula[3],tree->ula[4],tree->ula[5], 
				   port_info[tree->port].dev->name, tree->flags,CURRENT_TIME-tree->timer);

			(*pos)+=size;
			(*len)+=size;
			(*pbuffer)+=size;
		}
		if (*pos <= offset)
			*len=0;

		if (tree->fdb_avl_left != avl_br_empty) {
			sprintf_avl (pbuffer,tree->fdb_avl_left,pos,len,offset,length);
		}
		if (tree->fdb_avl_right != avl_br_empty) {
			sprintf_avl (pbuffer,tree->fdb_avl_right,pos,len,offset,length);
		}

	}

	return;
}

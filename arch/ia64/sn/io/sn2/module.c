/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/io.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/module.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/xtalk/xswitch.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/sn_cpuid.h>


/* #define LDEBUG	1  */

#ifdef LDEBUG
#define DPRINTF		printk
#define printf		printk
#else
#define DPRINTF(x...)
#endif

module_t	       *modules[MODULE_MAX];
int			nummodules;

#define SN00_SERIAL_FUDGE	0x3b1af409d513c2
#define SN0_SERIAL_FUDGE	0x6e

void
encode_int_serial(uint64_t src,uint64_t *dest)
{
    uint64_t val;
    int i;

    val = src + SN00_SERIAL_FUDGE;


    for (i = 0; i < sizeof(long long); i++) {
	((char*)dest)[i] =
	    ((char*)&val)[sizeof(long long)/2 +
			 ((i%2) ? ((i/2 * -1) - 1) : (i/2))];
    }
}


void
decode_int_serial(uint64_t src, uint64_t *dest)
{
    uint64_t val;
    int i;

    for (i = 0; i < sizeof(long long); i++) {
	((char*)&val)[sizeof(long long)/2 +
		     ((i%2) ? ((i/2 * -1) - 1) : (i/2))] =
	    ((char*)&src)[i];
    }

    *dest = val - SN00_SERIAL_FUDGE;
}


void
encode_str_serial(const char *src, char *dest)
{
    int i;

    for (i = 0; i < MAX_SERIAL_NUM_SIZE; i++) {

	dest[i] = src[MAX_SERIAL_NUM_SIZE/2 +
		     ((i%2) ? ((i/2 * -1) - 1) : (i/2))] +
	    SN0_SERIAL_FUDGE;
    }
}

void
decode_str_serial(const char *src, char *dest)
{
    int i;

    for (i = 0; i < MAX_SERIAL_NUM_SIZE; i++) {
	dest[MAX_SERIAL_NUM_SIZE/2 +
	    ((i%2) ? ((i/2 * -1) - 1) : (i/2))] = src[i] -
	    SN0_SERIAL_FUDGE;
    }
}


module_t *module_lookup(moduleid_t id)
{
    int			i;

    for (i = 0; i < nummodules; i++)
	if (modules[i]->id == id) {
	    DPRINTF("module_lookup: found m=0x%p\n", modules[i]);
	    return modules[i];
	}

    return NULL;
}

/*
 * module_add_node
 *
 *   The first time a new module number is seen, a module structure is
 *   inserted into the module list in order sorted by module number
 *   and the structure is initialized.
 *
 *   The node number is added to the list of nodes in the module.
 */

module_t *module_add_node(geoid_t geoid, cnodeid_t cnodeid)
{
    module_t	       *m;
    int			i;
    char		buffer[16];
    moduleid_t		moduleid;

    memset(buffer, 0, 16);
    moduleid = geo_module(geoid);
    format_module_id(buffer, moduleid, MODULE_FORMAT_BRIEF);
    DPRINTF("module_add_node: moduleid=%s node=%d\n", buffer, cnodeid);

    if ((m = module_lookup(moduleid)) == 0) {
	m = kmalloc(sizeof (module_t), GFP_KERNEL);
	memset(m, 0 , sizeof(module_t));
	ASSERT_ALWAYS(m);

	m->id = moduleid;
	spin_lock_init(&m->lock);

	mutex_init_locked(&m->thdcnt);

	/* Insert in sorted order by module number */

	for (i = nummodules; i > 0 && modules[i - 1]->id > moduleid; i--)
	    modules[i] = modules[i - 1];

	modules[i] = m;
	nummodules++;
    }

    m->nodes[m->nodecnt] = cnodeid;
    m->geoid[m->nodecnt] = geoid;
    m->nodecnt++;

    DPRINTF("module_add_node: module %s now has %d nodes\n", buffer, m->nodecnt);

    return m;
}

int module_probe_snum(module_t *m, nasid_t nasid)
{
    lboard_t	       *board;
    klmod_serial_num_t *comp;
    char serial_number[16];

    /*
     * record brick serial number
     */
    board = find_lboard((lboard_t *) KL_CONFIG_INFO(nasid), KLTYPE_SNIA);

    if (! board || KL_CONFIG_DUPLICATE_BOARD(board))
    {
#if	LDEBUG
	printf ("module_probe_snum: no IP35 board found!\n");
#endif
	return 0;
    }

    board_serial_number_get( board, serial_number );
    if( serial_number[0] != '\0' ) {
	encode_str_serial( serial_number, m->snum.snum_str );
	m->snum_valid = 1;
    }
#if	LDEBUG
    else {
	printf("module_probe_snum: brick serial number is null!\n");
    }
    printf("module_probe_snum: brick serial number == %s\n", serial_number);
#endif /* DEBUG */

    board = find_lboard((lboard_t *) KL_CONFIG_INFO(nasid),
			KLTYPE_IOBRICK_XBOW);

    if (! board || KL_CONFIG_DUPLICATE_BOARD(board))
	return 0;

    comp = GET_SNUM_COMP(board);

    if (comp) {
#if LDEBUG
	    int i;

	    printf("********found module with id %x and string", m->id);

	    for (i = 0; i < MAX_SERIAL_NUM_SIZE; i++)
		printf(" %x ", comp->snum.snum_str[i]);

	    printf("\n");	/* Fudged string is not ASCII */
#endif

	    if (comp->snum.snum_str[0] != '\0') {
		memcpy(m->sys_snum,
		       comp->snum.snum_str,
		       MAX_SERIAL_NUM_SIZE);
		m->sys_snum_valid = 1;
	    }
    }

    if (m->sys_snum_valid)
	return 1;
    else {
	DPRINTF("Invalid serial number for module %d, "
		"possible missing or invalid NIC.", m->id);
	return 0;
    }
}

void
io_module_init(void)
{
    cnodeid_t		node;
    lboard_t	       *board;
    nasid_t		nasid;
    int			nserial;
    module_t	       *m;

    DPRINTF("*******module_init\n");

    nserial = 0;

    for (node = 0; node < numnodes; node++) {
	nasid = COMPACT_TO_NASID_NODEID(node);

	board = find_lboard((lboard_t *) KL_CONFIG_INFO(nasid), KLTYPE_SNIA);
	ASSERT(board);

	m = module_add_node(board->brd_geoid, node);

	if (! m->snum_valid && module_probe_snum(m, nasid))
	    nserial++;
    }

    DPRINTF("********found total of %d serial numbers in the system\n",
	    nserial);

    if (nserial == 0)
	DPRINTF(KERN_WARNING  "io_module_init: No serial number found.\n");
}

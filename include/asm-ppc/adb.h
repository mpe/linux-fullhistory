/*
 * Definitions for ADB (Apple Desktop Bus) support.
 */
#ifndef __PPC_ADB_H
#define __PPC_ADB_H

/* ADB commands */
#define ADB_BUSRESET		0
#define ADB_FLUSH(id)		(1 + ((id) << 4))
#define ADB_WRITEREG(id, reg)	(8 + (reg) + ((id) << 4))
#define ADB_READREG(id, reg)	(0xc + (reg) + ((id) << 4))

/* ADB default device IDs (upper 4 bits of ADB command byte) */
#define ADB_DONGLE	1	/* "software execution control" devices */
#define ADB_KEYBOARD	2
#define ADB_MOUSE	3
#define ADB_TABLET	4
#define ADB_MODEM	5
#define ADB_MISC	7	/* maybe a monitor */

#define ADB_RET_OK	0
#define ADB_RET_TIMEOUT	3

/* The kind of ADB request. The controller may emulate some
   of all of those CUDA/PMU packet kinds */
#define ADB_PACKET	0
#define CUDA_PACKET	1
#define ERROR_PACKET	2
#define TIMER_PACKET	3
#define POWER_PACKET	4
#define MACIIC_PACKET	5
#define PMU_PACKET	6

#ifdef __KERNEL__

struct adb_request {
    unsigned char data[32];
    int nbytes;
    unsigned char reply[32];
    int reply_len;
    unsigned char reply_expected;
    unsigned char sent;
    unsigned char complete;
    void (*done)(struct adb_request *);
    void *arg;
    struct adb_request *next;
};

struct adb_ids {
    int nids;
    unsigned char id[16];
};

/* Messages sent thru the client_list notifier. You should NOT stop
   the operation, at least not with this version */
enum adb_message {
    ADB_MSG_POWERDOWN,	/* Currently called before sleep only */
    ADB_MSG_PRE_RESET,	/* Called before resetting the bus */
    ADB_MSG_POST_RESET	/* Called after resetting the bus (re-do init & register) */
};
extern struct notifier_block *adb_client_list;

/* Kind of ADB controller */
enum adb_hw {
    ADB_NONE, ADB_VIACUDA, ADB_VIAPMU, ADB_MACIO, ADB_UNKNOWN
};

/* Definition of a controller */
extern struct adb_controller {
    enum adb_hw		kind;
	
    int		(*send_request)(struct adb_request *req, int sync);
    int		(*autopoll)(int devs);
    int		(*reset_bus)(void);
    void	(*poll)(void);
} *adb_controller;
extern enum adb_hw adb_hardware;

/* Values for adb_request flags */
#define ADBREQ_REPLY	1	/* expect reply */
#define ADBREQ_SYNC	2	/* poll until done */

void adb_init(void);

int adb_request(struct adb_request *req, void (*done)(struct adb_request *),
		int flags, int nbytes, ...);
int adb_register(int default_id,int handler_id,struct adb_ids *ids,
		 void (*handler)(unsigned char *, int, struct pt_regs *, int));
void adb_input(unsigned char *, int, struct pt_regs *, int);

int adb_try_handler_change(int address, int new_id);
int adb_get_infos(int address, int *original_address, int *handler_id);

int adb_reset_bus(void);

void adb_poll(void);

#endif /* __KERNEL__ */

#endif /* __PPC_ADB_H */

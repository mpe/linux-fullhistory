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


#ifdef __KERNEL__

struct adb_request {
    unsigned char data[16];
    int nbytes;
    unsigned char reply[16];
    int reply_len;
    unsigned char reply_expected;
    unsigned char sent;
    unsigned char complete;
    void (*done)(struct adb_request *);
    void *arg;
    struct adb_request *next;
};

extern enum adb_hw {
	ADB_NONE, ADB_VIACUDA, ADB_VIAPMU, ADB_MACIO
} adb_hardware;

extern int (*adb_send_request)(struct adb_request *req, int sync);
extern int (*adb_autopoll)(int on);

/* Values for adb_request flags */
#define ADBREQ_REPLY	1	/* expect reply */
#define ADBREQ_SYNC	2	/* poll until done */

void adb_init(void);
int adb_request(struct adb_request *req, void (*done)(struct adb_request *),
		int flags, int nbytes, ...);
int adb_register(int default_id,
		 void (*handler)(unsigned char *, int, struct pt_regs *, int));
void adb_input(unsigned char *, int, struct pt_regs *, int);

#endif /* __KERNEL__ */

#endif /* __PPC_ADB_H */

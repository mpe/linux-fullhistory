#ifndef _PNP_H_
#define _PNP_H_

#define MAX_PNP_CARDS 	16

#define PNP_DEVID(a, b, c, id) \
	((a-'@')<<26) | ((b-'@')<<21) | ((c-'@') << 16) | (id&0xffff)

#define NO_PORT	0
#define NO_IRQ  0
#define NO_DMA	4

struct pnp_port_resource
{
	short range_min, range_max;
	unsigned char align, len;
};

struct pnp_func
  {
    struct pnp_dev *dev;
    unsigned long   flags;
    struct pnp_func *next;
    int nports;
    struct pnp_port_resource ports[8];
    int nirq;
    unsigned short irq[2];
    int ndma;
    unsigned char dma[2];
  };

struct pnp_dev
  {
    int             key;	/* A PnP device id identifying this device */
    char           *name;	/* ANSI identifier string */
    int		    devno;	/* Logical device number within a card */
    int             ncompat;	/* Number of compatible device idents */
    int             compat_keys[8];	/* List of PnP compatible device idents */
    struct pnp_card *card;	/* Link to the card which holds this device */
    struct pnp_dev *next;	/* Pointer to next logical device or NULL */

    int 	    nports, nirq, ndma;

    int             nfunc;	/* Number of dependent function records */
    struct pnp_func *functions;	/* List of dependent functions */
    int             driver;	/* Driver signature or 0 */
    int		    preconfig;  /* 1 if config has been set manully */

  };

struct pnp_card
  {
    int             key;	/* Unique PnP device identifier */
    char           *name;	/* ANSI identifier string of the card */
    int             csn;	/* Card select number */

    char            pnp_version;
    char            vendor_version;

    int             driven;	/* 0=No driver assigned, */
    /* 1=Drivers assigned to some of logical devices */
    /* 2=Card and all of it's devices have a driver */
    int             relocated;	/* 0=Card is inactive, 1=card is up and running */

    int             ndevs;	/* Number of logical devices on the card */
    struct pnp_dev *devices;	/* Pointer to first function entry */
  };

typedef struct pnp_card_info {
  struct pnp_card card;
  char name[64];
} pnp_card_info;

extern int      pnp_card_count;
extern struct 	pnp_card *pnp_cards[MAX_PNP_CARDS];
extern struct pnp_dev *pnp_device_list;

extern int pnp_trace;

/*
 * Callable functions
 */

extern void pnp_init(void);		/* Called by kernel during boot */
extern void terminate_pnp(void);

extern int pnp_connect(char *driver_name);
extern void pnp_disconnect(int driver_signature);

/*
 * pnp_get_descr() returns an ASCII desctription string for a device.
 * The parameter is an compressed EISA identifier of the device/card.
 */
extern char *pnp_get_descr (int id);

extern void pnp_enable_device(struct pnp_dev *dev, int state);
extern void pnp_set_port(struct pnp_dev *dev, int selec, unsigned short base);
extern void pnp_set_irq(struct pnp_dev *dev, int selec, unsigned short val);
extern void pnp_set_dma(struct pnp_dev *dev, int selec, unsigned short val);
extern unsigned short pnp_get_port(struct pnp_dev *dev, int selec);
extern unsigned short pnp_get_irq(struct pnp_dev *dev, int selec);
extern unsigned short pnp_get_dma(struct pnp_dev *dev, int selec);
extern int pnp_allocate_device(int driver_sig, struct pnp_dev *dev, int basemask, int irqmask,
			       int dmamask, int memmask);
extern void pnp_release_device(int driver_sig, struct pnp_dev *dev);
extern int pnp_asc2devid(char *name);
extern char *pnp_devid2asc(int id);
extern void pnp_dump_resources(void);
extern int pnp_device_status (struct pnp_dev *dev);
struct pnp_dev *pnp_get_next_device(int driver_sig, struct pnp_dev *prev);
unsigned char pnp_readreg (struct pnp_dev *dev, int reg);
#endif

#include <linux/skbuff.h>

#define LLC_MODULE

typedef struct llc_struct llc;
typedef struct llc_struct *llcptr;

/*
 *	LLC operations object.
 */
 
typedef struct
{
	void (* data_indication_ep) (llcptr llc, struct sk_buff *skb);
	/* unit data returns 0 to keep the data 1 to free it */
	int (* unit_data_indication_ep) (llcptr llc, int ll, char *xid_data);       
	void (* connect_indication_ep) (llcptr llc);
	void (* connect_confirm_ep) (llcptr llc);
	void (* data_connect_indication_ep) (llcptr llc);
	void (* data_connect_confirm_ep) (llcptr llc);
	void (* disconnect_indication_ep) (llcptr llc);
	void (* disconnect_confirm_ep) (llcptr llc);
	void (* reset_confirm_ep) (llcptr llc, char lr);
	void (* reset_indication_ep) (llcptr llc, char lr);
#define LOCAL		0
#define REMOTE		1
	void (* xid_indication_ep) (llcptr llc, int ll, char *xid_data);
	void (* test_indication_ep) (llcptr llc, int ll, char *test_data);
	void (* report_status_ep) (llcptr llc, char status);
#define FRMR_RECEIVED	0
#define FRMR_SENT	1
#define REMOTE_BUSY	2
#define REMOTE_NOT_BUSY	3
} llc_ops;

/*
 *	LLC private data area structure.
 */

struct llc_struct
{ 
	char eye[4];			/* To recognize llc area in dump */
	int retry_count;		/* LLC link state variables */
	unsigned char s_flag;
	unsigned char p_flag;
	unsigned char f_flag;
	unsigned char data_flag;
	unsigned char cause_flag;
	unsigned char vs;		/* Send state variable */
	unsigned char vr;		/* Receive state variable */
	unsigned char remote_busy;
	unsigned char state;		/* Current state of type2 llc procedure */
	int n1;				/* Maximum number of bytes in I pdu 7.8.2 */
	int n2;				/* Naximum number of retransmissions 7.8.2 */
	unsigned char k;		/* Transmit window size 7.8.4, tw in IBM doc*/ 
	unsigned char rw;		/* Receive window size */
	struct 
	{     				
		/*
		 *	FRMR_RSP info field structure: 5.4.2.3.5 p55
		 */

		unsigned char cntl1;
		unsigned char cntl2;
		unsigned char vs;
		unsigned char vr_cr;
		unsigned char xxyz;
	} frmr_info_fld;

	/*
	 *	Timers in 7.8.1 page 78 
	 */

#define P_TIMER         0
#define REJ_TIMER       1
#define ACK_TIMER       2 
#define BUSY_TIMER      3
	unsigned long timer_expire_time[4];	
	unsigned char timer_state[4];	/* The state of each timer */
#define TIMER_IDLE      0
#define TIMER_RUNNING   1
#define TIMER_EXPIRED   2
	unsigned long timer_interval[4]; 
	struct timer_list tl[4];

	/* 
	 *	Client entry points, called by the LLC 
	 */
	 
	llc_ops *ops;
	
	/*
	 *	Mux and Demux variables
	 */
	 
	char * client_data;		/* Pointer to clients context */
	unsigned char local_sap;
	unsigned char remote_sap ;
	char remote_mac[MAX_ADDR_LEN];  /* MAC address of remote session partner */
	int  remote_mac_len;		/* Actual length of mac address */
	int  mac_offset;		/* Source mac offset in skb */ 
	struct device *dev;		/* Device we are attached to */
		     
	unsigned char llc_mode;		/* See doc 7.1 on p70 */
#define MODE_ADM 1
#define MODE_ABM 2

	struct sk_buff *rtq_front;	/* oldest skb in the re-transmit queue */
	struct sk_buff *rtq_back;

	struct sk_buff *atq_front;	/* oldest skb in the await-transmit queue */
	struct sk_buff *atq_back; 
      
	unsigned char xid_count;
	char * nextllc;			/* ptr to next llc struct in proto chain */
};

#define ADD_TO_RTQ(skb) llc_add_to_queue(skb, &lp->rtq_front, &lp->rtq_back) 
#define ADD_TO_ATQ(skb) llc_add_to_queue(skb, &lp->atq_front, &lp->atq_back) 

void 		llc_cancel_timers(llcptr lp);
int		llc_decode_frametype(frameptr fr);
llcptr 		llc_find(void);
int		llc_free_acknowledged_skbs(llcptr lp, unsigned char ack);
void		llc_handle_xid_indication( char *chsp, short int ll, char *xid_data);
void		llc_interpret_pseudo_code(llcptr lp, int pc_label, struct sk_buff *skb, char type);
void		llc_add_to_queue(struct sk_buff *skb, struct sk_buff **f, struct sk_buff **b);
void		llc_process_otype2_frame(llcptr lp, struct sk_buff *skb, char type);
struct sk_buff *llc_pull_from_atq(llcptr lp); 
int 		llc_resend_ipdu(llcptr lp, unsigned char ack_nr, unsigned char type, char p);
void 		llc_sendpdu(llcptr lp, char type, char pf, int data_len, char *pdu_data);
void 		llc_sendipdu(llcptr lp, char type, char pf, struct sk_buff *skb);
void		llc_start_timer(llcptr lp, int t);
void		llc_stop_timer(llcptr lp, int t);
void		llc_timer_expired(llcptr lp, int t);
int		llc_validate_seq_nos(llcptr lp, frameptr fr);

int		llc_data_request(llcptr lp, struct sk_buff *skb);
void		llc_unit_data_request(llcptr lp, int ll, char * data);
void		llc_disconnect_request(llcptr lp);
void		llc_connect_request(llcptr lp);
void		llc_xid_request(llcptr lp, char opt, int data_len, char *pdu_data);
void		llc_test_request(llcptr lp, int data_len, char *pdu_data);

int		register_cl2llc_client(llcptr llc, const char *device, llc_ops *ops, u8 *rmac, u8 ssap, u8 dsap);
void		unregister_cl2llc_client(llcptr lp);

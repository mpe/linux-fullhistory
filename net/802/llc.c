/*
 *	802.2 Class 2 LLC service.
 */
 
 
int llc_rx_adm(struct sock *sk,struct sk_buff *skb, int type, int cmd, int pf, int nr, int ns)
{
	if(type==CMD)
	{
		if(cmd==DISC)
			send_response(sk,DM|pf);
		else if(cmd==SABM)
		{
			if(sk->state!=TCP_LISTEN)
				send_response(sk. DM|pf);
			else
			{
				sk=ll_rx_accept(sk);
				if(sk!=NULL)
				{
					send_response(sk, UA|pf);
					sk->llc.vs=0;
					sk->llc.vr=0;
					sk->llc.p_flag=0;
					sk->llc.remote_busy=0;
					llc_state(sk,LLC_NORMAL);
				}
			}	
		}
		else if(pf)
			send_response(sk, DM|PF);
	}
	return 0;
}

int llc_rx_setup(struct sock *sk, struct sk_buff *skb, int type, int cmd, int pf, int nr, int ns)
{
	if(type==CMD)
	{
		if(cmd==SABM)
		{
			sk->llc.vs=0;
			sk->llc.vr=0;
			send_response(sk, UA|pf);
		}
		if(cmd==DISC)
		{
			send_response(sk, DM|pf);
			llc_error(sk,ECONNRESET);
			llc_state(sk, LLC_ADM);
		}
	}
	else
	{
		if(cmd==UA && pf==sk->llc.p_flag)
		{
			del_timer(&sk->llc.t1);
			sk->llc.vs=0;
			llc_update_p_flag(sk,pf);
			llc_state(sk,LLC_NORMAL);
		}
		if(cmd==DM)
		{
			llc_error(sk, ECONNRESET);
			llc_state(sk, LLC_ADM);
		}
	}
}

int llc_rx_reset(struct sock *sk, struct sk_buff *skb, int type, int cmd, int pf, int nr, int ns)
{
	if(type==CMD)
	{
		if(cmd==SABM)
		{
			sk->llc.vr=0;
			sk->llc.vs=0;
			send_response(sk, UA|pf);
		}
		else if(cmd==DISC)
		{
			if(sk->llc.cause_flag==1)
				llc_shutdown(sk,SHUTDOWN_MASK);
			else
				llc_eror(sk, ECONNREFUSED);
			send_response(sk, DM|pf);
			llc_state(sk, LLC_ADM);
		}
	}
	else
	{
		if(cmd==UA)
		{
			if(sk->llc.p_flag==pf)
			{
				del_timer(&sk->llc.t1);
				sk->llc.vs=0;
				sk->llc.vr=0;
				llc_update_p_flag(sk,pf);
				llc_confirm_reset(sk, sk->llc.cause_flag);
				sk->llc.remote_busy=0;
				llc_state(sk, LLC_NORMAL);
			}
		}
		if(cmd==DM)
		{	/* Should check cause_flag */
			llc_shutdown(sk, SHUTDOWN_MASK);
			llc_state(sk, LLC_ADM);
		}
	}
	return 0;
}

int llc_rx_d_conn(struct sock *sk, struct sk_buff *skb, int type, int cmd, int pf, int nr, int ns)
{
	if(type==CMD)
	{
		if(cmd==SABM)
		{
			llc_error(sk, ECONNRESET);
			llc_state(sk, ADM);
		}
		else if(cmd==DISC)
		{
			send_response(UA|pf);
			llc_state(sk, LLC_D_CONN);
		}
		else if(pf)
			send_response(sk, DM|PF);
	}
	else
	{
		if(cmd==UA && pf==sk->llc.p_flag)
		{
			del_timer(&sk->llc.t1);
			llc_state(sk, ADM);
			llc_confirm_reset(sk, sk->llc.cause_flag);
		}
		if(cmd==DM)
		{
			del_timer(&sk->llc.t1);
			/*if(sk->llc.cause_flag)*/
			llc_shutdown(sk, SHUTDOWN_MASK);	
		}
		
	}
	return 0;
}

int llc_rx_error(struct sock *sk, struct sk_buff *skb, int type, int cmd, int pf, int nr, int ns)
{
	if(type==CMD)
	{
		if(cmd==SABM)
		{
			sk->llc.vs=0;
			sk->llc.vr=0;
			send_response(sk, UA|pf);
			llc_error(sk,ECONNRESET);
			sk->llc.p_flag=0;
			sk->llc.remote_busy=0;
			llc_state(sk, LLC_NORMAL);
		}
		else if(cmd==DISC)
		{
			send_response(sk, UA|pf);
			llc_shutdown(sk, SHUTDOWN_MASK);
			llc_state(sk, LLC_ADM);
		}
		else
			llc_resend_frmr_rsp(sk,pf);
	}
	else
	{
		if(cmd==DM)
		{
			llc_error(sk, ECONNRESET);
			del_timer(&sk->llc.t1);
			llc_state(sk, LLC_ADM);
		}
		if(cmd==FRMR)
		{
			send_command(sk, SABM);
			sk->llc.p_flag=pf;
			llc_start_t1();
			sk->llc.retry_count=0;
			sk->llc.cause_flag=0;
			llc_error(sk, EPROTO);
			llc_state(sk, LLC_RESET);
		}
	}
}


/*
 *	Subroutine for handling the shared cases of the data modes.
 */
 
int llc_rx_nr_shared(struct sock *sk, struct sk_buff *skb, int type, int cmd, int pf, int nr, int ns)
{
	if(type==CMD)
	{
		if(cmd==SABM)
		{
			/*
			 *	Optional reset processing. We decline resets.
			 */
			send_response(sk,DM|pf);
			llc_error(sk, ECONNRESET);
			llc_state(sk, LLC_ADM);
		}
		else if(cmd==DISC)
		{
			send_response(sk,UA|pf);
			llc_state(sk, LLC_ADM);
			llc_shutdown(sk, SHUTDOWN_MASK);
		}
		/*
		 *	We only ever use windows of 7, so there is no illegal NR/NS value
		 *	otherwise we would FRMR here and go to ERROR state
		 */
		else if(cmd==ILLEGAL)
		{
			llc_send_frmr_response(sk, ILLEGAL_TYPE,pf);
			llc_state(sk, LLC_ERROR);
			llc_error(sk, EPROTO);
		}
		else
			/*
			 *	Not covered by general rule
			 */
			return 0;
	}
	else
	{
		/*
		 *	We close on errors
		 */
		if(cmd==FRMR)
		{
			send_command(sk, DM|pf);
			sk->llc.p_flag=pf;
			llc_start_t1(sk);
			llc_error(sk, EPROTO);
			sk->llc.cause_flag=0;
			llc_state(sk, LLC_D_CONN):
		}
		else if(cmd==DM)
		{
			llc_state(sk, LLC_ADM);
			llc_error(sk, ECONNREFUSED);
		}
		/*
		 *	We always use a window of 7 so can't get I resp
		 *	with invalid NS,  or any resp with invalid NR. If
		 *	we add this they do the same as..
		 */
		else if(cmd==UA)
		{
			llc_send_frmr_response(sk, UNEXPECTED_CONTROL, pf);
			llc_state(sk, LLC_ERROR);
			llc_error(sk, EPROTO);
		}
		else if(pf==1 && sk->llc.p_flag==0)
		{
			llc_send_frmr_response(sk, UNEXPECTED_RESPONSE, pf);
			llc_state(sk, LLC_ERROR);
			llc_error(sk, EPROTO);
		}
		else if(cmd==ILLEGAL)
		{
			llc_send_frmr_response(sk, ILLEGAL_TYPE,pf);
			llc_state(sk, LLC_ERROR);
			llc_error(sk, EPROTO);
		}
		else
			/*
			 *	Not covered by general rule
			 */
			return 0
	}
	/*
	 *	Processed.
	 */
	return 1;
}

int llc_rx_normal(struct sock *sk, struct sk_buff *skb, int type, int cmd, int pf, int nr, int ns)
{
	if(llc_rx_nr_shared(sk, skb, type, cmd, pf, nr, ns))
		return 0;
	if(cmd==I)
	{
		if(llc_invalid_ns(sk,ns))
		{
			if((type==RESP && sk->llc.p_flag==pf)||(type==CMD && pf==0 && sk->llc.p_flag==0))
			{
				llc_command(sk, REJ|PF);
				llc_ack_frames(sk,nr);	/* Ack frames and update N(R) */
				sk->llc.p_flag=PF;
				llc_state(sk, LLC_REJECT);
				sk->llc.retry_count=0;
				llc_start_t1(sk);
				sk->llc.remote_busy=0;
			}
			else if((type==CMD && !pf && sk->llc.p_flag==1) || (type==RESP && !pf && sk->llc.p_flag==1))
			{
				if(type==CMD)
					llc_response(sk, REJ);
				else
					llc_command(sk, REJ);
				llc_ack_frames(sk,nr);
				sk->llc.retry_count=0;
				llc_state(sk, LLC_REJECT);
				llc_start_t1(sk);
			}
			else if(pf && type==CMD)
			{
				llc_response(sk, REJ|PF);
				llc_ack_frames(sk,nr);
				sk->llc.retry_count=0;
				llc_start_t1(sk);
			}
		}
		else
		{
			/*
			 *	Valid I frame cases
			 */
			 
			 if(sk->llc.p_flag==pf && !(type==CMD && pf))
			 {
			 	sk->llc.vr=(sk->llc.vr+1)&7;
			 	llc_queue_rr_cmd(sk, PF);
			 	sk->llc.retry_count=0;
			 	llc_start_t1(sk);
			 	sk->llc.p_flag=1;
			 	llc_ack_frames(sk,nr);
			 	sk->llc.remote_busy=0;
			 }
			 else if(sk->ppc.p_flag!=pf)
			 {
			 	sk->llc.vr=(sk->llc.vr+1)&7;
			 	if(type==CMD)
			 		llc_queue_rr_resp(sk, 0);
			 	else
			 		llc_queue_rr_cmd(sk, 0);
			 	if(sk->llc.nr!=nr)
			 	{
			 		llc_ack_frames(sk,nr);
			 		llc_reset_t1(sk);
			 	}
			 }
			 else if(pf)
			 {
			 	sk->llc.vr=(sk->llc.vr+1)&7;
			 	llc_queue_rr_resp(sk,PF);
			 	if(sk->llc.nr!=nr)
			 	{
					llc_ack_frames(sk,nr);
					llc_reset_t1(sk);
			 	}
			 }
			 llc_queue_data(sk,skb);
			 return 1;
		}
	}
	else if(cmd==RR||cmd==RNR)
	{
		if(type==CMD || (type==RESP && (!pf || pf==1 && sk->llc.p_flag==1)))
		{
			llc_update_p_flag(sk,pf);
			if(sk->llc.nr!=nr)
			{
				llc_ack_frames(sk,nr);
				llc_reset_t1(sk);
			}
			if(cmd==RR)
				sk->llc.remote_busy=0;
			else
			{	sk->llc.remote_busy=1;
					if(!llc_t1_running(sk))
					llc_start_t1(sk);
			}
		}
		else if(type==cmd && pf)
		{
			if(cmd==RR)
				llc_queue_rr_resp(sk,PF);
			else
			{
				send_response(sk, RR|PF);
				if(!llc_t1_running(sk))
					llc_start_t1(sk);
			}
			if(sk->llc.nr!=nr)
			{
				llc_ack_frames(sk,nr);
				llc_reset_t1(sk);
			}
			if(cmd==RR)
				sk->llc.remote_busy=0;
			else
				sk->llc.remote_busy=1;
		}
	}
	else if(cmd==REJ)
	{
		
	}
}
			

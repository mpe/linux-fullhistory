/*
 *  $Id: ipconfig.h,v 1.2 1997/10/17 12:41:16 mj Exp $
 *
 *  Copyright (C) 1997 Martin Mares
 *
 *  Automatic IP Layer Configuration
 */

extern __u32 root_server_addr;
extern u8 root_server_path[];
extern u32 ic_myaddr;
extern u32 ic_servaddr;
extern u32 ic_gateway;
extern u32 ic_netmask;
extern int ic_bootp_flag;
extern int ic_rarp_flag;
extern int ic_enable;
extern int ic_host_name_set;
extern int ic_set_manually;

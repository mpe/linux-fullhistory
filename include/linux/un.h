#ifndef _LINUX_UN_H
#define _LINUX_UN_H

#define UNIX_PATH_MAX	108

struct sockaddr_un {
	unsigned short sun_family;	/* AF_UNIX */
	char sun_path[UNIX_PATH_MAX];	/* pathname */
};

struct cmsghdr {
	unsigned int cmsg_len;
	int cmsg_level;
	int cmsg_type;
	unsigned char cmsg_data[0];
};

#endif /* _LINUX_UN_H */

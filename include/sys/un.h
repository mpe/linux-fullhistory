#ifndef _UN_H
#define _UN_H

struct sockaddr_un {
	u_short sun_family;		/* AF_UNIX */
	char sun_path[108];		/* pathname */
};

#endif /* _UN_H */

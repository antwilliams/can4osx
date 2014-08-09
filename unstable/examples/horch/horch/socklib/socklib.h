/*
 * socklib - simple TCP socket interface
 *
 * Copyright (c) 2003-2006 port GmbH Halle
 *------------------------------------------------------------------
 * $Header: /z2/cvsroot/library/co_lib/tools/horch/socklib/socklib.h,v 1.4 2007/02/02 07:31:02 hae Exp $
 *
 *------------------------------------------------------------------
 *
 * modification history
 * --------------------
 * $Log: socklib.h,v $
 * Revision 1.4  2007/02/02 07:31:02  hae
 * updated to version 1.11 of socklib
 * stops hanging of a simple client programmed in C
 *
 * Revision 1.9  2006/11/10 13:25:57  ro
 * Header don't include twice
 *
 * Revision 1.8  2006/09/13 09:35:51  hae
 * Entwicklungsversion
 * blockweises Lesen von socket
 * klarere Augabe von Fehlern unter Windows
 *
 * Revision 1.6  2006/03/03 07:35:58  hae
 * pass socket_options as pointer to so_open2
 *
 * Revision 1.5  2006/02/06 14:46:26  hae
 * structure for socket options added; for now only TCP_NODELAY is available
 *
 * Revision 1.4  2005/02/04 17:18:00  hae
 * declare debug switch for external reference
 *
 * Revision 1.3  2004/10/28 15:15:02  ro
 * so_server_doit() changed - timeout also for Linux
 * special for Linux: timeout == 0 means, no timeout
 * Linux timeout need for CPC driver, e.g. EtherCAN
 *
 * Revision 1.2  2003/10/16 13:07:22  boe
 * defines return values for so_server_doit()
 * add more client information to SOCKET_T
 * move readline to so_readline
 *
 * Revision 1.1  2003/10/16 09:00:51  boe
 * socket library
 *
 *
 *
 *
 *
 *------------------------------------------------------------------
 */

#ifndef __SOCKLIB_H
#define __SOCKLIB_H

#if defined( __WIN32__) || defined(_WIN32)
#    include <winsock.h>
#elif defined(TARGET_IPC)
#    include <sys/socket.h>
# 
#else	/* LINUX */
#    include <sys/socket.h>
#    include <netinet/in.h>
#endif

#define S_DELAY		0
#define S_NDELAY	1

#ifdef S_LIBRARY
# define S_RESET	0
# define S_SET		1
# define S_NAMLEN	64
#endif

#define LISTENQ		5

typedef int CLIENT_FD_T;

typedef struct {
	struct	sockaddr_in sin;
	int	sinlen;
	int	bindflag;
	int	sd;
	fd_set	allset;		/* fd set we're interested in */
	CLIENT_FD_T *client;	/* pointer to client list */
	int	maxClients;	/* max number of clients */
	int	actClients;	/* actual number of clients */
	int     maxfd;		/* maximum socket to wait for input */
	int     nready;		/* state of last read */
	void	*databuf;	/* pointer to data buffer */
} SOCKET_T;

typedef struct {
	int	tcp_no_delay;		/* */
	int	binary;
	int	client_buflen;
} SOCK_OPT_T;

extern int	so_debug;		/* enable/disable debugging */

/* return values for so_server_doit */
#define SRET_SELECT_ERROR	-2
#define SRET_CONN_FAIL		-1
#define SRET_UNKNOWN_REASON	0
#define SRET_CONN_CLOSED	1
#define SRET_CONN_NEW		2
#define SRET_CONN_DATA		3
#define SRET_SELECT_USER	4


/* function prototyping - look at the source for description */
SOCKET_T 	*so_open(void);
SOCKET_T	*so_open2(SOCK_OPT_T *);
int	so_close(SOCKET_T *s);
int	so_server(SOCKET_T *s, int, CLIENT_FD_T *, int);
int	so_client(SOCKET_T *s, char *host, int port);
int	so_readline(int fd, int idx, char *ptr, int maxlen);
int	so_write(int sd, char *buf, int len);
int	so_server_doit(SOCKET_T *s, int *, char *, int *, int);

#endif /* __SOCKLIB_H */

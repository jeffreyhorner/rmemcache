/*
 *  R : A Computer Language for Statistical Data Analysis

 *  Copyright (C) 1996, 1997  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1998-2003   Robert Gentleman, Ross Ihaka and the
 *                            R Development Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

/* <UTF8> chars are handled as a whole */

/* modified to work with rmemcache */

#include "sock.h"
#include "config.h"

#include <string.h>

#ifdef Win32
#include <winsock.h>
#include <io.h>
#define EWOULDBLOCK             WSAEWOULDBLOCK
#define EINPROGRESS             WSAEINPROGRESS
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <time.h>

#ifndef Win32
#include <unistd.h>
#define closesocket(s) close(s)
#define SOCKET int
#endif

static int socket_errno(void)
{
#ifdef Win32
    return(WSAGetLastError());
#else
    return(errno);
#endif
}

static unsigned int timeout = 2;

/* Returns -1 on error
 * 0 on success, meaning the socket is ready for reading/writing
 * 1 on timeout
 */
int mc_SocketWait(int sockfd, int write)
{
	fd_set rfd, wfd;
	struct timeval tv;
	double used = 0.0;

	while(1) {
		int maxfd = 0, howmany;
#ifdef Win32
		tv.tv_sec = 0;
		tv.tv_usec = 2e5;
#else
		tv.tv_sec = timeout;
		tv.tv_usec = 600;
#endif


		FD_ZERO(&rfd);
		FD_ZERO(&wfd);

		if(write) FD_SET(sockfd, &wfd); else FD_SET(sockfd, &rfd);
		maxfd = sockfd;

		/* increment used value _before_ the select in case select
		   modifies tv (as Linux does) */
		used += tv.tv_sec + 1e-6 * tv.tv_usec;

		howmany = select(maxfd+1, &rfd, &wfd, NULL, &tv);

		if (howmany < 0) {
			if (socket_errno() == EINTR) continue;
			return -1;
		}
		if (howmany == 0) {
			if(used >= timeout) return 1;
			continue;
		}

		/* the socket was ready */
		break;
	}
	return 0;
}

void mc_SockTimeout(int delay)
{
	timeout = (unsigned int) delay;
}

int mc_SockConnect(int port, char *host)
{
	SOCKET s;
	fd_set wfd, rfd;
	struct timeval tv;
	int status = 0;
	double used = 0.0;
	struct sockaddr_in server;
	struct hostent *hp;

	s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == -1)  return -1;

#ifdef Win32
	{
		u_long one = 1;

		status = ioctlsocket(s, FIONBIO, &one) == SOCKET_ERROR ? -1 : 0;
	}
#else
#ifdef HAVE_FCNTL_H
	if ((status = fcntl(s, F_GETFL, 0)) != -1) {
		status |= O_NONBLOCK;
		status = fcntl(s, F_SETFL, status);
	}
#endif
	if (status < 0) {
		closesocket(s);
		return(-1);
	}
#endif

	if (! (hp = gethostbyname(host))) return -1;

	memcpy((char *)&server.sin_addr, hp->h_addr_list[0], hp->h_length);
	server.sin_port = htons((short)port);
	server.sin_family = AF_INET;

	if ((connect(s, (struct sockaddr *) &server, sizeof(server)) == -1)) {

		switch (socket_errno()) {
			case EISCONN:
				return s;
				break;
			case EINPROGRESS:
			case EWOULDBLOCK:
				break;
			default:
				closesocket(s);
				return(-1);
		}
	} else {
		return s; /* do we really need to check for writeability? */
	}

	while(1) {
		int maxfd = 0;
#ifdef Win32
		tv.tv_sec = 0;
		tv.tv_usec = 2e5;
#else
		tv.tv_sec = timeout;
		tv.tv_usec = 600;
#endif

		FD_ZERO(&rfd);
		FD_ZERO(&wfd);
		FD_SET(s, &wfd);
		if(maxfd < s) maxfd = s;

		switch(select(maxfd+1, &rfd, &wfd, NULL, &tv))
		{
			case 0:
				/* Time out */
				used += tv.tv_sec + 1e-6 * tv.tv_usec;
				if(used < timeout) continue;
				closesocket(s);
				return(-1);
			case -1:
				/* Ermm.. ?? */
				closesocket(s);
				return(-1);
		}

		if ( FD_ISSET(s, &wfd) ) {
			socklen_t len;
			len = sizeof(status);
			if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&status, &len) < 0){
				/* Solaris error code */
				return (-1);
			}
			if ( status ) {
				closesocket(s);
				errno = status;
				return (-1);
			} else return(s);
		}
	}
	/* not reached */
	return(-1);
}

int mc_SockClose(int sockp)
{
    return closesocket(sockp);
}

/* When blocking:
 *   a return value of 0 either means client closed connection or an
 *   error or a timeout occured.
 *   a negative return value means either an error in select recv.
 *
 * When non-blocking:
 *   a return value of 0 probably means no data to read.
 * 	 a negative return value means error in recv().
 */
int mc_SockRead(int sockp, void *buf, int len, int blocking)
{
    int res;

    if(blocking && mc_SocketWait(sockp, 0) != 0) return 0;
    res = (int) read(sockp, buf, len);
    return (res >= 0) ? res : -socket_errno();
}

/* Blocking write: if return val != len then a send() timed out */
int mc_SockWrite(int sockp, const void *buf, int len)
{
	int res, out = 0;

	do {
		if(mc_SocketWait(sockp, 1) != 0) return out;
		res = (int) send(sockp, buf, len, 0);
		if (res < 0) {
			switch(socket_errno()){
				case EAGAIN:
				case EINTR:
				case ENOBUFS:
					continue;
					break;
				default:
					return -socket_errno();
					break;
			}
		} else {
			{ const char *cbuf = buf; cbuf += res; buf = cbuf; }
			len -= res;
			out += res;
		}
	} while (len > 0);
	return out;
}

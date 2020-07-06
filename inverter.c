/* inverter.c */
/*
 * (C) 2015 Dickon Hood <dickon@fluff.org>
 *
 * Parse the inverter datalogger blobs, and produce something useful
 * from it.
 */

#include <stdio.h>
#include <unistd.h>
#include "inverter.h"
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/time.h>

// parse the 133 byte packet and put results into "inv".
// 
// returns: 0 if successful
//			< 0 if error
int inv_parse_packet(inverter_t *inv, const uint8_t *pkt, int pktLen)
{
	if (pktLen == 0)
		return -1;
	if (pktLen != 103)
		return -1;

	inv->parsetime = time(NULL);

	if (inv->invserial) {
		memcpy(inv->invserial, &pkt[0x11], 0x0e);
		inv->invserial[0x0e] = '\0';
	}

	inv->dlserial = pkt[0x07]<<24 | pkt[0x06]<<16 | pkt[0x05]<<8 | pkt[0x04];

	inv->temp = pkt[0x1f]<<8 | pkt[0x20];
	inv->pv1v = pkt[0x21]<<8 | pkt[0x22];
	inv->pv2v = pkt[0x23]<<8 | pkt[0x24];
	inv->pv3v = pkt[0x25]<<8 | pkt[0x26];
	inv->pv1i = pkt[0x27]<<8 | pkt[0x28];
	inv->pv2i = pkt[0x29]<<8 | pkt[0x2a];
	inv->pv3i = pkt[0x2b]<<8 | pkt[0x2c];
	inv->l1i  = pkt[0x2d]<<8 | pkt[0x2e];
	inv->l2i  = pkt[0x2f]<<8 | pkt[0x30];
	inv->l3i  = pkt[0x31]<<8 | pkt[0x32];
	inv->l1v  = pkt[0x33]<<8 | pkt[0x34];
	inv->l2v  = pkt[0x35]<<8 | pkt[0x36];
	inv->l3v  = pkt[0x37]<<8 | pkt[0x38];
	inv->freq = pkt[0x39]<<8 | pkt[0x3a];
	inv->l1p  = pkt[0x3b]<<8 | pkt[0x3c];
	inv->l2p  = pkt[0x3d]<<8 | pkt[0x3e];
	inv->l3p  = pkt[0x3f]<<8 | pkt[0x40];
	inv->ttot = pkt[0x45]<<8 | pkt[0x46];
	inv->tot  = pkt[0x49]<<8 | pkt[0x4a];

	return 0;
}


// generate 16 byte "magic string" based off 32bit serial number
// 
// returns: size of generated string if successful
//			< 0 if error
int inv_gen_magic_string(uint8_t *magic, int magicLen, uint32_t serial)
{
	uint8_t crc;
	int i;
	
	if (magicLen < 16)
		return -1;

	/* Fixed four bytes: */
	magic[ 0] = 0x68;
	magic[ 1] = 0x02;
	magic[ 2] = 0x40;
	magic[ 3] = 0x30;

	/* BE serial number, twice: */
	magic[ 4] = (serial & 0x000000ff) >>  0;
	magic[ 5] = (serial & 0x0000ff00) >>  8;
	magic[ 6] = (serial & 0x00ff0000) >> 16;
	magic[ 7] = (serial & 0xff000000) >> 24;

	magic[ 8] = (serial & 0x000000ff) >>  0;
	magic[ 9] = (serial & 0x0000ff00) >>  8;
	magic[10] = (serial & 0x00ff0000) >> 16;
	magic[11] = (serial & 0xff000000) >> 24;

	magic[12] = 0x01;
	magic[13] = 0x00;

//	/* No, I don't know either: */
//	magic[14] = (115 + (magic[4] + magic[5] + magic[6] + magic[7]) * 2) & 0xff;
	
	// this must be a crc over the previous bytes from offets 1 to 13
	crc = 0;
	for(i=1; i<=13; i++)
	{
		crc += magic[i];
	}
	magic[14] = crc;

	magic[15] = 0x16;

	return 16;
}



/*
// opens a socket to the inverter
// returns: >=0 if successful (this will be the socket file descriptor)
//			< 0 if error
int inv_open_socket(const char* ip, const char* port, int useUDP, int *lfd)
{
	struct timeval timeout;
	int flag = 1;
	int fd;
	int ret;
	struct addrinfo *a;
	struct addrinfo hints;

	// setup into info to help find the right address for the inverter
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;    	 // Allow IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // Stream socket
	hints.ai_flags = AI_PASSIVE;     // For wildcard IP address
	hints.ai_protocol = 0;           // Any protocol
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	
	if( useUDP )
		hints.ai_socktype = GRAM;
	
	if( ip )
	{
		// connect to server
		ret = getaddrinfo(ip, port, &hints, &a);
		if(ret != 0)
		{
			printf("Error getting address info %s", gai_strerror(ret));
			return -11;
		}
	}
	else
	{
		// listen for client
		ret = getaddrinfo(NULL, port, &hints, &a);
		if( ret != 0 )
		{
			printf("Error getting address info %s", gai_strerror(ret));
			return -12;
		}
	}
	
	fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
	if (fd == -1)
	{
		printf("Error creating socket\n");
		return -13;
	}
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag));
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof(flag));
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &flag, sizeof(flag));
	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_sec = 10;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof(timeout));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *) &timeout, sizeof(timeout));

	if (listening)
	{
		// incoming connection - accept connections from client device
		if (bind(fd, a->ai_addr, a->ai_addrlen) == -1)
		{
			printf("Error binding socket\n");
			return -14
		}
		*lfd = fd;
		
		ret = listen(fd, 16);
		if( ret != 0 )
		{
			printf("Error listening to socket\n");
			return -15;
		}
		
		fd = accept(fd, a->ai_addr, &a->ai_addrlen);
		if(fd < 0 )
		{
			printf("Error accepting socket\n");
			return fd;
		}
	}
	else
	{
		// outgoing connection - connect to server device
		if (connect(fd, a->ai_addr, a->ai_addrlen) == -1)
		{
			printf("Error connecting socket\n");
			return -15;
		}
	}
	
	return fd;
}

*/


// Attempt to connect to an inverter running as a server for our client.
// returns: >=0 if successful (this will be the socket file descriptor)
//			< 0 if error
int inv_connect_to_server(const char* ip, const char* port, int useUDP)
{
	struct timeval timeout;
	int flag = 1;
	int fd;
	int ret;
	struct addrinfo *a;
	struct addrinfo hints;

	// setup into info to help find the right address for the inverter
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;     	 // Allow IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // Stream socket
	hints.ai_flags = AI_PASSIVE;     // For wildcard IP address
	hints.ai_protocol = 0;           // Any protocol
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	
	if( useUDP )
		hints.ai_socktype = SOCK_DGRAM;
	
	if( ip==NULL )
	{
		printf("Error you must specify the IP address to connect to\n");
		return -15;
	}
	if( port==NULL )
	{
		printf("Error you must specify the port to connect to\n");
		return -16;
	}
	
	// get addr of server
	ret = getaddrinfo(ip, port, &hints, &a);
	if(ret != 0)
	{
		printf("Error getting address info %s", gai_strerror(ret));
		return -11;
	}
	
	// create socket
	fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
	if (fd == -1)
	{
		printf("Error creating socket\n");
		return -13;
	}
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag));
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof(flag));
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &flag, sizeof(flag));
	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_sec = 10;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof(timeout));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *) &timeout, sizeof(timeout));
	
	// connect to server
	if (connect(fd, a->ai_addr, a->ai_addrlen) == -1)
	{
		printf("Error connecting socket\n");
		return -15;
	}
	
	return fd;
}

// Start a server listening for inverters as clients wanting to connect to us.
// 
// returns: >=0 if successful (this will be the socket file descriptor)
//			< 0 if error
int inv_start_server(const char* port)
{
	struct timeval timeout;
	int flag = 1;
	int fd;
	int ret;
	struct addrinfo *a;
	struct addrinfo hints;
	
	if( port==NULL )
	{
		printf("Error you must specify the port to listen for clients on\n");
		return -16;
	}
	
	// setup into info to help find the right address for the inverter
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;       // Allow IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // Stream socket
	hints.ai_flags = AI_PASSIVE;     // For wildcard IP address
	hints.ai_protocol = 0;           // Any protocol
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	
	// get addr of server
	ret = getaddrinfo(NULL, port, &hints, &a);
	if(ret != 0)
	{
		printf("Error getting address info %s", gai_strerror(ret));
		return -11;
	}
	
	// create socket
	fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
	if (fd == -1)
	{
		printf("Error creating socket\n");
		return -13;
	}
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag));
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof(flag));
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &flag, sizeof(flag));
	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_sec = 10;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof(timeout));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *) &timeout, sizeof(timeout));
	
	// bind socket for listening for connections on
	if (bind(fd, a->ai_addr, a->ai_addrlen) == -1)
	{
		printf("Error binding socket\n");
		return -14;
	}

	ret = listen(fd, 16);
	if( ret != 0 )
	{
		printf("Error listening to socket\n");
		return -15;
	}
	
	// success
	return fd;
}


// wait for a client to connect	
int inv_listen_for_client(int lfd, struct sockaddr* addr, socklen_t* addrlen)
{
	struct sockaddr sa;
	socklen_t sl;
	int fd;
	
	if( addr==NULL )
		addr = &sa;
	if(addrlen==NULL)
		addrlen = &sl;
	
	fd = accept(lfd, addr, addrlen);
	if(fd < 0 )
	{
		printf("Error accepting socket\n");
		return fd;
	}
	
	return fd;
}


int inv_close(int fd)
{
	close(fd);
	return 0;
}


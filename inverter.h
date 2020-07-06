/* inverter.h */
/*
 * (C) 2015 Dickon Hood <dickon@fluff.org>
 *
 * GPLv2.
 */

#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/tcp.h>

#define INVERTER_PKT_SIZE	103

/* All values are deci-$unit unless otherwise specified. */
typedef struct {
	uint32_t	dlserial;
	char		*invserial;
	uint16_t	temp;
	uint16_t	pv1v;
	uint16_t	pv2v;
	uint16_t	pv3v;
	uint16_t	pv1i;
	uint16_t	pv2i;
	uint16_t	pv3i;
	uint16_t	l1i;
	uint16_t	l2i;
	uint16_t	l3i;
	uint16_t	l1v;
	uint16_t	l2v;
	uint16_t	l3v;
	uint16_t	freq;		/* centiHz */
	uint16_t	l1p;		/* W */
	uint16_t	l2p;		/* W */
	uint16_t	l3p;		/* W */
	uint16_t	ttot;		/* centikilowatthours */
	uint32_t	tot;
	time_t		parsetime;
} inverter_t;


// Attempt to connect to an inverter running as a server for our client.
// returns: >=0 if successful (this will be the socket file descriptor)
//			< 0 if error
int inv_connect_to_server(const char* ip, const char* port, int useUDP);

// Start a server listening for inverters as clients wanting to connect to us.
// 
// returns: >=0 if successful (this will be the socket file descriptor)
//			< 0 if error
int inv_start_server(const char* port);

// wait for a client to connect	
int inv_listen_for_client(int lfd, struct sockaddr* addr, socklen_t* addrlen);

// close socket.
int inv_close(int fd);


int inv_gen_magic_string(uint8_t *magic, int magicLen, uint32_t serial);

int inv_parse_packet(inverter_t *inv, const uint8_t *pkt, int pktLen);

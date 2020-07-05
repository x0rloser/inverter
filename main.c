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
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/time.h>


void usage(char *name)
{
	printf(	"Usage:    %s <options>\n"
			"Options:  -d <delay>     = delay in seconds to wait between polling an inverter for data (default is 60 seconds)\n"
			"          -f <filename>  = filename for writing values to (default is stdout)\n"
			"          -s <serial num>= data logger serial number in decimal or hex format\n"
			"          -i <ip address>= data logger IP address in string format\n"
			"          -p <port>      = data logger port in string format\n"
			"\n"
			"Notes:\n"
			"* This can run as a client or server.\n"
			"* An IP and port is required to connect to an inverter server.\n"
			"* Only a port is required to run as a server for inverter as client.\n"
			"* If no IP address is specified then this will run as a server.\n"
			"* A port must always be specified.\n"
			"* If running as a server then we wait for the inverter to send us data.\n"
			"* If running as a client we will poll the inverter for data constantly.\n"
			, name);
}


int run_as_client(const char* ip, const char* port,
				int useUDP, int delay, uint32_t serial_num, const char* filename)
{
	int ret;
	uint8_t serial_num_magic_str[16] = {0};
	uint8_t pkt_buff[103];
	struct tm* tm;
	char atime[32];
	FILE* fd_o = NULL;
	inverter_t pkt;
	
	// generate magic string from serial number
	if( inv_gen_magic_string(serial_num_magic_str, sizeof(serial_num_magic_str), serial_num) < 0 )
	{
		printf("Error generating magic string from serial number\n");
		return -4;
	}
	
	// append to log file
	if(filename==NULL)
	{
		fd_o = stdout;
	}
	else
	{
		fd_o = fopen(filename, "a");
		if(fd_o==NULL)
		{
			printf("Error opening log file %s.\n", filename);
			return -70;
		}
	}
	
	// connect to server loop
	while(1)
	{
		// connect to server
		// stops if cant connect. may want to wait and retry X times
		int fd_s = inv_connect_to_server(ip, port, useUDP);
		if( fd_s < 0 )
		{
			printf("Error %d connecting to server\n", fd_s);
			return fd_s;
		}
		
		// talk to connected server loop
		while(1)
		{
			// send request to server
			ret = write(fd_s, serial_num_magic_str, sizeof(serial_num_magic_str));
			if(ret==0)
			{
				// disconnected from server?
				break;
			}
			if(ret != sizeof(serial_num_magic_str))
			{
				printf("Error sending to server. Only sent %d of %d bytes\n", ret, (int)sizeof(serial_num_magic_str));
				return -60;
			}
			
			// read response from server
			ret = read(fd_s, pkt_buff, sizeof(pkt_buff));
			if (ret == 0)
			{
				// disconnected from server?
				break;
			}
			if (ret == INVERTER_PKT_SIZE)
			{
				// parse the packet
				if (inv_parse_packet(&pkt, pkt_buff, sizeof(pkt_buff)) != 0)
				{
					// invalid packet?
					printf("Invalid packet?!\n");
				}
				else
				{
					// valid packet
					tm = gmtime(&pkt.parsetime);
					strftime(atime, sizeof(atime), "%Y%m%dT%H%M%SZ", tm);
					
					// print out packet
					fprintf(fd_o,
						"Time:\t\t%s\n"
						"Temperature:\t%5.01f degrees Celsius\n"
						"PV1 voltage:\t%5.01f V\n"
						"PV2 voltage:\t%5.01f V\n"
						"PV3 voltage:\t%5.01f V\n"
						"PV1 current:\t%5.01f A\n"
						"PV2 current:\t%5.01f A\n"
						"PV3 current:\t%5.01f A\n"
						"L1 current:\t%5.01f A\n"
						"L2 current:\t%5.01f A\n"
						"L3 current:\t%5.01f A\n"
						"L1 voltage:\t%5.01f V\n"
						"L2 voltage:\t%5.01f V\n"
						"L3 voltage:\t%5.01f V\n"
						"Frequency:\t%6.02f Hz\n"
						"L1 power:\t%3d W\n"
						"L2 power:\t%3d W\n"
						"L3 power:\t%3d W\n"
						"Today's total:\t%6.02f kWh\n"
						"Total:\t\t%5.01f kWh\n"
						"\n",
						atime,
						((float) pkt.temp)/10,
						((float) pkt.pv1v)/10,
						((float) pkt.pv2v)/10,
						((float) pkt.pv3v)/10,
						((float) pkt.pv1i)/10,
						((float) pkt.pv2i)/10,
						((float) pkt.pv3i)/10,
						((float) pkt.l1i)/10,
						((float) pkt.l2i)/10,
						((float) pkt.l3i)/10,
						((float) pkt.l1v)/10,
						((float) pkt.l2v)/10,
						((float) pkt.l3v)/10,
						((float) pkt.freq)/100,	// Yes...
						pkt.l1p,
						pkt.l2p,
						pkt.l3p,
						((float) pkt.ttot)/100,
						((float) pkt.tot)/10
					);
					fflush(fd_o);

					// It dumps out a 31-byte binary blob saying it's done it.
					// Read and discard.
					ret = read(fd_s, pkt_buff, 31);
				}
			}
			
			// wait a bit before trying again
			sleep(delay);
		}
	}
	
	// never gets here :P
	return 0;
}


int run_as_server(const char* port, int useUDP, const char* filename)
{
	int ret;
	uint8_t pkt_buff[103];
	struct tm* tm;
	char atime[32];
	FILE* fd_o = NULL;
	inverter_t pkt;
	int fd_l;
	
	// append to log file
	if(filename==NULL)
	{
		fd_o = stdout;
	}
	else
	{
		fd_o = fopen(filename, "a");
		if(fd_o==NULL)
		{
			printf("Error opening log file %s.\n", filename);
			return -70;
		}
	}
	
	// start server
	fd_l = inv_start_server(port, useUDP);
	if( fd_l < 0)
	{
		printf("Error starting server\n");
		return -71;
	}
	
	// accept connections from clients - loop
	while(1)
	{
		// wait for a client to connect	
		int fd_c = inv_listen_for_client(fd_l, NULL, NULL);
		if( fd_c < 0 )
		{
			printf("Error %d accepting client connection\n", fd_c);
			return fd_c;
		}
		
		// talk to connected client loop
		while(1)
		{
			// read packet from client
			ret = read(fd_c, pkt_buff, sizeof(pkt_buff));
			if (ret == 0)
			{
				// disconnected from server?
				break;
			}
			if (ret == INVERTER_PKT_SIZE)
			{
				// parse the packet
				if (inv_parse_packet(&pkt, pkt_buff, sizeof(pkt_buff)) != 0)
				{
					// invalid packet?
					printf("Invalid packet?!\n");
				}
				else
				{
					// valid packet
					tm = gmtime(&pkt.parsetime);
					strftime(atime, sizeof(atime), "%Y%m%dT%H%M%SZ", tm);
					
					// print out packet
					fprintf(fd_o,
						"Time:\t\t%s\n"
						"Temperature:\t%5.01f degrees Celsius\n"
						"PV1 voltage:\t%5.01f V\n"
						"PV2 voltage:\t%5.01f V\n"
						"PV3 voltage:\t%5.01f V\n"
						"PV1 current:\t%5.01f A\n"
						"PV2 current:\t%5.01f A\n"
						"PV3 current:\t%5.01f A\n"
						"L1 current:\t%5.01f A\n"
						"L2 current:\t%5.01f A\n"
						"L3 current:\t%5.01f A\n"
						"L1 voltage:\t%5.01f V\n"
						"L2 voltage:\t%5.01f V\n"
						"L3 voltage:\t%5.01f V\n"
						"Frequency:\t%6.02f Hz\n"
						"L1 power:\t%3d W\n"
						"L2 power:\t%3d W\n"
						"L3 power:\t%3d W\n"
						"Today's total:\t%6.02f kWh\n"
						"Total:\t\t%5.01f kWh\n"
						"\n",
						atime,
						((float) pkt.temp)/10,
						((float) pkt.pv1v)/10,
						((float) pkt.pv2v)/10,
						((float) pkt.pv3v)/10,
						((float) pkt.pv1i)/10,
						((float) pkt.pv2i)/10,
						((float) pkt.pv3i)/10,
						((float) pkt.l1i)/10,
						((float) pkt.l2i)/10,
						((float) pkt.l3i)/10,
						((float) pkt.l1v)/10,
						((float) pkt.l2v)/10,
						((float) pkt.l3v)/10,
						((float) pkt.freq)/100,	// Yes...
						pkt.l1p,
						pkt.l2p,
						pkt.l3p,
						((float) pkt.ttot)/100,
						((float) pkt.tot)/10
					);
					fflush(fd_o);
				}
			}
		}
	}
	
	// never gets here :P
	return 0;
}


int main(int argc, char *argv[])
{
	int argi = 0;
	int delay = 60;
	const char* filename = NULL;
	uint32_t serial_num = 0;
	char* ip_str = NULL;
	char* port_str = NULL;
	int use_udp = 0;
	
	// process args pased to program
	if( argc == 1 )
	{
		// no args, (only main filename) so print help
		usage(argv[0]);
		return 0;
	}
	for(argi=1; argi<argc; argc++)
	{
		if( strcasecmp(argv[argi], "-d")==0 )
		{
			if(argi+1 <= argc)
			{
				printf("Param missing for %s\n", argv[argi]);
				return -2;
			}
			delay = strtol(argv[argi+1], 0, 0);
		}
		else if( strcasecmp(argv[argi], "-f")==0 )
		{
			if(argi+1 <= argc)
			{
				printf("Param missing for %s\n", argv[argi]);
				return -2;
			}
			filename = argv[argi+1];
		}
		else if( strcasecmp(argv[argi], "-s")==0 )
		{
			if(argi+1 <= argc)
			{
				printf("Param missing for %s\n", argv[argi]);
				return -2;
			}
			serial_num = strtoul(argv[argi+1], 0, 0);
		}
		else if( strcasecmp(argv[argi], "-i")==0 )
		{
			if(argi+1 <= argc)
			{
				printf("Param missing for %s\n", argv[argi]);
				return -2;
			}
			ip_str = argv[argi+1];
		}
		else if( strcasecmp(argv[argi], "-p")==0 )
		{
			if(argi+1 <= argc)
			{
				printf("Param missing for %s\n", argv[argi]);
				return -2;
			}
			port_str = argv[argi+1];
		}
		else
		{
			printf("Unknown param %s\n", argv[argi]);
			return -3;
		}
	}
	
	
	// port must always be specified
	if( port_str==NULL )
	{
		printf("Error you must specify the port\n");
		return -20;
	}
	
	// talk with inverter
	if( ip_str )
	{
		// connect to server
		return run_as_client(ip_str, port_str, use_udp, delay, serial_num, filename);
	}
	else
	{
		// accept client inverter connections
		return run_as_server(port_str, use_udp, filename);
	}
}


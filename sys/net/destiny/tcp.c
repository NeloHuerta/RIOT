/*
 * tcp.c
 *
 *  Created on: 29.09.2011
 *      Author: Oliver
 */

#include <stdio.h>
#include <thread.h>
#include <string.h>

#include "tcp.h"
#include "in.h"
#include "socket.h"
#include "sys/net/net_help/net_help.h"


void printArrayRange_tcp(uint8_t *udp_header, uint16_t len)
	{
	int i = 0;
	printf("-------------MEMORY-------------\n");
	for (i = 0; i < len; i++)
		{
		printf("%#x ", *(udp_header+i));
		}
	printf("-------------MEMORY-------------\n");
	}

uint16_t tcp_csum(ipv6_hdr_t *ipv6_header, tcp_hdr_t *tcp_header)
	{
    uint16_t sum;
    uint16_t len = ipv6_header->length;

	sum = len + IPPROTO_TCP;
	sum = csum(sum, (uint8_t *)&ipv6_header->srcaddr, 2 * sizeof(ipv6_addr_t));
	sum = csum(sum, (uint8_t*)tcp_header, len);
    return (sum == 0) ? 0xffff : HTONS(sum);
	}

void tcp_packet_handler (void)
	{
	msg_t m_recv_ip, m_send_ip, m_recv_tcp, m_send_tcp;
	struct ipv6_hdr_t *ipv6_header;
	struct tcp_hdr_t *tcp_header;
	uint8_t *payload;
	socket_internal_t *tcp_socket = NULL;
	uint16_t chksum;

	while (1)
		{
		msg_receive(&m_recv_ip);
		ipv6_header = ((struct ipv6_hdr_t*)&buffer);
		tcp_header = ((struct tcp_hdr_t*)(&buffer[IPV6_HDR_LEN]));
		payload = &buffer[IPV6_HDR_LEN+TCP_HDR_LEN];

		chksum = tcp_csum(ipv6_header, tcp_header);
		printf("Checksum is %x!\n", chksum);

		tcp_socket = get_tcp_socket(ipv6_header, tcp_header);

		if ((chksum == 0xffff) && (tcp_socket != NULL))
			{
			// Remove reserved bits from tcp flags field
			uint8_t tcp_flags = tcp_header->reserved_flags & 0xC0;

			//TODO: URG Flag and PSH flag are currently being ignored
			switch (tcp_flags)
				{
				case TCP_ACK:
					{
					// only ACK Bit set
					// TODO: WRONG, check in queued sockets of dst socket!
					if (tcp_socket->in_socket.local_tcp_status.state == SYN_RCVD)
						{
						m_send_tcp.content.ptr = (char*)buffer;
						m_send_tcp.content.value = IPV6_HDR_LEN + ipv6_header->length;
						msg_send_receive(&m_recv_tcp, &m_send_tcp, tcp_socket->pid);
						}
					break;
					}
				case TCP_RST:
					{
					// only RST Bit set
					break;
					}
				case TCP_SYN:
					{
					// only SYN Bit set, look for matching, listening socket and request new queued socket

					if (tcp_socket->in_socket.local_tcp_status.state == LISTEN)
						{
						socket_t *new_socket = new_tcp_queued_socket(ipv6_header, tcp_header, tcp_socket);
						if (new_socket != NULL)
							{
							// notify socket function accept(..) that a new connection request has arrived
							// No need to wait for an answer because the server accept() function isnt reading from anything other than the queued sockets
							msg_send(&m_send_tcp, tcp_socket->pid, 0);
							}
						else
							{
							printf("Dropped TCP SYN Message because an error occured while requesting a new queued socket!\n");
							}
						}
					else
						{
						printf("Droppec TCP SYN Message because socket was not in state LISTEN!");
						}
					break;
					}
				case TCP_SYN_ACK:
					{
					// only SYN and ACK Bit set, complete three way handshake when socket in state SYN_SENT
					if (tcp_socket->in_socket.local_tcp_status.state == SYN_SENT)
						{
						m_send_tcp.content.ptr = (char*)buffer;
						m_send_tcp.content.value = IPV6_HDR_LEN + ipv6_header->length;
						msg_send_receive(&m_recv_tcp, &m_send_tcp, tcp_socket->pid);
						}
					else
						{
						printf("Socket not in state SYN_SENT, dropping SYN-ACK-packet!");
						}
					break;
					}
				case TCP_FIN:
					{
					// only FIN Bit set
					break;
					}
				case TCP_FIN_ACK:
					{
					// only FIN and ACK Bit set
					break;
					}
				default:
					{
					// TODO: any other case
					}
				}
			}
		else
			{
			printf("Wrong checksum (%x) or no corresponding socket found!\n", chksum);
			}

		tcp_socket = NULL;
		msg_reply(&m_recv_ip, &m_send_ip);
		}
	}


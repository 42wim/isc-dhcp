/* dhcrelay.c

   DHCP/BOOTP Relay Agent. */

/*
 * Copyright (c) 1997 The Internet Software Consortium.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#ifndef lint
static char copyright[] =
"$Id: dhcrelay.c,v 1.2 1997/02/22 12:28:26 mellon Exp $ Copyright (c) 1997 The Internet Software Consortium.  All rights reserved.\n";
#endif /* not lint */

#include "dhcpd.h"

static void usage PROTO ((void));

TIME cur_time;
TIME default_lease_time = 43200; /* 12 hours... */
TIME max_lease_time = 86400; /* 24 hours... */
struct tree_cache *global_options [256];

int log_perror = 1;

#ifdef USE_FALLBACK
struct interface_info fallback_interface;
#endif

u_int16_t local_port;
u_int16_t remote_port;
int log_priority;

struct server_list {
	struct server_list *next;
	struct sockaddr_in to;
} *servers;

int main (argc, argv, envp)
	int argc;
	char **argv, **envp;
{
	int i;
	struct servent *ent;
	struct server_list *sp;
	int no_daemon;

#ifdef SYSLOG_4_2
	openlog ("dhcrelay", LOG_NDELAY);
	log_priority = LOG_DAEMON;
#else
	openlog ("dhcrelay", LOG_NDELAY, LOG_DAEMON);
#endif

#if !(defined (DEBUG) || defined (SYSLOG_4_2))
	setlogmask (LOG_UPTO (LOG_INFO));
#endif	

	for (i = 1; i < argc; i++) {
		if (!strcmp (argv [i], "-p")) {
			if (++i == argc)
				usage ();
			local_port = htons (atoi (argv [i]));
			debug ("binding to user-specified port %d",
			       ntohs (local_port));
		} else if (!strcmp (argv [i], "-d")) {
			no_daemon = 1;
 		} else if (!strcmp (argv [i], "-i")) {
 		    struct interface_info *tmp =
 			((struct interface_info *)
 			 dmalloc (sizeof *tmp, "specified_interface"));
 		    if (!tmp)
 			error ("Insufficient memory to %s %s",
 			       "record interface", argv [i]);
		    if (++i == argc) {
			    usage ();
		    }
 		    memset (tmp, 0, sizeof *tmp);
 		    strcpy (tmp -> name, argv [i]);
 		    tmp -> next = interfaces;
 		    tmp -> flags = INTERFACE_REQUESTED;
 		    interfaces = tmp;
 		} else if (argv [i][0] == '-') {
 		    usage ();
 		} else {
			struct hostent *he;
			he = gethostbyname (argv [i]);
			if (!he) {
				herror (argv [i]);
			} else {
				sp = (struct server_list *)malloc (sizeof *sp);
				if (!sp)
					error ("no memory for server.\n");
				sp -> next = servers;
				servers = sp;
				memcpy (&sp -> to.sin_addr,
					he -> h_addr_list [0], he -> h_length);
			}
 		}
	}
	/* Default to the DHCP/BOOTP port. */
	if (!local_port) {
		ent = getservbyname ("dhcps", "udp");
		if (!ent)
			local_port = htons (67);
		else
			local_port = ent -> s_port;
		endservent ();
	}
	remote_port = htons (ntohs (local_port) + 1);
  
	/* We need at least one server. */
	if (!sp) {
		usage ();
	}

	/* Set up the server sockaddrs. */
	for (sp = servers; sp; sp = sp -> next) {
		sp -> to.sin_port = remote_port;
		sp -> to.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
		sp -> to.sin_len = sizeof sp -> to;
#endif
	}

	/* Get the current time... */
	GET_TIME (&cur_time);

	/* Discover all the network interfaces. */
	discover_interfaces (DISCOVER_RELAY);

	/* Start dispatching packets and timeouts... */
	dispatch (0);
	/*NOTREACHED*/
	return 0;
}

void relay (ip, packet, length)
	struct interface_info *ip;
	struct dhcp_packet *packet;
	int length;
{
	struct server_list *sp;
	struct sockaddr_in to;
	struct interface_info *out;
	struct hardware hto;

	/* If it's a bootreply, forward it to the client. */
	if (packet -> op == BOOTREPLY) {
#ifndef USE_FALLBACK
		if (!packet -> flags & htons (BOOTP_BROADCAST)) {
			to.sin_addr = packet -> raw -> ciaddr;
			to.sin_port = remote_port;
		} else
#endif
		{
			to.sin_addr.s_addr = htonl (INADDR_BROADCAST);
			to.sin_port = remote_port;
		}
		to.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
		to.sin_len = sizeof to;
#endif

		memcpy (hto.haddr, packet -> chaddr,
			(packet -> hlen > sizeof hto.haddr
			 ? sizeof hto.haddr
			 : packet -> hlen));
		hto.htype = packet -> htype;

		/* Find the interface that corresponds to the giaddr
		   in the packet. */
		for (out = interfaces; out; out = out -> next) {
			if (!memcmp (&out -> primary_address,
				     &packet -> giaddr,
				     sizeof packet -> giaddr))
				break;
		}
		if (!out) {
			warn ("packet to bogus giaddr %s.\n",
			      inet_ntoa (packet -> giaddr));
			return;
		}

		if (send_packet (out,
				 (struct packet *)0,
				 packet, length, out -> primary_address,
				 &to, &hto) < 0)
			debug ("sendpkt: %m");
		else
			debug ("forwarded BOOTREPLY for %s to %s",
			       print_hw_addr (packet -> htype, packet -> hlen,
					      packet -> chaddr),
			       inet_ntoa (to.sin_addr));

		return;
	}

	/* If giaddr is set on a BOOTREQUEST, ignore it - it's already
	   been gatewayed. */
	if (packet -> giaddr.s_addr) {
		note ("ignoring BOOTREQUEST with giaddr of %s\n",
		      inet_ntoa (packet -> giaddr));
		return;
	}

	/* Set the giaddr so the server can figure out what net it's
	   from and so that we can later forward the response to the
	   correct net. */
	packet -> giaddr = ip -> primary_address;

	/* Otherwise, it's a BOOTREQUEST, so forward it to all the
	   servers. */
	for (sp = servers; sp; sp = sp -> next) {
		if (
#ifdef USE_FALLBACK
		    send_fallback (&fallback_interface,
				   (struct packet *)0,
				   packet, length, ip -> primary_address,
				   &sp -> to, (struct hardware *)0)
#else
		    send_packet (interfaces,
				 (struct packet *)0,
				 packet, length, ip -> primary_address,
				 &sp -> to, (struct hardware *)0)
#endif
		    < 0) {
			debug ("send_packet: %m");
		} else {
			debug ("forwarded BOOTREQUEST for %s to %s",
			       print_hw_addr (packet -> htype, packet -> hlen,
					      packet -> chaddr),
			       inet_ntoa (sp -> to.sin_addr));
		}
	}
				 
}

static void usage ()
{
	error ("Usage: dhcrelay [-c] [-p <port>] [server1 [... serverN]]");
}

void cleanup ()
{
}

int write_lease (lease)
	struct lease *lease;
{
	return 1;
}

int commit_leases ()
{
	return 1;
}

void bootp (packet)
	struct packet *packet;
{
}

void dhcp (packet)
	struct packet *packet;
{
}

/* dispatch.c

   Network input dispatcher... */

/*
 * Copyright (c) 1995, 1996 The Internet Software Consortium.
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
"$Id: dispatch.c,v 1.32 1997/02/22 10:55:40 mellon Exp $ Copyright (c) 1995, 1996 The Internet Software Consortium.  All rights reserved.\n";
#endif /* not lint */

#include "dhcpd.h"
#include <sys/ioctl.h>

struct interface_info *interfaces, *dummy_interfaces;
struct timeout *timeouts;
static struct timeout *free_timeouts;
static int interfaces_invalidated;

static void got_one PROTO ((struct interface_info *, int));

/* Use the SIOCGIFCONF ioctl to get a list of all the attached interfaces.
   For each interface that's of type INET and not the loopback interface,
   register that interface with the network I/O software, figure out what
   subnet it's on, and add it to the list of interfaces. */

void discover_interfaces (state)
	int state;
{
	struct interface_info *tmp;
	struct interface_info *last;
	char buf [8192];
	struct ifconf ic;
	struct ifreq ifr;
	int i;
	int sock;
	int address_count = 0;
	struct subnet *subnet;
	struct shared_network *share;
	struct sockaddr_in foo;
	int ir;
#ifdef ALIAS_NAMES_PERMUTED
	char *s;
#endif
#ifdef USE_FALLBACK
	static struct shared_network fallback_network;
#endif

	/* Create an unbound datagram socket to do the SIOCGIFADDR ioctl on. */
	if ((sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		error ("Can't create addrlist socket");

	/* Get the interface configuration information... */
	ic.ifc_len = sizeof buf;
	ic.ifc_ifcu.ifcu_buf = (caddr_t)buf;
	i = ioctl(sock, SIOCGIFCONF, &ic);

	if (i < 0)
		error ("ioctl: SIOCGIFCONF: %m");

	/* If we already have a list of interfaces, and we're running as
	   a DHCP server, the interfaces were requested. */
	if (interfaces && state == DISCOVER_SERVER)
		ir = 0;
	else
		ir = INTERFACE_REQUESTED;

	/* Cycle through the list of interfaces looking for IP addresses.
	   Go through twice; once to count the number if addresses, and a
	   second time to copy them into an array of addresses. */
	for (i = 0; i < ic.ifc_len;) {
		struct ifreq *ifp = (struct ifreq *)((caddr_t)ic.ifc_req + i);
#ifdef HAVE_SA_LEN
		i += (sizeof ifp -> ifr_name) + ifp -> ifr_addr.sa_len;
#else
		i += sizeof *ifp;
#endif

#ifdef ALIAS_NAMES_PERMUTED
		if ((s = strrchr (ifp -> ifr_name, ':'))) {
			*s = 0;
		}
#endif


		/* See if this is the sort of interface we want to
		   deal with. */
		strcpy (ifr.ifr_name, ifp -> ifr_name);
		if (ioctl (sock, SIOCGIFFLAGS, &ifr) < 0)
			error ("Can't get interface flags for %s: %m",
			       ifr.ifr_name);

		/* Skip loopback, point-to-point and down interfaces,
		   except don't skip down interfaces if we're trying to
		   get a list of configurable interfaces. */
		if ((ifr.ifr_flags & IFF_LOOPBACK) ||
		    (ifr.ifr_flags & IFF_POINTOPOINT) ||
		    (!(ifr.ifr_flags & IFF_UP) &&
		     state != DISCOVER_UNCONFIGURED))
			continue;
		
		/* See if we've seen an interface that matches this one. */
		for (tmp = interfaces; tmp; tmp = tmp -> next)
			if (!strcmp (tmp -> name, ifp -> ifr_name))
				break;

		/* If there isn't already an interface by this name,
		   allocate one. */
		if (!tmp) {
			tmp = ((struct interface_info *)
			       dmalloc (sizeof *tmp, "get_interface_list"));
			if (!tmp)
				error ("Insufficient memory to %s %s",
				       "record interface", ifp -> ifr_name);
			strcpy (tmp -> name, ifp -> ifr_name);
			tmp -> next = interfaces;
			tmp -> flags = ir;
			interfaces = tmp;
		}

		/* If we have the capability, extract link information
		   and record it in a linked list. */
#ifdef AF_LINK
		if (ifp -> ifr_addr.sa_family == AF_LINK) {
			struct sockaddr_dl *foo = ((struct sockaddr_dl *)
						   (&ifp -> ifr_addr));
			tmp -> hw_address.hlen = foo -> sdl_alen;
			tmp -> hw_address.htype = HTYPE_ETHER; /* XXX */
			memcpy (tmp -> hw_address.haddr,
				LLADDR (foo), foo -> sdl_alen);
		} else
#endif /* AF_LINK */

		if (ifp -> ifr_addr.sa_family == AF_INET) {
			struct iaddr addr;

#if defined (SIOCGIFHWADDR) && !defined (AF_LINK)
			struct ifreq ifr;
			struct sockaddr sa;
			int b, sk;
			
			/* Read the hardware address from this interface. */
			ifr = *ifp;
			if (ioctl (sock, SIOCGIFHWADDR, &ifr) < 0)
				error ("Can't get hardware address for %s: %m",
				       ifr.ifr_name);

			sa = *(struct sockaddr *)&ifr.ifr_hwaddr;
					
			switch (sa.sa_family) {
			      case ARPHRD_LOOPBACK:
				/* ignore loopback interface */
				break;

			      case ARPHRD_ETHER:
				tmp -> hw_address.hlen = 6;
				tmp -> hw_address.htype = ARPHRD_ETHER;
				memcpy (tmp -> hw_address.haddr,
					sa.sa_data, 6);
				break;

			      case ARPHRD_METRICOM:
				tmp -> hw_address.hlen = 6;
				tmp -> hw_address.htype = ARPHRD_METRICOM;
				memcpy (tmp -> hw_address.haddr,
					sa.sa_data, 6);
				break;

			      default:
				error ("%s: unknown hardware address type %d",
				       ifr.ifr_name, sa.sa_family);
			}
#endif /* defined (SIOCGIFHWADDR) && !defined (AF_LINK) */

			/* Get a pointer to the address... */
			memcpy (&foo, &ifp -> ifr_addr,
				sizeof ifp -> ifr_addr);

			/* We don't want the loopback interface. */
			if (foo.sin_addr.s_addr == htonl (INADDR_LOOPBACK))
				continue;


			/* If this is the first real IP address we've
			   found, keep a pointer to ifreq structure in
			   which we found it. */
			if (!tmp -> ifp) {
				struct ifreq *tif;
#ifdef HAVE_SA_LEN
				int len = ((sizeof ifp -> ifr_name) +
					   ifp -> ifr_addr.sa_len);
#else
				int len = sizeof *ifp;
#endif
				tif = (struct ifreq *)malloc (len);
				if (!tif)
					error ("no space to remember ifp.");
				memcpy (tif, ifp, len);
				tmp -> ifp = ifp;
				tmp -> primary_address = foo.sin_addr;
			}

			/* Grab the address... */
			addr.len = 4;
			memcpy (addr.iabuf, &foo.sin_addr.s_addr,
				addr.len);

			/* If there's a registered subnet for this address,
			   connect it together... */
			if ((subnet = find_subnet (addr))) {
				/* If this interface has multiple aliases
				   on the same subnet, ignore all but the
				   first we encounter. */
				if (!subnet -> interface) {
					subnet -> interface = tmp;
					subnet -> interface_address = addr;
				} else if (subnet -> interface != tmp) {
					warn ("Multiple %s %s: %s %s", 
					      "interfaces match the",
					      "same subnet",
					      subnet -> interface -> name,
					      tmp -> name);
				}
				share = subnet -> shared_network;
				if (tmp -> shared_network &&
				    tmp -> shared_network != share) {
					warn ("Interface %s matches %s",
					      tmp -> name,
					      "multiple shared networks");
				} else {
					tmp -> shared_network = share;
				}

				if (!share -> interface) {
					share -> interface = tmp;
				} else if (share -> interface != tmp) {
					warn ("Multiple %s %s: %s %s", 
					      "interfaces match the",
					      "same shared network",
					      share -> interface -> name,
					      tmp -> name);
				}
			}
		}
	}

	/* If we're just trying to get a list of interfaces that we might
	   be able to configure, we can quit now. */
	if (state == DISCOVER_UNCONFIGURED)
		return;

	/* Weed out the interfaces that did not have IP addresses. */
	last = (struct interface_info *)0;
	for (tmp = interfaces; tmp; tmp = tmp -> next) {
		if (!tmp -> ifp || !(tmp -> flags & INTERFACE_REQUESTED)) {
			if ((tmp -> flags & INTERFACE_REQUESTED) != ir)
				error ("%s: not found", tmp -> name);
			if (!last)
				interfaces = interfaces -> next;
			else
				last -> next = tmp -> next;

			/* Remember the interface in case we need to know
			   about it later. */
			tmp -> next = dummy_interfaces;
			dummy_interfaces = tmp;
			continue;
		}
		last = tmp;

		memcpy (&foo, &tmp -> ifp -> ifr_addr,
			sizeof tmp -> ifp -> ifr_addr);

		/* We must have a subnet declaration for each interface. */
		if (!tmp -> shared_network && (state == DISCOVER_SERVER))
			error ("No subnet declaration for %s (%s).",
			       tmp -> name, inet_ntoa (foo.sin_addr));

		/* Find subnets that don't have valid interface
		   addresses... */
		for (subnet = (tmp -> shared_network
			       ? tmp -> shared_network -> subnets
			       : (struct subnet *)0);
		     subnet; subnet = subnet -> next_sibling) {
			if (!subnet -> interface_address.len) {
				/* Set the interface address for this subnet
				   to the first address we found. */
				subnet -> interface_address.len = 4;
				memcpy (subnet -> interface_address.iabuf,
					&foo.sin_addr.s_addr, 4);
			}
		}

		/* Register the interface... */
		if_register_receive (tmp);
		if_register_send (tmp);
	}

	close (sock);

#ifdef USE_FALLBACK
	strcpy (fallback_interface.name, "fallback");	
	fallback_interface.shared_network = &fallback_network;
	fallback_network.name = "fallback-net";
	if_register_fallback (&fallback_interface);
#endif
}

void reinitialize_interfaces ()
{
	struct interface_info *ip;

	for (ip = interfaces; ip; ip = ip -> next) {
		if_reinitialize_receive (ip);
		if_reinitialize_send (ip);
	}

#ifdef USE_FALLBACK
	if_reinitialize_fallback (&fallback_interface);
#endif

	interfaces_invalidated = 1;
}

#ifdef USE_POLL
/* Wait for packets to come in using poll().  Anyway, when a packet
   comes in, call receive_packet to receive the packet and possibly
   strip hardware addressing information from it, and then call
   do_packet to try to do something with it. 

   As you can see by comparing this with the code that uses select(),
   below, this is gratuitously complex.  Quelle surprise, eh?  This is
   SysV we're talking about, after all, and even in the 90's, it
   wouldn't do for SysV to make networking *easy*, would it?  Rant,
   rant... */

void dispatch (parse)
	int parse;
{
	struct interface_info *l;
	int nfds = 0;
	struct pollfd *fds;
	int count;
	int i;
	int to_msec;

	nfds = 0;
	for (l = interfaces; l; l = l -> next) {
		++nfds;
	}
#ifdef USE_FALLBACK
	++nfds;
#endif
	fds = (struct pollfd *)malloc ((nfds) * sizeof (struct pollfd));
	if (!fds)
		error ("Can't allocate poll structures.");

	do {
		/* Call any expired timeouts, and then if there's
		   still a timeout registered, time out the select
		   call then. */
	      another:
		if (timeouts) {
			struct timeout *t;
			if (timeouts -> when <= cur_time) {
				t = timeouts;
				timeouts = timeouts -> next;
				(*(t -> func)) (t -> interface);
				t -> next = free_timeouts;
				free_timeouts = t;
				goto another;
			}
			/* Figure timeout in milliseconds, and check for
			   potential overflow.   We assume that integers
			   are 32 bits, which is harmless if they're 64
			   bits - we'll just get extra timeouts in that
			   case.    Lease times would have to be quite
			   long in order for a 32-bit integer to overflow,
			   anyway. */
			to_msec = timeouts -> when - cur_time;
			if (to_msec > 2147483)
				to_msec = 2147483;
			to_msec *= 1000;
		} else
			to_msec = -1;

		/* Set up the descriptors to be polled. */
		i = 0;
		for (l = interfaces; l; l = l -> next) {
			fds [i].fd = l -> rfdesc;
			fds [i].events = POLLIN;
			fds [i].revents = 0;
			++i;
		}

#ifdef USE_FALLBACK
		fds [i].fd = fallback_interface.wfdesc;
		fds [i].events = POLLIN;
		fds [i].revents = 0;
		++i;
#endif

		/* Wait for a packet or a timeout... XXX */
		count = poll (fds, nfds, to_msec);

		/* Get the current time... */
		GET_TIME (&cur_time);

		/* Not likely to be transitory... */
		if (count < 0)
			error ("poll: %m");

		i = 0;
		for (l = interfaces; l; l = l -> next) {
			if ((fds [i].revents & POLLIN)) {
				fds [i].revents = 0;
				got_one (l, parse);
				if (interfaces_invalidated)
					break;
			}
			++i;
		}
#ifdef USE_FALLBACK
		if ((fds [i].revents & POLLIN) && !interfaces_invalidated)
			fallback_discard (&fallback_interface);
#endif
		interfaces_invalidated = 0;
	} while (1);
}
#else
/* Wait for packets to come in using select().   When one does, call
   receive_packet to receive the packet and possibly strip hardware
   addressing information from it, and then call do_packet to try to
   do something with it. */

void dispatch (parse)
	int parse;
{
	fd_set r, w, x;
	struct interface_info *l;
	int max = 0;
	int count;
	struct timeval tv, *tvp;

	FD_ZERO (&w);
	FD_ZERO (&x);

	do {
		/* Call any expired timeouts, and then if there's
		   still a timeout registered, time out the select
		   call then. */
	      another:
		if (timeouts) {
			struct timeout *t;
			if (timeouts -> when <= cur_time) {
				t = timeouts;
				timeouts = timeouts -> next;
				(*(t -> func)) (t -> interface);
				t -> next = free_timeouts;
				free_timeouts = t;
				goto another;
			}
			tv.tv_sec = timeouts -> when - cur_time;
			tv.tv_usec = 0;
			tvp = &tv;
		} else
			tvp = (struct timeval *)0;

		/* Set up the read mask. */
		FD_ZERO (&r);

		for (l = interfaces; l; l = l -> next) {
			FD_SET (l -> rfdesc, &r);
			FD_SET (l -> rfdesc, &x);
			if (l -> rfdesc > max)
				max = l -> rfdesc;
		}
#ifdef USE_FALLBACK
		FD_SET (fallback_interface.wfdesc, &r);
		if (fallback_interface.wfdesc > max)
				max = fallback_interface.wfdesc;
#endif

		/* Wait for a packet or a timeout... XXX */
		count = select (max + 1, &r, &w, &x, tvp);

		/* Get the current time... */
		GET_TIME (&cur_time);

		/* Not likely to be transitory... */
		if (count < 0)
			error ("select: %m");

		for (l = interfaces; l; l = l -> next) {
			if (!FD_ISSET (l -> rfdesc, &r))
				continue;
			got_one (l, parse);
			if (interfaces_invalidated)
				break;
		}
#ifdef USE_FALLBACK
		if (FD_ISSET (fallback_interface.wfdesc, &r) &&
		    !interfaces_invalidated)
			fallback_discard (&fallback_interface);
#endif
		interfaces_invalidated = 1;
	} while (1);
}
#endif /* USE_POLL */

static void got_one (l, parse)
	struct interface_info *l;
	int parse;
{
	struct sockaddr_in from;
	struct hardware hfrom;
	struct iaddr ifrom;
	int result;
	static unsigned char packbuf [4095]; /* Packet input buffer.
						Must be as large as largest
						possible MTU. */

	if ((result = receive_packet (l, packbuf, sizeof packbuf,
				      &from, &hfrom)) < 0) {
		warn ("receive_packet failed on %s: %m", l -> name);
		return;
	}
	if (result == 0)
		return;

	if (parse) {
		ifrom.len = 4;
		memcpy (ifrom.iabuf, &from.sin_addr, ifrom.len);
	
		do_packet (l, packbuf, result,
			   from.sin_port, ifrom, &hfrom);
	} else
		relay (l, (struct dhcp_packet *)packbuf, result);
}

void do_packet (interface, packbuf, len, from_port, from, hfrom)
	struct interface_info *interface;
	unsigned char *packbuf;
	int len;
	unsigned short from_port;
	struct iaddr from;
	struct hardware *hfrom;
{
	struct packet tp;
	struct dhcp_packet tdp;

	memcpy (&tdp, packbuf, len);
	memset (&tp, 0, sizeof tp);
	tp.raw = &tdp;
	tp.packet_length = len;
	tp.client_port = from_port;
	tp.client_addr = from;
	tp.interface = interface;
	tp.haddr = hfrom;
	
	parse_options (&tp);
	if (tp.options_valid &&
	    tp.options [DHO_DHCP_MESSAGE_TYPE].data)
		tp.packet_type =
			tp.options [DHO_DHCP_MESSAGE_TYPE].data [0];
	if (tp.packet_type)
		dhcp (&tp);
	else if (tdp.op == BOOTREQUEST)
		bootp (&tp);
}

int locate_network (packet)
	struct packet *packet;
{
	struct iaddr ia;

	/* If this came through a gateway, find the corresponding subnet... */
	if (packet -> raw -> giaddr.s_addr) {
		struct subnet *subnet;
		ia.len = 4;
		memcpy (ia.iabuf, &packet -> raw -> giaddr, 4);
		subnet = find_subnet (ia);
		if (subnet)
			packet -> shared_network = subnet -> shared_network;
		else
			packet -> shared_network = (struct shared_network *)0;
	} else {
		packet -> shared_network =
			packet -> interface -> shared_network;
	}
	if (packet -> shared_network)
		return 1;
	return 0;
}

void add_timeout (when, where, what)
	TIME when;
	void (*where) (struct interface_info *);
	struct interface_info *what;
{
	struct timeout *t, *q;

	/* See if this timeout supersedes an existing timeout. */
	t = (struct timeout *)0;
	for (q = timeouts; q; q = q -> next) {
		if (q -> func == where && q -> interface == what) {
			if (t)
				t -> next = q -> next;
			else
				timeouts = q -> next;
			break;
		}
		t = q;
	}

	/* If we didn't supersede a timeout, allocate a timeout
	   structure now. */
	if (!q) {
		if (free_timeouts) {
			q = free_timeouts;
			free_timeouts = q -> next;
			q -> func = where;
			q -> interface = what;
		} else {
			q = (struct timeout *)malloc (sizeof (struct timeout));
			if (!q)
				error ("Can't allocate timeout structure!");
			q -> func = where;
			q -> interface = what;
		}
	}

	q -> when = when;

	/* Now sort this timeout into the timeout list. */

	/* Beginning of list? */
	if (!timeouts || timeouts -> when > q -> when) {
		q -> next = timeouts;
		timeouts = q;
		return;
	}

	/* Middle of list? */
	for (t = timeouts; t -> next; t = t -> next) {
		if (t -> next -> when > q -> when) {
			q -> next = t -> next;
			t -> next = q;
			return;
		}
	}

	/* End of list. */
	t -> next = q;
	q -> next = (struct timeout *)0;
}

void cancel_timeout (where, what)
	void (*where) (struct interface_info *);
	struct interface_info *what;
{
	struct timeout *t, *q;

	/* Look for this timeout on the list, and unlink it if we find it. */
	t = (struct timeout *)0;
	for (q = timeouts; q; q = q -> next) {
		if (q -> func == where && q -> interface == what) {
			if (t)
				t -> next = q -> next;
			else
				timeouts = q -> next;
			break;
		}
		t = q;
	}

	/* If we found the timeout, put it on the free list. */
	if (q) {
		q -> next = free_timeouts;
		free_timeouts = q;
	}
}

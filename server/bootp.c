/* bootp.c

   BOOTP Protocol support. */

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
"@(#) Copyright (c) 1995, 1996 The Internet Software Consortium.  All rights reserved.\n";
#endif /* not lint */

#include "dhcpd.h"

void bootp (packet)
	struct packet *packet;
{
	int result;
	struct host_decl *hp;
	struct host_decl *host = (struct host_decl *)0;
	struct packet outgoing;
	struct dhcp_packet raw;
	struct sockaddr_in to;
	struct hardware hto;
	struct tree_cache *options [256];
	struct subnet *subnet;
	struct lease *lease;
	struct iaddr ip_address;
	int i;

	note ("BOOTREQUEST from %s via %s",
	      print_hw_addr (packet -> raw -> htype,
			     packet -> raw -> hlen,
			     packet -> raw -> chaddr),
	      packet -> raw -> giaddr.s_addr
	      ? inet_ntoa (packet -> raw -> giaddr)
	      : packet -> interface -> name);



	locate_network (packet);

	hp = find_hosts_by_haddr (packet -> raw -> htype,
				  packet -> raw -> chaddr,
				  packet -> raw -> hlen);

	lease = find_lease (packet, packet -> shared_network);

	/* Find an IP address in the host_decl that matches the
	   specified network. */
	if (hp && packet -> shared_network)
		subnet = find_host_for_network (&hp, &ip_address,
						packet -> shared_network);
	else
		subnet = (struct subnet *)0;

	if (!subnet) {
		/* We didn't find an applicable host declaration.
		   Just in case we may be able to dynamically assign
		   an address, see if there's a host declaration
		   that doesn't have an ip address associated with it. */
		if (hp) {
			for (; hp; hp = hp -> n_ipaddr) {
				if (!hp -> fixed_addr) {
					host = hp;
				}
			}
		}

		/* If the packet is from a host we don't know and there
		   are no dynamic bootp addresses on the network it came
		   in on, drop it on the floor. */
		if (!(packet -> shared_network &&
		      packet -> shared_network -> dynamic_bootp)) {
		      lose:
			note ("No applicable record for BOOTP host %s",
			      print_hw_addr (packet -> raw -> htype,
					     packet -> raw -> hlen,
					     packet -> raw -> chaddr));
			return;
		}

		/* If a lease has already been assigned to this client
		   and it's still okay to use dynamic bootp on
		   that lease, reassign it. */
		if (lease) {
			/* If this lease can be used for dynamic bootp,
			   do so. */
			if ((lease -> flags & DYNAMIC_BOOTP_OK)) {

				/* If it's not a DYNAMIC_BOOTP lease,
				   release it before reassigning it
				   so that we don't get a lease
				   conflict. */
				if (!(lease -> flags & BOOTP_LEASE))
					release_lease (lease);

				lease -> host = host;
				ack_lease (packet, lease, 0, 0);
				return;
			}

			 /* If dynamic BOOTP is no longer allowed for
			   this lease, set it free. */
			release_lease (lease);
		}

		/* At this point, if we don't know the network from which
		   the packet came, lose it. */
		if (!packet -> shared_network)
			goto lose;

		/* If there are dynamic bootp addresses that might be
		   available, try to snag one. */
		for (lease =
		     packet -> shared_network -> last_lease;
		     lease && lease -> ends <= cur_time;
		     lease = lease -> prev) {
			if ((lease -> flags & DYNAMIC_BOOTP_OK)) {
				lease -> host = host;
				ack_lease (packet, lease, 0, 0);
				return;
			}
		}
		goto lose;
	}

	/* If we don't have a fixed address for it, drop it. */
	if (!subnet) {
		note ("No fixed address for BOOTP host %s (%s)",
		      print_hw_addr (packet -> raw -> htype,
				     packet -> raw -> hlen,
				     packet -> raw -> chaddr),
		      hp -> name);
		return;
	}

	/* Set up the outgoing packet... */
	memset (&outgoing, 0, sizeof outgoing);
	memset (&raw, 0, sizeof raw);
	outgoing.raw = &raw;

	/* Come up with a list of options that we want to send to this
	   client.   Start with the per-subnet options, and then override
	   those with client-specific options. */

	memcpy (options, subnet -> options, sizeof options);

	for (i = 0; i < 256; i++) {
		if (hp -> options [i])
			options [i] = hp -> options [i];
	}

	/* Pack the options into the buffer.   Unlike DHCP, we can't
	   pack options into the filename and server name buffers. */

	cons_options (packet, &outgoing, options, 0);
	

	/* Take the fields that we care about... */
	raw.op = BOOTREPLY;
	raw.htype = packet -> raw -> htype;
	raw.hlen = packet -> raw -> hlen;
	memcpy (raw.chaddr, packet -> raw -> chaddr, raw.hlen);
	memset (&raw.chaddr [raw.hlen], 0,
		(sizeof raw.chaddr) - raw.hlen);
	raw.hops = packet -> raw -> hops;
	raw.xid = packet -> raw -> xid;
	raw.secs = packet -> raw -> secs;
	raw.flags = 0;
	raw.ciaddr = packet -> raw -> ciaddr;
	memcpy (&raw.yiaddr, ip_address.iabuf, sizeof raw.yiaddr);

	if (subnet -> interface_address.len)
		memcpy (&raw.siaddr, subnet -> interface_address.iabuf, 4);
	else
		memcpy (&raw.siaddr, server_identifier.iabuf, 4);

	raw.giaddr = packet -> raw -> giaddr;
	if (hp -> server_name) {
		strncpy (raw.sname, hp -> server_name,
			 (sizeof raw.sname) - 1);
		raw.sname [(sizeof raw.sname) - 1] = 0;
	}
	if (hp -> filename) {
		strncpy (raw.file, hp -> filename,
			 (sizeof raw.file) - 1);
		raw.file [(sizeof raw.file) - 1] = 0;
	}

	/* Set up the hardware destination address... */
	hto.htype = packet -> raw -> htype;
	hto.hlen = packet -> raw -> hlen;
	memcpy (hto.haddr, packet -> raw -> chaddr, hto.hlen);

	/* Report what we're doing... */
	note ("BOOTREPLY for %s to %s via %s",
	      inet_ntoa (raw.yiaddr),
	      print_hw_addr (packet -> raw -> htype,
			     packet -> raw -> hlen,
			     packet -> raw -> chaddr),
	      packet -> raw -> giaddr.s_addr
	      ? inet_ntoa (packet -> raw -> giaddr)
	      : packet -> interface -> name);

	/* Set up the parts of the address that are in common. */
	to.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
	to.sin_len = sizeof to;
#endif
	memset (to.sin_zero, 0, sizeof to.sin_zero);

	/* If this was gatewayed, send it back to the gateway... */
	if (raw.giaddr.s_addr) {
		to.sin_addr = raw.giaddr;
		to.sin_port = server_port;

#ifdef USE_FALLBACK
		result = send_fallback (&fallback_interface,
					(struct packet *)0,
					&raw, outgoing.packet_length,
					raw.siaddr, &to, &hto);
		if (result < 0)
			warn ("send_fallback: %m");
		return;
#endif
	/* Otherwise, broadcast it on the local network. */
	} else {
		to.sin_addr.s_addr = INADDR_BROADCAST;
		to.sin_port = htons (ntohs (server_port) + 1); /* XXX */
	}

	errno = 0;
	result = send_packet (packet -> interface,
			      packet, &raw, outgoing.packet_length,
			      raw.siaddr, &to, &hto);
	if (result < 0)
		warn ("send_packet: %m");
}

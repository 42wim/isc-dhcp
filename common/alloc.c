/* alloc.c

   Memory allocation... */

/*
 * Copyright (c) 1996-1999 Internet Software Consortium.
 * Use is subject to license terms which appear in the file named
 * ISC-LICENSE that should have accompanied this file when you
 * received it.   If a file named ISC-LICENSE did not accompany this
 * file, or you are not sure the one you have is correct, you may
 * obtain an applicable copy of the license at:
 *
 *             http://www.isc.org/isc-license-1.0.html. 
 *
 * This file is part of the ISC DHCP distribution.   The documentation
 * associated with this file is listed in the file DOCUMENTATION,
 * included in the top-level directory of this release.
 *
 * Support and other services are available for ISC products - see
 * http://www.isc.org for more information.
 */

#ifndef lint
static char copyright[] =
"$Id: alloc.c,v 1.38 2000/01/26 14:55:33 mellon Exp $ Copyright (c) 1995, 1996, 1998 The Internet Software Consortium.  All rights reserved.\n";
#endif /* not lint */

#include "dhcpd.h"
#include <omapip/omapip_p.h>

struct dhcp_packet *dhcp_free_list;
struct packet *packet_free_list;

struct dhcp_packet *new_dhcp_packet (file, line)
	const char *file;
	int line;
{
	struct dhcp_packet *rval;
	rval = (struct dhcp_packet *)dmalloc (sizeof (struct dhcp_packet),
					      file, line);
	return rval;
}

struct hash_table *new_hash_table (count, file, line)
	int count;
	const char *file;
	int line;
{
	struct hash_table *rval = dmalloc (sizeof (struct hash_table)
					   - (DEFAULT_HASH_SIZE
					      * sizeof (struct hash_bucket *))
					   + (count
					      * sizeof (struct hash_bucket *)),
					   file, line);
	rval -> hash_count = count;
	return rval;
}

struct hash_bucket *new_hash_bucket (file, line)
	const char *file;
	int line;
{
	struct hash_bucket *rval = dmalloc (sizeof (struct hash_bucket),
					    file, line);
	return rval;
}

struct lease *new_leases (n, file, line)
	unsigned n;
	const char *file;
	int line;
{
	struct lease *rval = dmalloc (n * sizeof (struct lease), file, line);
	return rval;
}

struct lease *new_lease (file, line)
	const char *file;
	int line;
{
	struct lease *rval = dmalloc (sizeof (struct lease), file, line);
	return rval;
}

struct subnet *new_subnet (file, line)
	const char *file;
	int line;
{
	struct subnet *rval = dmalloc (sizeof (struct subnet), file, line);
	return rval;
}

struct class *new_class (file, line)
	const char *file;
	int line;
{
	struct class *rval = dmalloc (sizeof (struct class), file, line);
	return rval;
}

struct shared_network *new_shared_network (file, line)
	const char *file;
	int line;
{
	struct shared_network *rval =
		dmalloc (sizeof (struct shared_network), file, line);
	return rval;
}

struct group *new_group (file, line)
	const char *file;
	int line;
{
	struct group *rval =
		dmalloc (sizeof (struct group), file, line);
	if (rval)
		memset (rval, 0, sizeof *rval);
	return rval;
}

struct protocol *new_protocol (file, line)
	const char *file;
	int line;
{
	struct protocol *rval = dmalloc (sizeof (struct protocol), file, line);
	return rval;
}

struct lease_state *free_lease_states;

struct lease_state *new_lease_state (file, line)
	const char *file;
	int line;
{
	struct lease_state *rval;

	if (free_lease_states) {
		rval = free_lease_states;
		free_lease_states =
			(struct lease_state *)(free_lease_states -> next);
 		dmalloc_reuse (rval, file, line, 0);
	} else {
		rval = dmalloc (sizeof (struct lease_state), file, line);
		if (!rval)
			return rval;
	}
	memset (rval, 0, sizeof *rval);
	if (!option_state_allocate (&rval -> options, file, line)) {
		free_lease_state (rval, file, line);
		return (struct lease_state *)0;
	}
	return rval;
}

struct domain_search_list *new_domain_search_list (file, line)
	const char *file;
	int line;
{
	struct domain_search_list *rval =
		dmalloc (sizeof (struct domain_search_list), file, line);
	return rval;
}

struct name_server *new_name_server (file, line)
	const char *file;
	int line;
{
	struct name_server *rval =
		dmalloc (sizeof (struct name_server), file, line);
	return rval;
}

void free_name_server (ptr, file, line)
	struct name_server *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

struct option *new_option (file, line)
	const char *file;
	int line;
{
	struct option *rval =
		dmalloc (sizeof (struct option), file, line);
	if (rval)
		memset (rval, 0, sizeof *rval);
	return rval;
}

void free_option (ptr, file, line)
	struct option *ptr;
	const char *file;
	int line;
{
/* XXX have to put all options on heap before this is possible. */
#if 0
	if (ptr -> name)
		dfree ((VOIDPTR)option -> name, file, line);
	dfree ((VOIDPTR)ptr, file, line);
#endif
}

struct universe *new_universe (file, line)
	const char *file;
	int line;
{
	struct universe *rval =
		dmalloc (sizeof (struct universe), file, line);
	return rval;
}

void free_universe (ptr, file, line)
	struct universe *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

void free_domain_search_list (ptr, file, line)
	struct domain_search_list *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

void free_lease_state (ptr, file, line)
	struct lease_state *ptr;
	const char *file;
	int line;
{
	if (ptr -> options)
		option_state_dereference (&ptr -> options, file, line);
	if (ptr -> packet)
		packet_dereference (&ptr -> packet, file, line);
	data_string_forget (&ptr -> parameter_request_list, file, line);
	data_string_forget (&ptr -> filename, file, line);
	data_string_forget (&ptr -> server_name, file, line);
	ptr -> next = free_lease_states;
	free_lease_states = ptr;
	dmalloc_reuse (free_lease_states, (char *)0, 0, 0);
}

void free_protocol (ptr, file, line)
	struct protocol *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

void free_group (ptr, file, line)
	struct group *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

void free_shared_network (ptr, file, line)
	struct shared_network *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

void free_class (ptr, file, line)
	struct class *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

void free_subnet (ptr, file, line)
	struct subnet *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

void free_lease (ptr, file, line)
	struct lease *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

void free_hash_bucket (ptr, file, line)
	struct hash_bucket *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

void free_hash_table (ptr, file, line)
	struct hash_table *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

void free_dhcp_packet (ptr, file, line)
	struct dhcp_packet *ptr;
	const char *file;
	int line;
{
	dfree ((VOIDPTR)ptr, file, line);
}

struct client_lease *new_client_lease (file, line)
	const char *file;
	int line;
{
	return (struct client_lease *)dmalloc (sizeof (struct client_lease),
					       file, line);
}

void free_client_lease (lease, file, line)
	struct client_lease *lease;
	const char *file;
	int line;
{
	dfree (lease, file, line);
}

struct pool *new_pool (file, line)
	const char *file;
	int line;
{
	struct pool *pool = ((struct pool *)
			     dmalloc (sizeof (struct pool), file, line));
	if (!pool)
		return pool;
	memset (pool, 0, sizeof *pool);
	return pool;
}

void free_pool (pool, file, line)
	struct pool *pool;
	const char *file;
	int line;
{
	dfree (pool, file, line);
}

struct auth_key *new_auth_key (len, file, line)
	unsigned len;
	const char *file;
	int line;
{
	struct auth_key *peer;
	unsigned size = len - 1 + sizeof (struct auth_key);

	peer = (struct auth_key *)dmalloc (size, file, line);
	if (!peer)
		return peer;
	memset (peer, 0, size);
	return peer;
}

void free_auth_key (peer, file, line)
	struct auth_key *peer;
	const char *file;
	int line;
{
	dfree (peer, file, line);
}

struct permit *new_permit (file, line)
	const char *file;
	int line;
{
	struct permit *permit = ((struct permit *)
				 dmalloc (sizeof (struct permit), file, line));
	if (!permit)
		return permit;
	memset (permit, 0, sizeof *permit);
	return permit;
}

void free_permit (permit, file, line)
	struct permit *permit;
	const char *file;
	int line;
{
	dfree (permit, file, line);
}

pair free_pairs;

pair new_pair (file, line)
	const char *file;
	int line;
{
	pair foo;

	if (free_pairs) {
		foo = free_pairs;
		free_pairs = foo -> cdr;
		memset (foo, 0, sizeof *foo);
		dmalloc_reuse (foo, file, line, 0);
		return foo;
	}

	foo = dmalloc (sizeof *foo, file, line);
	if (!foo)
		return foo;
	memset (foo, 0, sizeof *foo);
	return foo;
}

void free_pair (foo, file, line)
	pair foo;
	const char *file;
	int line;
{
	foo -> cdr = free_pairs;
	free_pairs = foo;
	dmalloc_reuse (free_pairs, (char *)0, 0, 0);
}

struct expression *free_expressions;

int expression_allocate (cptr, file, line)
	struct expression **cptr;
	const char *file;
	int line;
{
	struct expression *rval;

	if (free_expressions) {
		rval = free_expressions;
		free_expressions = rval -> data.not;
	} else {
		rval = dmalloc (sizeof (struct expression), file, line);
		if (!rval)
			return 0;
	}
	memset (rval, 0, sizeof *rval);
	return expression_reference (cptr, rval, file, line);
}

int expression_reference (ptr, src, file, line)
	struct expression **ptr;
	struct expression *src;
	const char *file;
	int line;
{
	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	if (*ptr) {
		log_error ("%s(%d): non-null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		*ptr = (struct expression *)0;
#endif
	}
	*ptr = src;
	src -> refcnt++;
	rc_register (file, line, src, src -> refcnt);
	dmalloc_reuse (src, file, line, 1);
	return 1;
}

void free_expression (expr, file, line)
	struct expression *expr;
	const char *file;
	int line;
{
	expr -> data.not = free_expressions;
	free_expressions = expr;
	dmalloc_reuse (free_expressions, (char *)0, 0, 0);
}

				
struct option_cache *free_option_caches;

int option_cache_allocate (cptr, file, line)
	struct option_cache **cptr;
	const char *file;
	int line;
{
	struct option_cache *rval;

	if (free_option_caches) {
		rval = free_option_caches;
		free_option_caches =
			(struct option_cache *)(rval -> expression);
		dmalloc_reuse (rval, file, line, 0);
	} else {
		rval = dmalloc (sizeof (struct option_cache), file, line);
		if (!rval)
			return 0;
	}
	memset (rval, 0, sizeof *rval);
	return option_cache_reference (cptr, rval, file, line);
}

int option_cache_reference (ptr, src, file, line)
	struct option_cache **ptr;
	struct option_cache *src;
	const char *file;
	int line;
{
	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	if (*ptr) {
		log_error ("%s(%d): non-null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		*ptr = (struct option_cache *)0;
#endif
	}
	*ptr = src;
	src -> refcnt++;
	rc_register (file, line, src, src -> refcnt);
	dmalloc_reuse (src, file, line, 1);
	return 1;
}

int buffer_allocate (ptr, len, file, line)
	struct buffer **ptr;
	unsigned len;
	const char *file;
	int line;
{
	struct buffer *bp;

	bp = dmalloc (len + sizeof *bp, file, line);
	if (!bp)
		return 0;
	memset (bp, 0, sizeof *bp);
	bp -> refcnt = 0;
	return buffer_reference (ptr, bp, file, line);
}

int buffer_reference (ptr, bp, file, line)
	struct buffer **ptr;
	struct buffer *bp;
	const char *file;
	int line;
{
	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	if (*ptr) {
		log_error ("%s(%d): non-null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		*ptr = (struct buffer *)0;
#endif
	}
	*ptr = bp;
	bp -> refcnt++;
	rc_register (file, line, bp, bp -> refcnt);
	dmalloc_reuse (bp, file, line, 1);
	return 1;
}

int buffer_dereference (ptr, file, line)
	struct buffer **ptr;
	const char *file;
	int line;
{
	struct buffer *bp;

	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}

	if (!*ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}

	(*ptr) -> refcnt--;
	rc_register (file, line, *ptr, (*ptr) -> refcnt);
	if (!(*ptr) -> refcnt)
		dfree ((*ptr), file, line);
	if ((*ptr) -> refcnt < 0) {
		log_error ("%s(%d): negative refcnt!", file, line);
#if defined (DEBUG_RC_HISTORY)
		dump_rc_history ();
#endif
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	*ptr = (struct buffer *)0;
	return 1;
}

int dns_host_entry_allocate (ptr, hostname, file, line)
	struct dns_host_entry **ptr;
	const char *hostname;
	const char *file;
	int line;
{
	struct dns_host_entry *bp;

	bp = dmalloc (strlen (hostname) + sizeof *bp, file, line);
	if (!bp)
		return 0;
	memset (bp, 0, sizeof *bp);
	bp -> refcnt = 0;
	strcpy (bp -> hostname, hostname);
	return dns_host_entry_reference (ptr, bp, file, line);
}

int dns_host_entry_reference (ptr, bp, file, line)
	struct dns_host_entry **ptr;
	struct dns_host_entry *bp;
	const char *file;
	int line;
{
	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	if (*ptr) {
		log_error ("%s(%d): non-null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		*ptr = (struct dns_host_entry *)0;
#endif
	}
	*ptr = bp;
	bp -> refcnt++;
	rc_register (file, line, bp, bp -> refcnt);
	dmalloc_reuse (bp, file, line, 1);
	return 1;
}

int dns_host_entry_dereference (ptr, file, line)
	struct dns_host_entry **ptr;
	const char *file;
	int line;
{
	struct dns_host_entry *bp;

	if (!ptr || !*ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}

	(*ptr) -> refcnt--;
	rc_register (file, line, bp, bp -> refcnt);
	if (!(*ptr) -> refcnt)
		dfree ((*ptr), file, line);
	if ((*ptr) -> refcnt < 0) {
		log_error ("%s(%d): negative refcnt!", file, line);
#if defined (DEBUG_RC_HISTORY)
		dump_rc_history ();
#endif
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	*ptr = (struct dns_host_entry *)0;
	return 1;
}

int option_state_allocate (ptr, file, line)
	struct option_state **ptr;
	const char *file;
	int line;
{
	unsigned size;

	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	if (*ptr) {
		log_error ("%s(%d): non-null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		*ptr = (struct option_state *)0;
#endif
	}

	size = sizeof **ptr + (universe_count - 1) * sizeof (VOIDPTR);
	*ptr = dmalloc (size, file, line);
	if (*ptr) {
		memset (*ptr, 0, size);
		(*ptr) -> universe_count = universe_count;
		(*ptr) -> refcnt = 1;
		rc_register (file, line, *ptr, (*ptr) -> refcnt);
		return 1;
	}
	return 0;
}

int option_state_reference (ptr, bp, file, line)
	struct option_state **ptr;
	struct option_state *bp;
	const char *file;
	int line;
{
	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	if (*ptr) {
		log_error ("%s(%d): non-null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		*ptr = (struct option_state *)0;
#endif
	}
	*ptr = bp;
	bp -> refcnt++;
	rc_register (file, line, bp, bp -> refcnt);
	dmalloc_reuse (bp, file, line, 1);
	return 1;
}

int option_state_dereference (ptr, file, line)
	struct option_state **ptr;
	const char *file;
	int line;
{
	int i;
	struct option_state *options;

	if (!ptr || !*ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}

	options = *ptr;
	*ptr = (struct option_state *)0;
	--options -> refcnt;
	rc_register (file, line, options, options -> refcnt);
	if (options -> refcnt > 0)
		return 1;

	if (options -> refcnt < 0) {
		log_error ("%s(%d): negative refcnt!", file, line);
#if defined (DEBUG_RC_HISTORY)
		dump_rc_history ();
#endif
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}

	/* Loop through the per-universe state. */
	for (i = 0; i < options -> universe_count; i++)
		if (options -> universes [i] &&
		    universes [i] -> option_state_dereference)
			((*(universes [i] -> option_state_dereference))
			 (universes [i], options));

	dfree (options, file, line);
	return 1;
}

int executable_statement_allocate (ptr, file, line)
	struct executable_statement **ptr;
	const char *file;
	int line;
{
	struct executable_statement *bp;

	bp = dmalloc (sizeof *bp, file, line);
	if (!bp)
		return 0;
	memset (bp, 0, sizeof *bp);
	return executable_statement_reference (ptr, bp, file, line);
}

int executable_statement_reference (ptr, bp, file, line)
	struct executable_statement **ptr;
	struct executable_statement *bp;
	const char *file;
	int line;
{
	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	if (*ptr) {
		log_error ("%s(%d): non-null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		*ptr = (struct executable_statement *)0;
#endif
	}
	*ptr = bp;
	bp -> refcnt++;
	rc_register (file, line, bp, bp -> refcnt);
	dmalloc_reuse (bp, file, line, 1);
	return 1;
}

static struct packet *free_packets;

int packet_allocate (ptr, file, line)
	struct packet **ptr;
	const char *file;
	int line;
{
	int size;

	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	if (*ptr) {
		log_error ("%s(%d): non-null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		*ptr = (struct packet *)0;
#endif
	}

	*ptr = dmalloc (sizeof **ptr, file, line);
	if (*ptr) {
		memset (*ptr, 0, sizeof **ptr);
		(*ptr) -> refcnt = 1;
		return 1;
	}
	return 0;
}

int packet_reference (ptr, bp, file, line)
	struct packet **ptr;
	struct packet *bp;
	const char *file;
	int line;
{
	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}
	if (*ptr) {
		log_error ("%s(%d): non-null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		*ptr = (struct packet *)0;
#endif
	}
	*ptr = bp;
	bp -> refcnt++;
	rc_register (file, line, bp, bp -> refcnt);
	dmalloc_reuse (bp, file, line, 1);
	return 1;
}

int packet_dereference (ptr, file, line)
	struct packet **ptr;
	const char *file;
	int line;
{
	int i;
	struct packet *packet;

	if (!ptr || !*ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}

	packet = *ptr;
	*ptr = (struct packet *)0;
	--packet -> refcnt;
	rc_register (file, line, packet, packet -> refcnt);
	if (packet -> refcnt > 0)
		return 1;

	if (packet -> refcnt < 0) {
		log_error ("%s(%d): negative refcnt!", file, line);
#if defined (DEBUG_RC_HISTORY)
		dump_rc_history ();
#endif
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}

	if (packet -> options)
		option_state_dereference (&packet -> options, file, line);
	packet -> raw = (struct dhcp_packet *)free_packets;
	free_packets = packet;
	dmalloc_reuse (free_packets, (char *)0, 0, 0);
	return 1;
}

int binding_scope_allocate (ptr, file, line)
	struct binding_scope **ptr;
	const char *file;
	int line;
{
	struct binding_scope *bp;

	if (!ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}

	if (*ptr) {
		log_error ("%s(%d): non-null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}

	bp = dmalloc (sizeof *bp, file, line);
	if (!bp)
		return 0;
	memset (bp, 0, sizeof *bp);
	*ptr = bp;
	return 1;
}

/* Make a copy of the data in data_string, upping the buffer reference
   count if there's a buffer. */

void data_string_copy (dest, src, file, line)
	struct data_string *dest;
	struct data_string *src;
	const char *file;
	int line;
{
	if (src -> buffer)
		buffer_reference (&dest -> buffer, src -> buffer, file, line);
	dest -> data = src -> data;
	dest -> terminated = src -> terminated;
	dest -> len = src -> len;
}

/* Release the reference count to a data string's buffer (if any) and
   zero out the other information, yielding the null data string. */

void data_string_forget (data, file, line)
	struct data_string *data;
	const char *file;
	int line;
{
	if (data -> buffer)
		buffer_dereference (&data -> buffer, file, line);
	memset (data, 0, sizeof *data);
}

/* Make a copy of the data in data_string, upping the buffer reference
   count if there's a buffer. */

void data_string_truncate (dp, len)
	struct data_string *dp;
	int len;
{
	if (len < dp -> len) {
		dp -> terminated = 0;
		dp -> len = len;
	}
}

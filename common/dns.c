/* dns.c

   Domain Name Service subroutines. */

/*
 * Copyright (c) 2000 Internet Software Consortium.
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
 * by Ted Lemon in cooperation with Nominum, Inc.
 * To learn more about the Internet Software Consortium, see
 * ``http://www.isc.org/''.  To learn more about Nominum, Inc., see
 * ``http://www.nominum.com''.
 */

#ifndef lint
static char copyright[] =
"$Id: dns.c,v 1.20 2000/04/06 22:41:47 mellon Exp $ Copyright (c) 2000 The Internet Software Consortium.  All rights reserved.\n";
#endif /* not lint */

#include "dhcpd.h"
#include "arpa/nameser.h"

/* This file is kind of a crutch for the BIND 8 nsupdate code, which has
 * itself been cruelly hacked from its original state.   What this code
 * does is twofold: first, it maintains a database of zone cuts that can
 * be used to figure out which server should be contacted to update any
 * given domain name.   Secondly, it maintains a set of named TSIG keys,
 * and associates those keys with zones.   When an update is requested for
 * a particular zone, the key associated with that zone is used for the
 * update.
 *
 * The way this works is that you define the domain name to which an
 * SOA corresponds, and the addresses of some primaries for that domain name:
 *
 *	zone FOO.COM {
 *	  primary 10.0.17.1;
 *	  secondary 10.0.22.1, 10.0.23.1;
 *	  key "FOO.COM Key";
 * 	}
 *
 * If an update is requested for GAZANGA.TOPANGA.FOO.COM, then the name
 * server looks in its database for a zone record for "GAZANGA.TOPANGA.FOO.COM",
 * doesn't find it, looks for one for "TOPANGA.FOO.COM", doesn't find *that*,
 * looks for "FOO.COM", finds it. So it
 * attempts the update to the primary for FOO.COM.   If that times out, it
 * tries the secondaries.   You can list multiple primaries if you have some
 * kind of magic name server that supports that.   You shouldn't list
 * secondaries that don't know how to forward updates (e.g., BIND 8 doesn't
 * support update forwarding, AFAIK).   If no TSIG key is listed, the update
 * is attempted without TSIG.
 *
 * The DHCP server tries to find an existing zone for any given name by
 * trying to look up a local zone structure for each domain containing
 * that name, all the way up to '.'.   If it finds one cached, it tries
 * to use that one to do the update.   That's why it tries to update
 * "FOO.COM" above, even though theoretically it should try GAZANGA...
 * and TOPANGA... first.
 *
 * If the update fails with a predefined or cached zone (we'll get to
 * those in a second), then it tries to find a more specific zone.   This
 * is done by looking first for an SOA for GAZANGA.TOPANGA.FOO.COM.   Then
 * an SOA for TOPANGA.FOO.COM is sought.   If during this search a predefined
 * or cached zone is found, the update fails - there's something wrong
 * somewhere.
 *
 * If a more specific zone _is_ found, that zone is cached for the length of
 * its TTL in the same database as that described above.   TSIG updates are
 * never done for cached zones - if you want TSIG updates you _must_
 * write a zone definition linking the key to the zone.   In cases where you
 * know for sure what the key is but do not want to hardcode the IP addresses
 * of the primary or secondaries, a zone declaration can be made that doesn't
 * include any primary or secondary declarations.   When the DHCP server
 * encounters this while hunting up a matching zone for a name, it looks up
 * the SOA, fills in the IP addresses, and uses that record for the update.
 * If the SOA lookup returns NXRRSET, a warning is printed and the zone is
 * discarded, TSIG key and all.   The search for the zone then continues as if
 * the zone record hadn't been found.   Zones without IP addresses don't
 * match when initially hunting for a predefined or cached zone to update.
 *
 * When an update is attempted and no predefined or cached zone is found
 * that matches any enclosing domain of the domain being updated, the DHCP
 * server goes through the same process that is done when the update to a
 * predefined or cached zone fails - starting with the most specific domain
 * name (GAZANGA.TOPANGA.FOO.COM) and moving to the least specific (the root),
 * it tries to look up an SOA record.   When it finds one, it creates a cached
 * zone and attempts an update, and gives up if the update fails.
 *
 * TSIG keys are defined like this:
 *
 *	key "FOO.COM Key" {
 *		algorithm HMAC-MD5.SIG-ALG.REG.INT;
 *		secret <Base64>;
 *	}
 *
 * <Base64> is a number expressed in base64 that represents the key.
 * It's also permissible to use a quoted string here - this will be
 * translated as the ASCII bytes making up the string, and will not
 * include any NUL termination.  The key name can be any text string,
 * and the key type must be one of the key types defined in the draft
 * or by the IANA.  Currently only the HMAC-MD5... key type is
 * supported.
 */

struct hash_table *tsig_key_hash;
struct hash_table *dns_zone_hash;

#if defined (NSUPDATE)
isc_result_t find_tsig_key (ns_tsig_key **key, const char *zname)
{
	struct dns_zone *zone;
	isc_result_t status;
	ns_tsig_key *tkey;

	zone = (struct dns_zone *)0;
	status = dns_zone_lookup (&zone, zname);
	if (status != ISC_R_SUCCESS)
		return status;
	if (!zone -> key) {
		dns_zone_dereference (&zone, MDL);
		return ISC_R_KEY_UNKNOWN;
	}
	
	if ((!zone -> key -> name ||
	     strlen (zone -> key -> name) > NS_MAXDNAME) ||
	    (!zone -> key -> algorithm ||
	     strlen (zone -> key -> algorithm) > NS_MAXDNAME) ||
	    (!zone -> key -> key.len)) {
		dns_zone_dereference (&zone, MDL);
		return ISC_R_INVALIDKEY;
	}
	tkey = dmalloc (sizeof *tkey, MDL);
	if (!tkey) {
	      nomem:
		dns_zone_dereference (&zone, MDL);
		return ISC_R_NOMEMORY;
	}
	memset (tkey, 0, sizeof *tkey);
	tkey -> data = dmalloc (zone -> key -> key.len, MDL);
	if (!tkey -> data) {
		dfree (tkey, MDL);
		goto nomem;
	}
	strcpy (tkey -> name, zone -> key -> name);
	strcpy (tkey -> alg, zone -> key -> algorithm);
	memcpy (tkey -> data,
		zone -> key -> key.data, zone -> key -> key.len);
	tkey -> len = zone -> key -> key.len;
	*key = tkey;
	return ISC_R_SUCCESS;
}

void tkey_free (ns_tsig_key **key)
{
	if ((*key) -> data)
		dfree ((*key) -> data, MDL);
	dfree ((*key), MDL);
	*key = (ns_tsig_key *)0;
}
#endif

isc_result_t enter_dns_zone (struct dns_zone *zone)
{
	struct dns_zone *tz;

	if (dns_zone_hash) {
		tz = hash_lookup (dns_zone_hash, zone -> name, 0);
		if (tz == zone)
			return ISC_R_SUCCESS;
		if (tz)
			delete_hash_entry (dns_zone_hash, zone -> name, 0);
	} else {
		dns_zone_hash =
			new_hash ((hash_reference)dns_zone_reference,
				  (hash_dereference)dns_zone_dereference, 1);
		if (!dns_zone_hash)
			return ISC_R_NOMEMORY;
	}
	add_hash (dns_zone_hash, zone -> name, 0, zone);
	return ISC_R_SUCCESS;
}

isc_result_t dns_zone_lookup (struct dns_zone **zone, const char *name) {
	struct dns_zone *tz;

	if (!dns_zone_hash)
		return ISC_R_NOTFOUND;
	tz = hash_lookup (dns_zone_hash, name, 0);
	if (!tz)
		return ISC_R_NOTFOUND;
	if (!dns_zone_reference (zone, tz, MDL))
		return ISC_R_UNEXPECTED;
	return ISC_R_SUCCESS;
}

isc_result_t enter_tsig_key (struct tsig_key *tkey)
{
	struct tsig_key *tk;

	if (tsig_key_hash) {
		tk = hash_lookup (tsig_key_hash, tkey -> name, 0);
		if (tk == tkey)
			return ISC_R_SUCCESS;
		if (tk)
			delete_hash_entry (tsig_key_hash, tkey -> name, 0);
	} else {
		tsig_key_hash =
			new_hash ((hash_reference)tsig_key_reference,
				  (hash_dereference)tsig_key_dereference, 1);
		if (!tsig_key_hash)
			return ISC_R_NOMEMORY;
	}
	add_hash (tsig_key_hash, tkey -> name, 0, tkey);
	return ISC_R_SUCCESS;
	
}

isc_result_t tsig_key_lookup (struct tsig_key **tkey, const char *name) {
	struct tsig_key *tk;

	if (!tsig_key_hash)
		return ISC_R_NOTFOUND;
	tk = hash_lookup (tsig_key_hash, name, 0);
	if (!tk)
		return ISC_R_NOTFOUND;
	if (!tsig_key_reference (tkey, tk, MDL))
		return ISC_R_UNEXPECTED;
	return ISC_R_SUCCESS;
}

int dns_zone_dereference (ptr, file, line)
	struct dns_zone **ptr;
	const char *file;
	int line;
{
	int i;
	struct dns_zone *dns_zone;

	if (!ptr || !*ptr) {
		log_error ("%s(%d): null pointer", file, line);
#if defined (POINTER_DEBUG)
		abort ();
#else
		return 0;
#endif
	}

	dns_zone = *ptr;
	*ptr = (struct dns_zone *)0;
	--dns_zone -> refcnt;
	rc_register (file, line, ptr, dns_zone, dns_zone -> refcnt);
	if (dns_zone -> refcnt > 0)
		return 1;

	if (dns_zone -> refcnt < 0) {
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

	if (dns_zone -> name)
		dfree (dns_zone -> name, file, line);
	if (dns_zone -> key)
		tsig_key_dereference (&dns_zone -> key, file, line);
	if (dns_zone -> primary)
		option_cache_dereference (&dns_zone -> primary, file, line);
	if (dns_zone -> secondary)
		option_cache_dereference (&dns_zone -> secondary, file, line);
	dfree (dns_zone, file, line);
	return 1;
}


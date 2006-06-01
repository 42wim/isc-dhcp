/* hash.c

   Routines for manipulating hash tables... */

/*
 * Copyright (c) 2004-2006 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1995-2003 by Internet Software Consortium
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *   Internet Systems Consortium, Inc.
 *   950 Charter Street
 *   Redwood City, CA 94063
 *   <info@isc.org>
 *   http://www.isc.org/
 *
 * This software has been written for Internet Systems Consortium
 * by Ted Lemon in cooperation with Vixie Enterprises and Nominum, Inc.
 * To learn more about Internet Systems Consortium, see
 * ``http://www.isc.org/''.  To learn more about Vixie Enterprises,
 * see ``http://www.vix.com''.   To learn more about Nominum, Inc., see
 * ``http://www.nominum.com''.
 */

#ifndef lint
static char copyright[] =
"$Id: hash.c,v 1.8 2006/06/01 20:23:17 dhankins Exp $ Copyright (c) 2004-2006 Internet Systems Consortium.  All rights reserved.\n";
#endif /* not lint */

#include <omapip/omapip_p.h>
#include <ctype.h>

static inline unsigned
find_length(const void *key,
	    unsigned (*do_hash)(const void *, unsigned, unsigned))
{
	if (do_hash == do_case_hash || do_hash == do_string_hash)
		return strlen((const char *)key);
	if (do_hash == do_number_hash)
		return sizeof(unsigned);
	if (do_hash == do_ip4_hash)
		return 4;

	log_fatal("Impossible condition at %s:%d.", MDL);
}

int new_hash_table (tp, count, file, line)
	struct hash_table **tp;
	unsigned count;
	const char *file;
	int line;
{
	struct hash_table *rval;

	if (!tp) {
		log_error ("%s(%d): new_hash_table called with null pointer.",
			   file, line);
#if defined (POINTER_DEBUG)
		abort ();
#endif
		return 0;
	}
	if (*tp) {
		log_error ("%s(%d): non-null target for new_hash_table.",
			   file, line);
#if defined (POINTER_DEBUG)
		abort ();
#endif
	}
	rval = dmalloc (sizeof (struct hash_table) -
			(DEFAULT_HASH_SIZE * sizeof (struct hash_bucket *)) +
			(count * sizeof (struct hash_bucket *)), file, line);
	if (!rval)
		return 0;
	rval -> hash_count = count;
	*tp = rval;
	return 1;
}

void free_hash_table (tp, file, line)
	struct hash_table **tp;
	const char *file;
	int line;
{
	int i;
	struct hash_bucket *hbc, *hbn = (struct hash_bucket *)0;
	struct hash_table *ptr = *tp;

#if defined (DEBUG_MEMORY_LEAKAGE) || \
		defined (DEBUG_MEMORY_LEAKAGE_ON_EXIT)
	for (i = 0; i < ptr -> hash_count; i++) {
	    for (hbc = ptr -> buckets [i]; hbc; hbc = hbn) {
		hbn = hbc -> next;
		if (ptr -> dereferencer && hbc -> value)
		    (*ptr -> dereferencer) (&hbc -> value, MDL);
	    }
	    for (hbc = ptr -> buckets [i]; hbc; hbc = hbn) {
		hbn = hbc -> next;
		free_hash_bucket (hbc, MDL);
	    }
	    ptr -> buckets [i] = (struct hash_bucket *)0;
	}
#endif

	dfree ((VOIDPTR)ptr, MDL);
	*tp = (struct hash_table *)0;
}

struct hash_bucket *free_hash_buckets;

#if defined (DEBUG_MEMORY_LEAKAGE_ON_EXIT)
struct hash_bucket *hash_bucket_hunks;

void relinquish_hash_bucket_hunks ()
{
	struct hash_bucket *c, *n, **p;

	/* Account for all the hash buckets on the free list. */
	p = &free_hash_buckets;
	for (c = free_hash_buckets; c; c = c -> next) {
		for (n = hash_bucket_hunks; n; n = n -> next) {
			if (c > n && c < n + 127) {
				*p = c -> next;
				n -> len++;
				break;
			}
		}
		/* If we didn't delete the hash bucket from the free list,
		   advance the pointer. */
		if (!n)
			p = &c -> next;
	}
		
	for (c = hash_bucket_hunks; c; c = n) {
		n = c -> next;
		if (c -> len != 126) {
			log_info ("hashbucket %lx hash_buckets %d free %u",
				  (unsigned long)c, 127, c -> len);
		}
		dfree (c, MDL);
	}
}
#endif

struct hash_bucket *new_hash_bucket (file, line)
	const char *file;
	int line;
{
	struct hash_bucket *rval;
	int i = 0;
	if (!free_hash_buckets) {
		rval = dmalloc (127 * sizeof (struct hash_bucket),
				file, line);
		if (!rval)
			return rval;
# if defined (DEBUG_MEMORY_LEAKAGE_ON_EXIT)
		rval -> next = hash_bucket_hunks;
		hash_bucket_hunks = rval;
		hash_bucket_hunks -> len = 0;
		i++;
		rval++;
#endif
		for (; i < 127; i++) {
			rval -> next = free_hash_buckets;
			free_hash_buckets = rval;
			rval++;
		}
	}
	rval = free_hash_buckets;
	free_hash_buckets = rval -> next;
	return rval;
}

void free_hash_bucket (ptr, file, line)
	struct hash_bucket *ptr;
	const char *file;
	int line;
{
	struct hash_bucket *hp;
#if defined (DEBUG_MALLOC_POOL)
	for (hp = free_hash_buckets; hp; hp = hp -> next) {
		if (hp == ptr) {
			log_error ("hash bucket freed twice!");
			abort ();
		}
	}
#endif
	ptr -> next = free_hash_buckets;
	free_hash_buckets = ptr;
}

int new_hash(struct hash_table **rp,
	     hash_reference referencer,
	     hash_dereference dereferencer,
	     unsigned hsize,
	     unsigned (*hasher)(const void *, unsigned, unsigned),
	     const char *file, int line)
{
	if (hsize == 0)
		hsize = DEFAULT_HASH_SIZE;

	if (!new_hash_table (rp, hsize, file, line))
		return 0;

	memset ((*rp)->buckets, 0, hsize * sizeof(struct hash_bucket *));

	(*rp)->referencer = referencer;
	(*rp)->dereferencer = dereferencer;
	(*rp)->do_hash = hasher;

	if (hasher == do_case_hash)
		(*rp)->cmp = casecmp;
	else
		(*rp)->cmp = memcmp;

	return 1;
}

unsigned
do_case_hash(const void *name, unsigned len, unsigned size)
{
	register unsigned accum = 0;
	register const unsigned char *s = name;
	int i = len;
	register unsigned c;

	while (i--) {
		/* Make the hash case-insensitive. */
		c = *s++;
		if (isascii(c))
			c = tolower(c);

		/* Add the character in... */
		accum = (accum << 1) + c;

		/* Add carry back in... */
		while (accum > 65535) {
			accum = (accum & 65535) + (accum >> 16);
		}

	}
	return accum % size;
}

unsigned
do_string_hash(const void *name, unsigned len, unsigned size)
{
	register unsigned accum = 0;
	register const unsigned char *s = (const unsigned char *)name;
	int i = len;

	while (i--) {
		/* Add the character in... */
		accum = (accum << 1) + *s++;

		/* Add carry back in... */
		while (accum > 65535) {
			accum = (accum & 65535) + (accum >> 16);
		}
	}
	return accum % size;
}

unsigned
do_number_hash(const void *key, unsigned len, unsigned size)
{
	register unsigned number = *((const unsigned *)key);

	return number % size;
}

unsigned
do_ip4_hash(const void *key, unsigned len, unsigned size)
{
	u_int32_t number;

	memcpy(&number, key, 4);

	number = ntohl(number);

	return number % size;
}

void add_hash (table, key, len, pointer, file, line)
	struct hash_table *table;
	unsigned len;
	const void *key;
	hashed_object_t *pointer;
	const char *file;
	int line;
{
	int hashno;
	struct hash_bucket *bp;
	void *foo;

	if (!table)
		return;

	if (!len)
		len = find_length(key, table->do_hash);

	hashno = (*table->do_hash)(key, len, table->hash_count);
	bp = new_hash_bucket (file, line);

	if (!bp) {
		log_error ("Can't add entry to hash table: no memory.");
		return;
	}
	bp -> name = key;
	if (table -> referencer) {
		foo = &bp -> value;
		(*(table -> referencer)) (foo, pointer, file, line);
	} else
		bp -> value = pointer;
	bp -> next = table -> buckets [hashno];
	bp -> len = len;
	table -> buckets [hashno] = bp;
}

void delete_hash_entry (table, key, len, file, line)
	struct hash_table *table;
	unsigned len;
	const void *key;
	const char *file;
	int line;
{
	int hashno;
	struct hash_bucket *bp, *pbp = (struct hash_bucket *)0;
	void *foo;

	if (!table)
		return;

	if (!len)
		len = find_length(key, table->do_hash);

	hashno = (*table->do_hash)(key, len, table->hash_count);

	/* Go through the list looking for an entry that matches;
	   if we find it, delete it. */
	for (bp = table -> buckets [hashno]; bp; bp = bp -> next) {
		if ((!bp -> len &&
		     !strcmp ((const char *)bp->name, key)) ||
		    (bp -> len == len &&
		     !(table -> cmp)(bp->name, key, len))) {
			if (pbp) {
				pbp -> next = bp -> next;
			} else {
				table -> buckets [hashno] = bp -> next;
			}
			if (bp -> value && table -> dereferencer) {
				foo = &bp -> value;
				(*(table -> dereferencer)) (foo, file, line);
			}
			free_hash_bucket (bp, file, line);
			break;
		}
		pbp = bp;	/* jwg, 9/6/96 - nice catch! */
	}
}

int hash_lookup (vp, table, key, len, file, line)
	hashed_object_t **vp;
	struct hash_table *table;
	const void *key;
	unsigned len;
	const char *file;
	int line;
{
	int hashno;
	struct hash_bucket *bp;

	if (!table)
		return 0;
	if (!len)
		len = find_length(key, table->do_hash);

	hashno = (*table->do_hash)(key, len, table->hash_count);

	for (bp = table -> buckets [hashno]; bp; bp = bp -> next) {
		if (len == bp -> len
		    && !(*table->cmp)(bp->name, key, len)) {
			if (table -> referencer)
				(*table -> referencer) (vp, bp -> value,
							file, line);
			else
				*vp = bp -> value;
			return 1;
		}
	}
	return 0;
}

int hash_foreach (struct hash_table *table, hash_foreach_func func)
{
	int i;
	struct hash_bucket *bp, *next;
	int count = 0;

	if (!table)
		return 0;

	for (i = 0; i < table -> hash_count; i++) {
		bp = table -> buckets [i];
		while (bp) {
			next = bp -> next;
			if ((*func)(bp->name, bp->len, bp->value)
							!= ISC_R_SUCCESS)
				return count;
			bp = next;
			count++;
		}
	}
	return count;
}

int casecmp (const void *v1, const void *v2, unsigned long len)
{
	unsigned i;
	const char *s = v1;
	const char *t = v2;
	
	for (i = 0; i < len; i++)
	{
		int c1, c2;
		if (isascii (s [i]) && isupper (s [i]))
			c1 = tolower (s [i]);
		else
			c1 = s [i];
		
		if (isascii (t [i]) && isupper (t [i]))
			c2 = tolower (t [i]);
		else
			c2 = t [i];
		
		if (c1 < c2)
			return -1;
		if (c1 > c2)
			return 1;
	}
	return 0;
}

/* parse.c

   Common parser code for dhcpd and dhclient. */

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
"$Id: parse.c,v 1.63 2000/02/05 18:04:47 mellon Exp $ Copyright (c) 1995, 1996, 1997, 1998, 1999 The Internet Software Consortium.  All rights reserved.\n";
#endif /* not lint */

#include "dhcpd.h"

/* Skip to the semicolon ending the current statement.   If we encounter
   braces, the matching closing brace terminates the statement.   If we
   encounter a right brace but haven't encountered a left brace, return
   leaving the brace in the token buffer for the caller.   If we see a
   semicolon and haven't seen a left brace, return.   This lets us skip
   over:

   	statement;
	statement foo bar { }
	statement foo bar { statement { } }
	statement}
 
	...et cetera. */

void skip_to_semi (cfile)
	struct parse *cfile;
{
	skip_to_rbrace (cfile, 0);
}

void skip_to_rbrace (cfile, brace_count)
	struct parse *cfile;
	int brace_count;
{
	enum dhcp_token token;
	const char *val;

#if defined (DEBUG_TOKEN)
	log_error ("skip_to_rbrace: %d\n", brace_count);
#endif
	do {
		token = peek_token (&val, cfile);
		if (token == RBRACE) {
			token = next_token (&val, cfile);
			if (brace_count) {
				if (!--brace_count)
					return;
			} else
				return;
		} else if (token == LBRACE) {
			brace_count++;
		} else if (token == SEMI && !brace_count) {
			token = next_token (&val, cfile);
			return;
		} else if (token == EOL) {
			/* EOL only happens when parsing /etc/resolv.conf,
			   and we treat it like a semicolon because the
			   resolv.conf file is line-oriented. */
			token = next_token (&val, cfile);
			return;
		}
		token = next_token (&val, cfile);
	} while (token != EOF);
}

int parse_semi (cfile)
	struct parse *cfile;
{
	enum dhcp_token token;
	const char *val;

	token = next_token (&val, cfile);
	if (token != SEMI) {
		parse_warn (cfile, "semicolon expected.");
		skip_to_semi (cfile);
		return 0;
	}
	return 1;
}

/* string-parameter :== STRING SEMI */

char *parse_string (cfile)
	struct parse *cfile;
{
	const char *val;
	enum dhcp_token token;
	char *s;

	token = next_token (&val, cfile);
	if (token != STRING) {
		parse_warn (cfile, "filename must be a string");
		skip_to_semi (cfile);
		return (char *)0;
	}
	s = (char *)dmalloc (strlen (val) + 1, MDL);
	if (!s)
		log_fatal ("no memory for string %s.", val);
	strcpy (s, val);

	if (!parse_semi (cfile))
		return (char *)0;
	return s;
}

/*
 * hostname :== IDENTIFIER
 *		| IDENTIFIER DOT
 *		| hostname DOT IDENTIFIER
 */

char *parse_host_name (cfile)
	struct parse *cfile;
{
	const char *val;
	enum dhcp_token token;
	unsigned len = 0;
	char *s;
	char *t;
	pair c = (pair)0;
	
	/* Read a dotted hostname... */
	do {
		/* Read a token, which should be an identifier. */
		token = peek_token (&val, cfile);
		if (!is_identifier (token) && token != NUMBER)
			break;
		token = next_token (&val, cfile);

		/* Store this identifier... */
		if (!(s = (char *)dmalloc (strlen (val) + 1, MDL)))
			log_fatal ("can't allocate temp space for hostname.");
		strcpy (s, val);
		c = cons ((caddr_t)s, c);
		len += strlen (s) + 1;
		/* Look for a dot; if it's there, keep going, otherwise
		   we're done. */
		token = peek_token (&val, cfile);
		if (token == DOT)
			token = next_token (&val, cfile);
	} while (token == DOT);

	/* Assemble the hostname together into a string. */
	if (!(s = (char *)dmalloc (len, MDL)))
		log_fatal ("can't allocate space for hostname.");
	t = s + len;
	*--t = 0;
	while (c) {
		pair cdr = c -> cdr;
		unsigned l = strlen ((char *)(c -> car));
		t -= l;
		memcpy (t, (char *)(c -> car), l);
		/* Free up temp space. */
		dfree (c -> car, MDL);
		dfree (c, MDL);
		c = cdr;
		if (t != s)
			*--t = '.';
	}
	return s;
}

/* ip-addr-or-hostname :== ip-address | hostname
   ip-address :== NUMBER DOT NUMBER DOT NUMBER DOT NUMBER
   
   Parse an ip address or a hostname.   If uniform is zero, put in
   an expr_substring node to limit hostnames that evaluate to more
   than one IP address. */

int parse_ip_addr_or_hostname (expr, cfile, uniform)
	struct expression **expr;
	struct parse *cfile;
	int uniform;
{
	const char *val;
	enum dhcp_token token;
	unsigned char addr [4];
	unsigned len = sizeof addr;
	char *name;
	struct expression *x = (struct expression *)0;

	token = peek_token (&val, cfile);
	if (is_identifier (token)) {
		name = parse_host_name (cfile);
		if (!name)
			return 0;
		if (!make_host_lookup (expr, name))
			return 0;
		if (!uniform) {
			if (!make_limit (&x, *expr, 4))
				return 0;
			expression_dereference (expr, MDL);
			*expr = x;
		}
	} else if (token == NUMBER) {
		if (!parse_numeric_aggregate (cfile, addr, &len, DOT, 10, 8))
			return 0;
		return make_const_data (expr, addr, len, 0, 1);
	} else {
		if (token != RBRACE && token != LBRACE)
			token = next_token (&val, cfile);
		parse_warn (cfile, "%s (%d): expecting IP address or hostname",
			    val, token);
		if (token != SEMI)
			skip_to_semi (cfile);
		return 0;
	}

	return 1;
}	
	
/*
 * ip-address :== NUMBER DOT NUMBER DOT NUMBER DOT NUMBER
 */

int parse_ip_addr (cfile, addr)
	struct parse *cfile;
	struct iaddr *addr;
{
	const char *val;
	enum dhcp_token token;

	addr -> len = 4;
	if (parse_numeric_aggregate (cfile, addr -> iabuf,
				     &addr -> len, DOT, 10, 8))
		return 1;
	return 0;
}	

/*
 * hardware-parameter :== HARDWARE hardware-type colon-seperated-hex-list SEMI
 * hardware-type :== ETHERNET | TOKEN_RING
 */

void parse_hardware_param (cfile, hardware)
	struct parse *cfile;
	struct hardware *hardware;
{
	const char *val;
	enum dhcp_token token;
	unsigned hlen;
	unsigned char *t;

	token = next_token (&val, cfile);
	switch (token) {
	      case ETHERNET:
		hardware -> hbuf [0] = HTYPE_ETHER;
		break;
	      case TOKEN_RING:
		hardware -> hbuf [0] = HTYPE_IEEE802;
		break;
	      case FDDI:
		hardware -> hbuf [0] = HTYPE_FDDI;
		break;
	      default:
		parse_warn (cfile, "expecting a network hardware type");
		skip_to_semi (cfile);
		return;
	}

	/* Parse the hardware address information.   Technically,
	   it would make a lot of sense to restrict the length of the
	   data we'll accept here to the length of a particular hardware
	   address type.   Unfortunately, there are some broken clients
	   out there that put bogus data in the chaddr buffer, and we accept
	   that data in the lease file rather than simply failing on such
	   clients.   Yuck. */
	hlen = 0;
	t = parse_numeric_aggregate (cfile, (unsigned char *)0, &hlen,
				     COLON, 16, 8);
	if (!t)
		return;
	if (hlen + 1 > sizeof hardware -> hbuf) {
		dfree (t, MDL);
		parse_warn (cfile, "hardware address too long");
	} else {
		hardware -> hlen = hlen + 1;
		memcpy ((unsigned char *)&hardware -> hbuf [1], t, hlen);
		if (hlen + 1 < sizeof hardware -> hbuf)
			memset (&hardware -> hbuf [hlen + 1], 0,
				(sizeof hardware -> hbuf) - hlen - 1);
		dfree (t, MDL);
	}
	
	token = next_token (&val, cfile);
	if (token != SEMI) {
		parse_warn (cfile, "expecting semicolon.");
		skip_to_semi (cfile);
	}
}

/* lease-time :== NUMBER SEMI */

void parse_lease_time (cfile, timep)
	struct parse *cfile;
	TIME *timep;
{
	const char *val;
	enum dhcp_token token;

	token = next_token (&val, cfile);
	if (token != NUMBER) {
		parse_warn (cfile, "Expecting numeric lease time");
		skip_to_semi (cfile);
		return;
	}
	convert_num (cfile, (unsigned char *)timep, val, 10, 32);
	/* Unswap the number - convert_num returns stuff in NBO. */
	*timep = ntohl (*timep); /* XXX */

	parse_semi (cfile);
}

/* No BNF for numeric aggregates - that's defined by the caller.  What
   this function does is to parse a sequence of numbers seperated by
   the token specified in seperator.  If max is zero, any number of
   numbers will be parsed; otherwise, exactly max numbers are
   expected.  Base and size tell us how to internalize the numbers
   once they've been tokenized. */

unsigned char *parse_numeric_aggregate (cfile, buf,
					max, seperator, base, size)
	struct parse *cfile;
	unsigned char *buf;
	unsigned *max;
	int seperator;
	int base;
	unsigned size;
{
	const char *val;
	enum dhcp_token token;
	unsigned char *bufp = buf, *s, *t;
	unsigned count = 0;
	pair c = (pair)0;

	if (!bufp && *max) {
		bufp = (unsigned char *)dmalloc (*max * size / 8, MDL);
		if (!bufp)
			log_fatal ("no space for numeric aggregate");
		s = 0;
	} else
		s = bufp;

	do {
		if (count) {
			token = peek_token (&val, cfile);
			if (token != seperator) {
				if (!*max)
					break;
				if (token != RBRACE && token != LBRACE)
					token = next_token (&val, cfile);
				parse_warn (cfile, "too few numbers.");
				if (token != SEMI)
					skip_to_semi (cfile);
				return (unsigned char *)0;
			}
			token = next_token (&val, cfile);
		}
		token = next_token (&val, cfile);

		if (token == EOF) {
			parse_warn (cfile, "unexpected end of file");
			break;
		}

		/* Allow NUMBER_OR_NAME if base is 16. */
		if (token != NUMBER &&
		    (base != 16 || token != NUMBER_OR_NAME)) {
			parse_warn (cfile, "expecting numeric value.");
			skip_to_semi (cfile);
			return (unsigned char *)0;
		}
		/* If we can, convert the number now; otherwise, build
		   a linked list of all the numbers. */
		if (s) {
			convert_num (cfile, s, val, base, size);
			s += size / 8;
		} else {
			t = (unsigned char *)dmalloc (strlen (val) + 1, MDL);
			if (!t)
				log_fatal ("no temp space for number.");
			strcpy ((char *)t, val);
			c = cons ((caddr_t)t, c);
		}
	} while (++count != *max);

	/* If we had to cons up a list, convert it now. */
	if (c) {
		bufp = (unsigned char *)dmalloc (count * size / 8, MDL);
		if (!bufp)
			log_fatal ("no space for numeric aggregate.");
		s = bufp + count - size / 8;
		*max = count;
	}
	while (c) {
		pair cdr = c -> cdr;
		convert_num (cfile, s, (char *)(c -> car), base, size);
		s -= size / 8;
		/* Free up temp space. */
		dfree (c -> car, MDL);
		dfree (c, MDL);
		c = cdr;
	}
	return bufp;
}

void convert_num (cfile, buf, str, base, size)
	struct parse *cfile;
	unsigned char *buf;
	const char *str;
	int base;
	unsigned size;
{
	const char *ptr = str;
	int negative = 0;
	u_int32_t val = 0;
	int tval;
	int max;

	if (*ptr == '-') {
		negative = 1;
		++ptr;
	}

	/* If base wasn't specified, figure it out from the data. */
	if (!base) {
		if (ptr [0] == '0') {
			if (ptr [1] == 'x') {
				base = 16;
				ptr += 2;
			} else if (isascii (ptr [1]) && isdigit (ptr [1])) {
				base = 8;
				ptr += 1;
			} else {
				base = 10;
			}
		} else {
			base = 10;
		}
	}

	do {
		tval = *ptr++;
		/* XXX assumes ASCII... */
		if (tval >= 'a')
			tval = tval - 'a' + 10;
		else if (tval >= 'A')
			tval = tval - 'A' + 10;
		else if (tval >= '0')
			tval -= '0';
		else {
			parse_warn (cfile, "Bogus number: %s.", str);
			break;
		}
		if (tval >= base) {
			parse_warn (cfile,
				    "Bogus number %s: digit %d not in base %d",
				    str, tval, base);
			break;
		}
		val = val * base + tval;
	} while (*ptr);

	if (negative)
		max = (1 << (size - 1));
	else
		max = (1 << (size - 1)) + ((1 << (size - 1)) - 1);
	if (val > max) {
		switch (base) {
		      case 8:
			parse_warn (cfile,
				    "%s%lo exceeds max (%d) for precision.",
				    negative ? "-" : "",
				    (unsigned long)val, max);
			break;
		      case 16:
			parse_warn (cfile,
				    "%s%lx exceeds max (%d) for precision.",
				    negative ? "-" : "",
				    (unsigned long)val, max);
			break;
		      default:
			parse_warn (cfile,
				    "%s%lu exceeds max (%d) for precision.",
				    negative ? "-" : "",
				    (unsigned long)val, max);
			break;
		}
	}

	if (negative) {
		switch (size) {
		      case 8:
			*buf = -(unsigned long)val;
			break;
		      case 16:
			putShort (buf, -(long)val);
			break;
		      case 32:
			putLong (buf, -(long)val);
			break;
		      default:
			parse_warn (cfile,
				    "Unexpected integer size: %d\n", size);
			break;
		}
	} else {
		switch (size) {
		      case 8:
			*buf = (u_int8_t)val;
			break;
		      case 16:
			putUShort (buf, (u_int16_t)val);
			break;
		      case 32:
			putULong (buf, val);
			break;
		      default:
			parse_warn (cfile,
				    "Unexpected integer size: %d\n", size);
			break;
		}
	}
}

/*
 * date :== NUMBER NUMBER SLASH NUMBER SLASH NUMBER 
 *		NUMBER COLON NUMBER COLON NUMBER SEMI |
 *          NUMBER NUMBER SLASH NUMBER SLASH NUMBER 
 *		NUMBER COLON NUMBER COLON NUMBER NUMBER SEMI |
 *	    NEVER
 *
 * Dates are stored in GMT or with a timezone offset; first number is day
 * of week; next is year/month/day; next is hours:minutes:seconds on a
 * 24-hour clock, followed by the timezone offset in seconds, which is
 * optional.
 */

TIME parse_date (cfile)
	struct parse *cfile;
{
	struct tm tm;
	int guess;
	int tzoff, wday, year, mon, mday, hour, min, sec;
	const char *val;
	enum dhcp_token token;
	static int months [11] = { 31, 59, 90, 120, 151, 181,
					  212, 243, 273, 304, 334 };

	/* Day of week, or "never"... */
	token = next_token (&val, cfile);
	if (token == NEVER) {
		if (!parse_semi (cfile))
			return 0;
		return MAX_TIME;
	}

	if (token != NUMBER) {
		parse_warn (cfile, "numeric day of week expected.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}
	wday = atoi (val);

	/* Year... */
	token = next_token (&val, cfile);
	if (token != NUMBER) {
		parse_warn (cfile, "numeric year expected.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}

	/* Note: the following is not a Y2K bug - it's a Y1.9K bug.   Until
	   somebody invents a time machine, I think we can safely disregard
	   it.   This actually works around a stupid Y2K bug that was present
	   in a very early beta release of dhcpd. */
	year = atoi (val);
	if (year > 1900)
		year -= 1900;

	/* Slash seperating year from month... */
	token = next_token (&val, cfile);
	if (token != SLASH) {
		parse_warn (cfile,
			    "expected slash seperating year from month.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}

	/* Month... */
	token = next_token (&val, cfile);
	if (token != NUMBER) {
		parse_warn (cfile, "numeric month expected.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}
	mon = atoi (val) - 1;

	/* Slash seperating month from day... */
	token = next_token (&val, cfile);
	if (token != SLASH) {
		parse_warn (cfile,
			    "expected slash seperating month from day.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}

	/* Month... */
	token = next_token (&val, cfile);
	if (token != NUMBER) {
		parse_warn (cfile, "numeric day of month expected.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}
	mday = atoi (val);

	/* Hour... */
	token = next_token (&val, cfile);
	if (token != NUMBER) {
		parse_warn (cfile, "numeric hour expected.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}
	hour = atoi (val);

	/* Colon seperating hour from minute... */
	token = next_token (&val, cfile);
	if (token != COLON) {
		parse_warn (cfile,
			    "expected colon seperating hour from minute.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}

	/* Minute... */
	token = next_token (&val, cfile);
	if (token != NUMBER) {
		parse_warn (cfile, "numeric minute expected.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}
	min = atoi (val);

	/* Colon seperating minute from second... */
	token = next_token (&val, cfile);
	if (token != COLON) {
		parse_warn (cfile,
			    "expected colon seperating hour from minute.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}

	/* Minute... */
	token = next_token (&val, cfile);
	if (token != NUMBER) {
		parse_warn (cfile, "numeric minute expected.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (TIME)0;
	}
	sec = atoi (val);

	token = peek_token (&val, cfile);
	if (token == NUMBER) {
		token = next_token (&val, cfile);
		tzoff = atoi (val);
	} else
		tzoff = 0;

	/* Make sure the date ends in a semicolon... */
	if (!parse_semi (cfile))
		return 0;

	/* Guess the time value... */
	guess = ((((((365 * (year - 70) +	/* Days in years since '70 */
		      (year - 69) / 4 +		/* Leap days since '70 */
		      (mon			/* Days in months this year */
		       ? months [mon - 1]
		       : 0) +
		      (mon > 1 &&		/* Leap day this year */
		       !((year - 72) & 3)) +
		      mday - 1) * 24) +		/* Day of month */
		    hour) * 60) +
		  min) * 60) + sec + tzoff;

	/* This guess could be wrong because of leap seconds or other
	   weirdness we don't know about that the system does.   For
	   now, we're just going to accept the guess, but at some point
	   it might be nice to do a successive approximation here to
	   get an exact value.   Even if the error is small, if the
	   server is restarted frequently (and thus the lease database
	   is reread), the error could accumulate into something
	   significant. */

	return guess;
}

/*
 * option-name :== IDENTIFIER |
 		   IDENTIFIER . IDENTIFIER
 */

struct option *parse_option_name (cfile, allocate, known)
	struct parse *cfile;
	int allocate;
	int *known;
{
	const char *val;
	enum dhcp_token token;
	char *uname;
	struct universe *universe;
	struct option *option;

	token = next_token (&val, cfile);
	if (!is_identifier (token)) {
		parse_warn (cfile,
			    "expecting identifier after option keyword.");
		if (token != SEMI)
			skip_to_semi (cfile);
		return (struct option *)0;
	}
	uname = dmalloc (strlen (val) + 1, MDL);
	if (!uname)
		log_fatal ("no memory for uname information.");
	strcpy (uname, val);
	token = peek_token (&val, cfile);
	if (token == DOT) {
		/* Go ahead and take the DOT token... */
		token = next_token (&val, cfile);

		/* The next token should be an identifier... */
		token = next_token (&val, cfile);
		if (!is_identifier (token)) {
			parse_warn (cfile, "expecting identifier after '.'");
			if (token != SEMI)
				skip_to_semi (cfile);
			return (struct option *)0;
		}

		/* Look up the option name hash table for the specified
		   uname. */
		universe = ((struct universe *)
			    hash_lookup (&universe_hash,
					 (unsigned char *)uname, 0));
		/* If it's not there, we can't parse the rest of the
		   declaration. */
		if (!universe) {
			parse_warn (cfile, "no option space named %s.", uname);
			skip_to_semi (cfile);
			return (struct option *)0;
		}
	} else {
		/* Use the default hash table, which contains all the
		   standard dhcp option names. */
		val = uname;
		universe = &dhcp_universe;
	}

	/* Look up the actual option info... */
	option = (struct option *)hash_lookup (universe -> hash,
					       (const unsigned char *)val, 0);

	/* If we didn't get an option structure, it's an undefined option. */
	if (option) {
		*known = 1;
	} else {
		/* If we've been told to allocate, that means that this
		   (might) be an option code definition, so we'll create
		   an option structure just in case. */
		if (allocate) {
			option = new_option (MDL);
			if (val == uname)
				option -> name = val;
			else {
				char *s;
				dfree (uname, MDL);
				s = dmalloc (strlen (val) + 1, MDL);
				if (!s)
				    log_fatal ("no memory for option %s.%s",
					       universe -> name, val);
				strcpy (s, val);
				option -> name = s;
			}
			option -> universe = universe;
			option -> code = 0;
			return option;
		}
		if (val == uname)
			parse_warn (cfile, "no option named %s", val);
		else
			parse_warn (cfile, "no option named %s in space %s",
				    val, uname);
		skip_to_semi (cfile);
		return (struct option *)0;
	}

	/* Free the initial identifier token. */
	dfree (uname, MDL);
	return option;
}

/* IDENTIFIER SEMI */

void parse_option_space_decl (cfile)
	struct parse *cfile;
{
	int token;
	const char *val;
	struct universe **ua, *nu;
	char *s;

	next_token (&val, cfile);	/* Discard the SPACE token, which was
					   checked by the caller. */
	token = next_token (&val, cfile);
	if (!is_identifier (token)) {
		parse_warn (cfile, "expecting identifier.");
		skip_to_semi (cfile);
		return;
	}
	nu = new_universe (MDL);
	if (!nu)
		log_fatal ("No memory for new option space.");

	/* Set up the server option universe... */
	s = dmalloc (strlen (val) + 1, MDL);
	if (!s)
		log_fatal ("No memory for new option space name.");
	strcpy (s, val);
	nu -> name = s;
	nu -> lookup_func = lookup_hashed_option;
	nu -> option_state_dereference =
		hashed_option_state_dereference;
	nu -> get_func = hashed_option_get;
	nu -> set_func = hashed_option_set;
	nu -> save_func = save_hashed_option;
	nu -> delete_func = delete_hashed_option;
	nu -> encapsulate = hashed_option_space_encapsulate;
	nu -> length_size = 1;
	nu -> tag_size = 1;
	nu -> store_tag = putUChar;
	nu -> store_length = putUChar;
	nu -> index = universe_count++;
	if (nu -> index >= universe_max) {
		ua = dmalloc (universe_max * 2 * sizeof *ua, MDL);
		if (!ua)
			log_fatal ("No memory to expand option space array.");
		memcpy (ua, universes, universe_max * sizeof *ua);
		universe_max *= 2;
		dfree (universes, MDL);
		universes = ua;
	}
	universes [nu -> index] = nu;
	nu -> hash = new_hash ();
	if (!nu -> hash)
		log_fatal ("Can't allocate %s option hash table.", nu -> name);
	add_hash (&universe_hash,
		  (const unsigned char *)nu -> name, 0, (unsigned char *)nu);
	parse_semi (cfile);
}

/* This is faked up to look good right now.   Ideally, this should do a
   recursive parse and allow arbitrary data structure definitions, but for
   now it just allows you to specify a single type, an array of single types,
   a sequence of types, or an array of sequences of types.

   ocd :== NUMBER EQUALS ocsd SEMI

   ocsd :== ocsd_type |
	    ocsd_type_sequence |
	    ARRAY OF ocsd_type |
	    ARRAY OF ocsd_type_sequence

   ocsd_type :== BOOLEAN |
		 INTEGER NUMBER |
		 SIGNED INTEGER NUMBER |
		 UNSIGNED INTEGER NUMBER |
		 IP-ADDRESS |
		 TEXT |
		 STRING

   ocsd_type_sequence :== LBRACE ocsd_types RBRACE

   ocsd_type :== ocsd_type |
		 ocsd_types ocsd_type */

int parse_option_code_definition (cfile, option)
	struct parse *cfile;
	struct option *option;
{
	const char *val;
	enum dhcp_token token;
	unsigned arrayp = 0;
	int recordp = 0;
	int no_more_in_record = 0;
	char tokbuf [128];
	unsigned tokix = 0;
	char type;
	int code;
	int is_signed;
	char *s;
	
	/* Parse the option code. */
	token = next_token (&val, cfile);
	if (token != NUMBER) {
		parse_warn (cfile, "expecting option code number.");
		skip_to_semi (cfile);
		return 0;
	}
	option -> code = atoi (val);

	token = next_token (&val, cfile);
	if (token != EQUAL) {
		parse_warn (cfile, "expecting \"=\"");
		skip_to_semi (cfile);
		return 0;
	}

	/* See if this is an array. */
	token = next_token (&val, cfile);
	if (token == ARRAY) {
		token = next_token (&val, cfile);
		if (token != OF) {
			parse_warn (cfile, "expecting \"of\".");
			skip_to_semi (cfile);
			return 0;
		}
		arrayp = 1;
		token = next_token (&val, cfile);
	}

	if (token == LBRACE) {
		recordp = 1;
		token = next_token (&val, cfile);
	}

	/* At this point we're expecting a data type. */
      next_type:
	switch (token) {
	      case BOOLEAN:
		type = 'f';
		break;
	      case INTEGER:
		is_signed = 1;
	      parse_integer:
		token = next_token (&val, cfile);
		if (token != NUMBER) {
			parse_warn (cfile, "expecting number.");
			skip_to_rbrace (cfile, recordp);
			if (recordp)
				skip_to_semi (cfile);
			return 0;
		}
		switch (atoi (val)) {
		      case 8:
			type = is_signed ? 'b' : 'B';
			break;
		      case 16:
			type = is_signed ? 's' : 'S';
			break;
		      case 32:
			type = is_signed ? 'l' : 'L';
			break;
		      default:
			parse_warn (cfile,
				    "%s bit precision is not supported.", val);
			skip_to_rbrace (cfile, recordp);
			if (recordp)
				skip_to_semi (cfile);
			return 0;
		}
		break;
	      case SIGNED:
		is_signed = 1;
	      parse_signed:
		token = next_token (&val, cfile);
		if (token != INTEGER) {
			parse_warn (cfile, "expecting \"integer\" keyword.");
			skip_to_rbrace (cfile, recordp);
			if (recordp)
				skip_to_semi (cfile);
			return 0;
		}
		goto parse_integer;
	      case UNSIGNED:
		is_signed = 0;
		goto parse_signed;

	      case IP_ADDRESS:
		type = 'I';
		break;
	      case TEXT:
		type = 't';
	      no_arrays:
		if (arrayp) {
			parse_warn (cfile, "arrays of text strings not %s",
				    "yet supported.");
			skip_to_rbrace (cfile, recordp);
			if (recordp)
				skip_to_semi (cfile);
			return 0;
		}
		no_more_in_record = 1;
		break;
	      case STRING:
		type = 'X';
		goto no_arrays;

	      default:
		parse_warn (cfile, "unknown data type %s", val);
		skip_to_rbrace (cfile, recordp);
		if (recordp)
			skip_to_semi (cfile);
		return 0;
	}

	if (tokix == sizeof tokbuf) {
		parse_warn (cfile, "too many types in record.");
		skip_to_rbrace (cfile, recordp);
		if (recordp)
			skip_to_semi (cfile);
		return 0;
	}
	tokbuf [tokix++] = type;

	if (recordp) {
		token = next_token (&val, cfile);
		if (token == COMMA) {
			if (no_more_in_record) {
				parse_warn (cfile,
					    "%s must be at end of record.",
					    type == 't' ? "text" : "string");
				skip_to_rbrace (cfile, 1);
				if (recordp)
					skip_to_semi (cfile);
				return 0;
			}
			token = next_token (&val, cfile);
			goto next_type;
		}
		if (token != RBRACE) {
			parse_warn (cfile, "expecting right brace.");
			skip_to_rbrace (cfile, 1);
			if (recordp)
				skip_to_semi (cfile);
			return 0;
		}
	}
	if (!parse_semi (cfile)) {
		parse_warn (cfile, "semicolon expected.");
		skip_to_semi (cfile);
		if (recordp)
			skip_to_semi (cfile);
		return 0;
	}
	s = dmalloc (tokix + arrayp + 1, MDL);
	if (!s)
		log_fatal ("no memory for option format.");
	memcpy (s, tokbuf, tokix);
	if (arrayp)
		s [tokix++] = 'A';
	s [tokix] = 0;
	option -> format = s;
	if (option -> universe -> options [option -> code]) {
		/* XXX Free the option, but we can't do that now because they
		   XXX may start out static. */
	}
	option -> universe -> options [option -> code] = option;
	add_hash (option -> universe -> hash,
		  (const unsigned char *)option -> name,
		  0, (unsigned char *)option);
	return 1;
}

/*
 * colon-seperated-hex-list :== NUMBER |
 *				NUMBER COLON colon-seperated-hex-list
 */

int parse_cshl (data, cfile)
	struct data_string *data;
	struct parse *cfile;
{
	u_int8_t ibuf [128];
	unsigned ilen = 0;
	unsigned tlen = 0;
	struct option_tag *sl = (struct option_tag *)0;
	struct option_tag *next, **last = &sl;
	enum dhcp_token token;
	const char *val;
	unsigned char *rvp;

	do {
		token = next_token (&val, cfile);
		if (token != NUMBER && token != NUMBER_OR_NAME) {
			parse_warn (cfile, "expecting hexadecimal number.");
			skip_to_semi (cfile);
			for (; sl; sl = next) {
				next = sl -> next;
				dfree (sl, MDL);
			}
			return 0;
		}
		if (ilen == sizeof ibuf) {
			next = (struct option_tag *)
				dmalloc (ilen - 1 +
					 sizeof (struct option_tag), MDL);
			if (!next)
				log_fatal ("no memory for string list.");
			memcpy (next -> data, ibuf, ilen);
			*last = next;
			last = &next -> next;
			tlen += ilen;
			ilen = 0;
		}
		convert_num (cfile, &ibuf [ilen++], val, 16, 8);

		token = peek_token (&val, cfile);
		if (token != COLON)
			break;
		token = next_token (&val, cfile);
	} while (1);

	if (!buffer_allocate (&data -> buffer, tlen + ilen, MDL))
		log_fatal ("no memory to store octet data.");
	data -> data = &data -> buffer -> data [0];
	data -> len = tlen + ilen;
	data -> terminated = 0;

	rvp = &data -> buffer -> data [0];
	while (sl) {
		next = sl -> next;
		memcpy (rvp, sl -> data, sizeof ibuf);
		rvp += sizeof ibuf;
		dfree (sl, MDL);
		sl = next;
	}
	
	memcpy (rvp, ibuf, ilen);
	return 1;
}

/*
 * executable-statements :== executable-statement executable-statements |
 *			     executable-statement
 *
 * executable-statement :==
 *	IF if-statement |
 * 	ADD class-name SEMI |
 *	BREAK SEMI |
 *	OPTION option-parameter SEMI |
 *	SUPERSEDE option-parameter SEMI |
 *	PREPEND option-parameter SEMI |
 *	APPEND option-parameter SEMI
 */

int parse_executable_statements (statements, cfile, lose, case_context)
	struct executable_statement **statements;
	struct parse *cfile;
	int *lose;
	enum expression_context case_context;
{
	struct executable_statement **next;

	next = statements;
	while (parse_executable_statement (next, cfile, lose, case_context))
		next = &((*next) -> next);
	if (!*lose)
		return 1;
	return 0;
}

int parse_executable_statement (result, cfile, lose, case_context)
	struct executable_statement **result;
	struct parse *cfile;
	int *lose;
	enum expression_context case_context;
{
	enum dhcp_token token;
	const char *val;
	struct executable_statement base;
	struct class *cta;
	struct option *option;
	struct option_cache *cache;
	int known;
	int flag;

	token = peek_token (&val, cfile);
	switch (token) {
	      case IF:
		next_token (&val, cfile);
		return parse_if_statement (result, cfile, lose);

	      case TOKEN_ADD:
		token = next_token (&val, cfile);
		token = next_token (&val, cfile);
		if (token != STRING) {
			parse_warn (cfile, "expecting class name.");
			skip_to_semi (cfile);
			*lose = 1;
			return 0;
		}
		cta = find_class (val);
		if (!cta) {
			parse_warn (cfile, "unknown class %s.", val);
			skip_to_semi (cfile);
			*lose = 1;
			return 0;
		}
		if (!parse_semi (cfile)) {
			*lose = 1;
			return 0;
		}
		if (!executable_statement_allocate (result, MDL))
			log_fatal ("no memory for new statement.");
		(*result) -> op = add_statement;
		(*result) -> data.add = cta;
		break;

	      case BREAK:
		token = next_token (&val, cfile);
		if (!parse_semi (cfile)) {
			*lose = 1;
			return 0;
		}
		if (!executable_statement_allocate (result, MDL))
			log_fatal ("no memory for new statement.");
		(*result) -> op = break_statement;
		break;

	      case SEND:
		*lose = 1;
		parse_warn (cfile, "send not appropriate here.");
		skip_to_semi (cfile);
		return 0;

	      case SUPERSEDE:
	      case OPTION:
		token = next_token (&val, cfile);
		known = 0;
		option = parse_option_name (cfile, 0, &known);
		if (!option) {
			*lose = 1;
			return 0;
		}
		return parse_option_statement (result, cfile, 1, option,
					       supersede_option_statement);

	      case ALLOW:
		flag = 1;
		goto pad;
	      case DENY:
		flag = 0;
		goto pad;
	      case IGNORE:
		flag = 2;
	      pad:
		token = next_token (&val, cfile);
		cache = (struct option_cache *)0;
		if (!parse_allow_deny (&cache, cfile, flag))
			return 0;
		if (!executable_statement_allocate (result, MDL))
			log_fatal ("no memory for new statement.");
		(*result) -> op = supersede_option_statement;
		(*result) -> data.option = cache;
		break;

	      case DEFAULT:
		token = next_token (&val, cfile);
		token = peek_token (&val, cfile);
		if (token == COLON)
			goto switch_default;
		known = 0;
		option = parse_option_name (cfile, 0, &known);
		if (!option) {
			*lose = 1;
			return 0;
		}
		return parse_option_statement (result, cfile, 1, option,
					       default_option_statement);

	      case PREPEND:
		token = next_token (&val, cfile);
		known = 0;
		option = parse_option_name (cfile, 0, &known);
		if (!option) {
			*lose = 1;
			return 0;
		}
		return parse_option_statement (result, cfile, 1, option,
					       prepend_option_statement);

	      case APPEND:
		token = next_token (&val, cfile);
		known = 0;
		option = parse_option_name (cfile, 0, &known);
		if (!option) {
			*lose = 1;
			return 0;
		}
		return parse_option_statement (result, cfile, 1, option,
					       append_option_statement);

	      case ON:
		token = next_token (&val, cfile);
		return parse_on_statement (result, cfile, lose);
			
	      case SWITCH:
		token = next_token (&val, cfile);
		return parse_switch_statement (result, cfile, lose);

	      case CASE:
		token = next_token (&val, cfile);
		if (case_context == context_any) {
			parse_warn (cfile,
				    "case statement in inappropriate scope.");
			*lose = 1;
			skip_to_semi (cfile);
			return 0;
		}
		return parse_case_statement (result,
					     cfile, lose, case_context);

	      switch_default:
		token = next_token (&val, cfile);
		if (case_context == context_any) {
			parse_warn (cfile, "switch default statement in %s",
				    "inappropriate scope.");
		
			*lose = 1;
			return 0;
		} else {
			if (!executable_statement_allocate (result, MDL))
				log_fatal ("no memory for default statement.");
			(*result) -> op = default_statement;
			return 1;
		}
			
	      case TOKEN_SET:
		token = next_token (&val, cfile);

		token = next_token (&val, cfile);
		if (token != NAME && token != NUMBER_OR_NAME) {
			parse_warn (cfile,
				    "%s can't be a variable name", val);
		      badset:
			skip_to_semi (cfile);
			*lose = 1;
			return 0;
		}

		if (!executable_statement_allocate (result, MDL))
			log_fatal ("no memory for set statement.");
		(*result) -> op = set_statement;
		(*result) -> data.set.name = dmalloc (strlen (val) + 1, MDL);
		if (!(*result)->data.set.name)
			log_fatal ("can't allocate variable name");
		strcpy ((*result) -> data.set.name, val);
		token = next_token (&val, cfile);
		if (token != EQUAL) {
			parse_warn (cfile, "expecting '=' in set statement.");
			goto badset;
		}

		if (!parse_expression (&(*result) -> data.set.expr,
				       cfile, lose, context_data, /* XXX */
				       (struct expression **)0, expr_none)) {
			if (!*lose)
				parse_warn (cfile,
					    "expecting data expression.");
			else
				*lose = 1;
			skip_to_semi (cfile);
			executable_statement_dereference (result, MDL);
			return 0;
		}
		parse_semi (cfile);
		break;

	      case UNSET:
		token = next_token (&val, cfile);

		token = next_token (&val, cfile);
		if (token != NAME && token != NUMBER_OR_NAME) {
			parse_warn (cfile,
				    "%s can't be a variable name", val);
		      badunset:
			skip_to_semi (cfile);
			*lose = 1;
			return 0;
		}

		if (!executable_statement_allocate (result, MDL))
			log_fatal ("no memory for set statement.");
		(*result) -> op = unset_statement;
		(*result) -> data.unset = dmalloc (strlen (val) + 1, MDL);
		if (!(*result)->data.unset)
			log_fatal ("can't allocate variable name");
		strcpy ((*result) -> data.unset, val);
		parse_semi (cfile);
		break;

	      case EVAL:
		token = next_token (&val, cfile);

		if (!executable_statement_allocate (result, MDL))
			log_fatal ("no memory for eval statement.");
		(*result) -> op = eval_statement;

		if (!parse_expression (&(*result) -> data.eval,
				       cfile, lose, context_data, /* XXX */
				       (struct expression **)0, expr_none)) {
			if (!*lose)
				parse_warn (cfile,
					    "expecting data expression.");
			else
				*lose = 1;
			skip_to_semi (cfile);
			executable_statement_dereference (result, MDL);
			return 0;
		}
		parse_semi (cfile);
		break;

	      default:
		if (config_universe && is_identifier (token)) {
			option = ((struct option *)
				  hash_lookup (config_universe -> hash,
					       (const unsigned char *)val, 0));
			if (option) {
				token = next_token (&val, cfile);
				return parse_option_statement
					(result, cfile, 1, option,
					 supersede_option_statement);
			}
		}
		*lose = 0;
		return 0;
	}

	return 1;
}

/*
 * on-statement :== event-types LBRACE executable-statements RBRACE
 * event-types :== event-type OR event-types |
 *		   event-type
 * event-type :== EXPIRY | COMMIT | RELEASE
 */

int parse_on_statement (result, cfile, lose)
	struct executable_statement **result;
	struct parse *cfile;
	int *lose;
{
	enum dhcp_token token;
	const char *val;

	if (!executable_statement_allocate (result, MDL))
		log_fatal ("no memory for new statement.");
	(*result) -> op = on_statement;

	do {
		token = next_token (&val, cfile);
		switch (token) {
		      case EXPIRY:
			(*result) -> data.on.evtypes |= ON_EXPIRY;
			break;
		
		      case COMMIT:
			(*result) -> data.on.evtypes |= ON_COMMIT;
			break;
			
		      case RELEASE:
			(*result) -> data.on.evtypes |= ON_RELEASE;
			break;
			
		      default:
			parse_warn (cfile, "expecting a lease event type");
			skip_to_semi (cfile);
			*lose = 1;
			executable_statement_dereference (result, MDL);
			return 0;
		}
		token = next_token (&val, cfile);
	} while (token == OR);
		
	/* Semicolon means no statements. */
	if (token == SEMI)
		return 1;

	if (token != LBRACE) {
		parse_warn (cfile, "left brace expected.");
		skip_to_semi (cfile);
		*lose = 1;
		executable_statement_dereference (result, MDL);
		return 0;
	}
	if (!parse_executable_statements (&(*result) -> data.on.statements,
					  cfile, lose, context_any)) {
		if (*lose) {
			/* Try to even things up. */
			do {
				token = next_token (&val, cfile);
			} while (token != EOF && token != RBRACE);
			executable_statement_dereference (result, MDL);
			return 0;
		}
	}
	token = next_token (&val, cfile);
	if (token != RBRACE) {
		parse_warn (cfile, "right brace expected.");
		skip_to_semi (cfile);
		*lose = 1;
		executable_statement_dereference (result, MDL);
		return 0;
	}
	return 1;
}

/*
 * switch-statement :== LPAREN expr RPAREN LBRACE executable-statements RBRACE
 *
 */

int parse_switch_statement (result, cfile, lose)
	struct executable_statement **result;
	struct parse *cfile;
	int *lose;
{
	enum dhcp_token token;
	const char *val;

	if (!executable_statement_allocate (result, MDL))
		log_fatal ("no memory for new statement.");
	(*result) -> op = switch_statement;

	token = next_token (&val, cfile);
	if (token != LPAREN) {
		parse_warn (cfile, "expecting left brace.");
	      pfui:
		*lose = 1;
		skip_to_semi (cfile);
	      gnorf:
		executable_statement_dereference (result, MDL);
		return 0;
	}

	if (!parse_expression (&(*result) -> data.s_switch.expr,
			       cfile, lose, context_data_or_numeric,
			       (struct expression **)0, expr_none)) {
		if (!*lose) {
			parse_warn (cfile,
				    "expecting data or numeric expression.");
			goto pfui;
		}
		goto gnorf;
	}

	token = next_token (&val, cfile);
	if (token != RPAREN) {
		parse_warn (cfile, "right paren expected.");
		goto pfui;
	}

	token = next_token (&val, cfile);
	if (token != LBRACE) {
		parse_warn (cfile, "left brace expected.");
		goto pfui;
	}
	if (!(parse_executable_statements
	      (&(*result) -> data.s_switch.statements, cfile, lose,
	       (is_data_expression ((*result) -> data.s_switch.expr)
		? context_data : context_numeric)))) {
		if (*lose) {
			skip_to_rbrace (cfile, 1);
			executable_statement_dereference (result, MDL);
			return 0;
		}
	}
	token = next_token (&val, cfile);
	if (token != RBRACE) {
		parse_warn (cfile, "right brace expected.");
		goto pfui;
	}
	return 1;
}

/*
 * case-statement :== CASE expr COLON
 *
 */

int parse_case_statement (result, cfile, lose, case_context)
	struct executable_statement **result;
	struct parse *cfile;
	int *lose;
	enum expression_context case_context;
{
	enum dhcp_token token;
	const char *val;

	if (!executable_statement_allocate (result, MDL))
		log_fatal ("no memory for new statement.");
	(*result) -> op = case_statement;

	if (!parse_expression (&(*result) -> data.c_case,
			       cfile, lose, case_context,
			       (struct expression **)0, expr_none))
	{
		if (!*lose) {
			parse_warn (cfile, "expecting %s expression.",
				    (case_context == context_data
				     ? "data" : "numeric"));
		}
	      pfui:
		*lose = 1;
		skip_to_semi (cfile);
		executable_statement_dereference (result, MDL);
		return 0;
	}

	token = next_token (&val, cfile);
	if (token != COLON) {
		parse_warn (cfile, "colon expected.");
		goto pfui;
	}
	return 1;
}

/*
 * if-statement :== boolean-expression LBRACE executable-statements RBRACE
 *						else-statement
 *
 * else-statement :== <null> |
 *		      ELSE LBRACE executable-statements RBRACE |
 *		      ELSE IF if-statement |
 *		      ELSIF if-statement
 */

int parse_if_statement (result, cfile, lose)
	struct executable_statement **result;
	struct parse *cfile;
	int *lose;
{
	enum dhcp_token token;
	const char *val;
	int parenp;

	if (!executable_statement_allocate (result, MDL))
		log_fatal ("no memory for if statement.");

	(*result) -> op = if_statement;

	token = peek_token (&val, cfile);
	if (token == LPAREN) {
		parenp = 1;
		next_token (&val, cfile);
	} else
		parenp = 0;


	if (!parse_boolean_expression (&(*result) -> data.ie.expr,
				       cfile, lose)) {
		if (!*lose)
			parse_warn (cfile, "boolean expression expected.");
		executable_statement_dereference (result, MDL);
		*lose = 1;
		return 0;
	}
#if defined (DEBUG_EXPRESSION_PARSE)
	print_expression ("if condition", if_condition);
#endif
	if (parenp) {
		token = next_token (&val, cfile);
		if (token != RPAREN) {
			parse_warn (cfile, "expecting right paren.");
			*lose = 1;
			executable_statement_dereference (result, MDL);
			return 0;
		}
	}
	token = next_token (&val, cfile);
	if (token != LBRACE) {
		parse_warn (cfile, "left brace expected.");
		skip_to_semi (cfile);
		*lose = 1;
		executable_statement_dereference (result, MDL);
		return 0;
	}
	if (!parse_executable_statements (&(*result) -> data.ie.true,
					  cfile, lose, context_any)) {
		if (*lose) {
			/* Try to even things up. */
			do {
				token = next_token (&val, cfile);
			} while (token != EOF && token != RBRACE);
			executable_statement_dereference (result, MDL);
			return 0;
		}
	}
	token = next_token (&val, cfile);
	if (token != RBRACE) {
		parse_warn (cfile, "right brace expected.");
		skip_to_semi (cfile);
		*lose = 1;
		executable_statement_dereference (result, MDL);
		return 0;
	}
	token = peek_token (&val, cfile);
	if (token == ELSE) {
		token = next_token (&val, cfile);
		token = peek_token (&val, cfile);
		if (token == IF) {
			token = next_token (&val, cfile);
			if (!parse_if_statement (&(*result) -> data.ie.false,
						 cfile, lose)) {
				if (!*lose)
					parse_warn (cfile,
						    "expecting if statement");
				executable_statement_dereference (result, MDL);
				*lose = 1;
				return 0;
			}
		} else if (token != LBRACE) {
			parse_warn (cfile, "left brace or if expected.");
			skip_to_semi (cfile);
			*lose = 1;
			executable_statement_dereference (result, MDL);
			return 0;
		} else {
			token = next_token (&val, cfile);
			if (!(parse_executable_statements
			      (&(*result) -> data.ie.false,
			       cfile, lose, context_any))) {
				executable_statement_dereference (result, MDL);
				return 0;
			}
			token = next_token (&val, cfile);
			if (token != RBRACE) {
				parse_warn (cfile, "right brace expected.");
				skip_to_semi (cfile);
				*lose = 1;
				executable_statement_dereference (result, MDL);
				return 0;
			}
		}
	} else if (token == ELSIF) {
		token = next_token (&val, cfile);
		if (!parse_if_statement (&(*result) -> data.ie.false,
					 cfile, lose)) {
			if (!*lose)
				parse_warn (cfile,
					    "expecting conditional.");
			executable_statement_dereference (result, MDL);
			*lose = 1;
			return 0;
		}
	} else
		(*result) -> data.ie.false = (struct executable_statement *)0;
	
	return 1;
}

/*
 * boolean_expression :== CHECK STRING |
 *  			  NOT boolean-expression |
 *			  data-expression EQUAL data-expression |
 *			  data-expression BANG EQUAL data-expression |
 *			  boolean-expression AND boolean-expression |
 *			  boolean-expression OR boolean-expression
 *			  EXISTS OPTION-NAME
 */
   			  
int parse_boolean_expression (expr, cfile, lose)
	struct expression **expr;
	struct parse *cfile;
	int *lose;
{
	/* Parse an expression... */
	if (!parse_expression (expr, cfile, lose, context_boolean,
			       (struct expression **)0, expr_none))
		return 0;

	if (!is_boolean_expression (*expr) &&
	    (*expr) -> op != expr_variable_reference &&
	    (*expr) -> op != expr_funcall) {
		parse_warn (cfile, "Expecting a boolean expression.");
		*lose = 1;
		expression_dereference (expr, MDL);
		return 0;
	}
	return 1;
}

/*
 * data_expression :== SUBSTRING LPAREN data-expression COMMA
 *					numeric-expression COMMA
 *					numeric-expression RPAREN |
 *		       CONCAT LPAREN data-expression COMMA 
					data-expression RPAREN
 *		       SUFFIX LPAREN data_expression COMMA
 *		       		     numeric-expression RPAREN |
 *		       OPTION option_name |
 *		       HARDWARE |
 *		       PACKET LPAREN numeric-expression COMMA
 *				     numeric-expression RPAREN |
 *		       STRING |
 *		       colon_seperated_hex_list
 */

int parse_data_expression (expr, cfile, lose)
	struct expression **expr;
	struct parse *cfile;
	int *lose;
{
	/* Parse an expression... */
	if (!parse_expression (expr, cfile, lose, context_data,
			       (struct expression **)0, expr_none))
		return 0;

	if (!is_data_expression (*expr) &&
	    (*expr) -> op != expr_variable_reference &&
	    (*expr) -> op != expr_funcall) {
		parse_warn (cfile, "Expecting a data expression.");
		*lose = 1;
		return 0;
	}
	return 1;
}

/*
 * numeric-expression :== EXTRACT_INT LPAREN data-expression
 *					     COMMA number RPAREN |
 *			  NUMBER
 */

int parse_numeric_expression (expr, cfile, lose)
	struct expression **expr;
	struct parse *cfile;
	int *lose;
{
	/* Parse an expression... */
	if (!parse_expression (expr, cfile, lose, context_numeric,
			       (struct expression **)0, expr_none))
		return 0;

	if (!is_numeric_expression (*expr) &&
	    (*expr) -> op != expr_variable_reference &&
	    (*expr) -> op != expr_funcall) {
		parse_warn (cfile, "Expecting a numeric expression.");
		*lose = 1;
		return 0;
	}
	return 1;
}

/*
 * dns-expression :==
 *	UPDATE LPAREN ns-class COMMA ns-type COMMA data-expression COMMA
 *				data-expression COMMA numeric-expression RPAREN
 *	DELETE LPAREN ns-class COMMA ns-type COMMA data-expression COMMA
 *				data-expression RPAREN
 *	EXISTS LPAREN ns-class COMMA ns-type COMMA data-expression COMMA
 *				data-expression RPAREN
 *	NOT EXISTS LPAREN ns-class COMMA ns-type COMMA data-expression COMMA
 *				data-expression RPAREN
 * ns-class :== IN | CHAOS | HS | NUMBER
 * ns-type :== A | PTR | MX | TXT | NUMBER
 */

int parse_dns_expression (expr, cfile, lose)
	struct expression **expr;
	struct parse *cfile;
	int *lose;
{
	/* Parse an expression... */
	if (!parse_expression (expr, cfile, lose, context_dns,
			       (struct expression **)0, expr_none))
		return 0;

	if (!is_dns_expression (*expr) &&
	    (*expr) -> op != expr_variable_reference &&
	    (*expr) -> op != expr_funcall) {
		parse_warn (cfile, "Expecting a dns update subexpression.");
		*lose = 1;
		return 0;
	}
	return 1;
}

/* Parse a subexpression that does not contain a binary operator. */

int parse_non_binary (expr, cfile, lose, context)
	struct expression **expr;
	struct parse *cfile;
	int *lose;
	enum expression_context context;
{
	enum dhcp_token token;
	const char *val;
	struct collection *col;
	struct option *option;
	struct expression *nexp, **ep;
	int known;
	enum expr_op opcode;
	const char *s;
	char *cptr;
	struct executable_statement *stmt;
	int i;
	unsigned long u;

	token = peek_token (&val, cfile);

	/* Check for unary operators... */
	switch (token) {
	      case CHECK:
		token = next_token (&val, cfile);
		token = next_token (&val, cfile);
		if (token != STRING) {
			parse_warn (cfile, "string expected.");
			skip_to_semi (cfile);
			*lose = 1;
			return 0;
		}
		for (col = collections; col; col = col -> next)
			if (!strcmp (col -> name, val))
				break;
		if (!col) {
			parse_warn (cfile, "unknown collection.");
			*lose = 1;
			return 0;
		}
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_check;
		(*expr) -> data.check = col;
		break;

	      case TOKEN_NOT:
		token = next_token (&val, cfile);
		if (context == context_dns) {
			token = peek_token (&val, cfile);
			goto not_exists;
		}
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_not;
		if (!parse_non_binary (&(*expr) -> data.not,
				       cfile, lose, context)) {
			if (!*lose) {
				parse_warn (cfile, "expression expected");
				skip_to_semi (cfile);
			}
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}
		break;

	      case EXISTS:
		if (context == context_dns)
			goto ns_exists;
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_exists;
		known = 0;
		(*expr) -> data.option = parse_option_name (cfile, 0, &known);
		if (!(*expr) -> data.option) {
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}
		break;

	      case STATIC:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_static;
		break;

	      case KNOWN:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_known;
		break;

	      case SUBSTRING:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_substring;

		token = next_token (&val, cfile);
		if (token != LPAREN) {
		      nolparen:
			expression_dereference (expr, MDL);
			parse_warn (cfile, "left parenthesis expected.");
			*lose = 1;
			return 0;
		}

		if (!parse_data_expression (&(*expr) -> data.substring.expr,
					    cfile, lose)) {
		      nodata:
			expression_dereference (expr, MDL);
			parse_warn (cfile, "expecting data expression.");
			skip_to_semi (cfile);
			*lose = 1;
			return 0;
		}

		token = next_token (&val, cfile);
		if (token != COMMA) {
		      nocomma:
			expression_dereference (expr, MDL);
			parse_warn (cfile, "comma expected.");
			*lose = 1;

			return 0;
		}

		if (!parse_numeric_expression
		    (&(*expr) -> data.substring.offset,cfile, lose)) {
		      nonum:
			if (!*lose) {
				parse_warn (cfile,
					    "expecting numeric expression.");
				skip_to_semi (cfile);
				*lose = 1;
			}
			expression_dereference (expr, MDL);
			return 0;
		}

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		if (!parse_numeric_expression
		    (&(*expr) -> data.substring.len, cfile, lose))
			goto nonum;

		token = next_token (&val, cfile);
		if (token != RPAREN) {
		      norparen:
			parse_warn (cfile, "right parenthesis expected.");
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}
		break;

	      case SUFFIX:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_suffix;

		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		if (!parse_data_expression (&(*expr) -> data.suffix.expr,
					    cfile, lose))
			goto nodata;

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		if (!parse_data_expression (&(*expr) -> data.suffix.len,
					    cfile, lose))
			goto nonum;

		token = next_token (&val, cfile);
		if (token != RPAREN)
			goto norparen;
		break;

	      case CONCAT:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_concat;

		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		if (!parse_data_expression (&(*expr) -> data.concat [0],
					    cfile, lose))
			goto nodata;

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

	      concat_another:
		if (!parse_data_expression (&(*expr) -> data.concat [1],
					    cfile, lose))
			goto nodata;

		token = next_token (&val, cfile);

		if (token == COMMA) {
			nexp = (struct expression *)0;
			if (!expression_allocate (&nexp, MDL))
				log_fatal ("can't allocate at CONCAT2");
			nexp -> op = expr_concat;
			expression_reference (&nexp -> data.concat [0],
					      *expr, MDL);
			expression_dereference (expr, MDL);
			expression_reference (expr, nexp, MDL);
			goto concat_another;
		}

		if (token != RPAREN)
			goto norparen;
		break;

	      case BINARY_TO_ASCII:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_binary_to_ascii;

		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		if (!parse_numeric_expression (&(*expr) -> data.b2a.base,
					       cfile, lose))
			goto nodata;

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		if (!parse_numeric_expression (&(*expr) -> data.b2a.width,
					       cfile, lose))
			goto nodata;

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		if (!parse_data_expression (&(*expr) -> data.b2a.seperator,
					    cfile, lose))
			goto nodata;

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		if (!parse_data_expression (&(*expr) -> data.b2a.buffer,
					    cfile, lose))
			goto nodata;

		token = next_token (&val, cfile);
		if (token != RPAREN)
			goto norparen;
		break;

	      case REVERSE:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_reverse;

		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		if (!(parse_numeric_expression
		      (&(*expr) -> data.reverse.width, cfile, lose)))
			goto nodata;

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		if (!(parse_data_expression
		      (&(*expr) -> data.reverse.buffer, cfile, lose)))
			goto nodata;

		token = next_token (&val, cfile);
		if (token != RPAREN)
			goto norparen;
		break;

	      case PICK:
		/* pick (a, b, c) actually produces an internal representation
		   that looks like pick (a, pick (b, pick (c, nil))). */
		token = next_token (&val, cfile);
		if (!(expression_allocate (expr, MDL)))
			log_fatal ("can't allocate expression");

		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		nexp = *expr;
		do {
			nexp -> op = expr_pick_first_value;
			if (!(parse_data_expression
			      (&nexp -> data.pick_first_value.car,
			       cfile, lose)))
				goto nodata;

			token = next_token (&val, cfile);
			if (token == COMMA) {
				if (!(expression_allocate
				      (&nexp -> data.pick_first_value.cdr,
				       MDL)))
					log_fatal ("can't allocate expr");
				nexp = nexp -> data.pick_first_value.cdr;
			}
		} while (token == COMMA);

		if (token != RPAREN)
			goto norparen;
		break;

		/* dns-update and dns-delete are present for historical
		   purposes, but are deprecated in favor of ns-update
		   in combination with update, delete, exists and not
		   exists. */
	      case DNS_UPDATE:
	      case DNS_DELETE:
#if !defined (NSUPDATE)
		parse_warn (cfile,
			    "Please rebuild dhcpd with --with-nsupdate.");
#endif
		token = next_token (&val, cfile);
		if (token == DNS_UPDATE)
			opcode = expr_ns_add;
		else
			opcode = expr_ns_delete;

		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		token = next_token (&val, cfile);
		if (token != STRING) {
			parse_warn (cfile,
				    "parse_expression: expecting string.");
		      badnsupdate:
			skip_to_semi (cfile);
			*lose = 1;
			return 0;
		}
			
		if (!strcasecmp (val, "a"))
			u = T_A;
		else if (!strcasecmp (val, "ptr"))
			u = T_PTR;
		else if (!strcasecmp (val, "mx"))
			u = T_MX;
		else if (!strcasecmp (val, "cname"))
			u = T_CNAME;
		else if (!strcasecmp (val, "TXT"))
			u = T_TXT;
		else {
			parse_warn (cfile, "unexpected rrtype: %s", val);
			goto badnsupdate;
		}

		s = (opcode == expr_ns_add
		     ? "old-dns-update"
		     : "old-dns-delete");
		cptr = dmalloc (strlen (s) + 1, MDL);
		if (!cptr)
			log_fatal ("can't allocate name for %s", s);
		strcpy (cptr, s);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_funcall;
		(*expr) -> data.funcall.name = cptr;

		/* Fake up a function call. */
		ep = &(*expr) -> data.funcall.arglist;
		if (!expression_allocate (ep, MDL))
			log_fatal ("can't allocate expression");
		(*ep) -> op = expr_arg;
		if (!make_const_int (&(*ep) -> data.arg.val, u))
			log_fatal ("can't allocate rrtype value.");

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;
		ep = &((*ep) -> data.arg.next);
		if (!expression_allocate (ep, MDL))
			log_fatal ("can't allocate expression");
		(*ep) -> op = expr_arg;
		if (!(parse_data_expression (&(*ep) -> data.arg.val,
					     cfile, lose)))
			goto nodata;

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		ep = &((*ep) -> data.arg.next);
		if (!expression_allocate (ep, MDL))
			log_fatal ("can't allocate expression");
		(*ep) -> op = expr_arg;
		if (!(parse_data_expression (&(*ep) -> data.arg.val,
					     cfile, lose)))
			goto nodata;

		if (opcode == expr_ns_add) {
			token = next_token (&val, cfile);
			if (token != COMMA)
				goto nocomma;
			
			ep = &((*ep) -> data.arg.next);
			if (!expression_allocate (ep, MDL))
				log_fatal ("can't allocate expression");
			(*ep) -> op = expr_arg;
			if (!(parse_numeric_expression (&(*ep) -> data.arg.val,
							cfile, lose))) {
				parse_warn (cfile,
					    "expecting numeric expression.");
				goto badnsupdate;
			}
		}

		token = next_token (&val, cfile);
		if (token != RPAREN)
			goto norparen;
		break;

	      case NS_UPDATE:
#if !defined (NSUPDATE)
		parse_warn (cfile,
			    "Please rebuild dhcpd with --with-nsupdate.");
#endif
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");

		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		nexp = *expr;
		do {
			nexp -> op = expr_dns_transaction;
			if (!(parse_dns_expression
			      (&nexp -> data.dns_transaction.car,
			       cfile, lose)))
			{
				if (!*lose)
					parse_warn
						(cfile,
						 "expecting dns expression.");
			      badnstrans:
				expression_dereference (expr, MDL);
				*lose = 1;
				return 0;
			}

			token = next_token (&val, cfile);
			
			if (token == COMMA) {
				if (!(expression_allocate
				      (&nexp -> data.dns_transaction.cdr,
				       MDL)))
					log_fatal
						("can't allocate expression");
				nexp = nexp -> data.dns_transaction.cdr;
			}
		} while (token == COMMA);

		if (token != RPAREN)
			goto norparen;
		break;

		/* NOT EXISTS is special cased above... */
	      not_exists:
		token = peek_token (&val, cfile);
		if (token != EXISTS) {
			parse_warn (cfile, "expecting DNS prerequisite.");
			*lose = 1;
			return 0;
		}
		opcode = expr_ns_not_exists;
		goto nsupdatecode;
	      case TOKEN_ADD:
		opcode = expr_ns_add;
		goto nsupdatecode;
	      case TOKEN_DELETE:
		opcode = expr_ns_delete;
		goto nsupdatecode;
	      ns_exists:
		opcode = expr_ns_exists;
	      nsupdatecode:
		token = next_token (&val, cfile);

#if !defined (NSUPDATE)
		parse_warn (cfile,
			    "Please rebuild dhcpd with --with-nsupdate.");
#endif
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = opcode;

		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		token = next_token (&val, cfile);
		if (!is_identifier (token) && token != NUMBER) {
			parse_warn (cfile, "expecting identifier or number.");
		      badnsop:
			expression_dereference (expr, MDL);
			skip_to_semi (cfile);
			*lose = 1;
			return 0;
		}
			
		if (token == NUMBER)
			(*expr) -> data.ns_add.rrclass = atoi (val);
		else if (!strcasecmp (val, "in"))
			(*expr) -> data.ns_add.rrclass = C_IN;
		else if (!strcasecmp (val, "chaos"))
			(*expr) -> data.ns_add.rrclass = C_CHAOS;
		else if (!strcasecmp (val, "hs"))
			(*expr) -> data.ns_add.rrclass = C_HS;
		else {
			parse_warn (cfile, "unexpected rrclass: %s", val);
			goto badnsop;
		}
		
		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		token = next_token (&val, cfile);
		if (!is_identifier (token) && token != NUMBER) {
			parse_warn (cfile, "expecting identifier or number.");
			goto badnsop;
		}
			
		if (token == NUMBER)
			(*expr) -> data.ns_add.rrtype = atoi (val);
		else if (!strcasecmp (val, "a"))
			(*expr) -> data.ns_add.rrtype = T_A;
		else if (!strcasecmp (val, "ptr"))
			(*expr) -> data.ns_add.rrtype = T_PTR;
		else if (!strcasecmp (val, "mx"))
			(*expr) -> data.ns_add.rrtype = T_MX;
		else if (!strcasecmp (val, "cname"))
			(*expr) -> data.ns_add.rrtype = T_CNAME;
		else if (!strcasecmp (val, "TXT"))
			(*expr) -> data.ns_add.rrtype = T_TXT;
		else {
			parse_warn (cfile, "unexpected rrtype: %s", val);
			goto badnsop;
		}

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		if (!(parse_data_expression
		      (&(*expr) -> data.ns_add.rrname, cfile, lose)))
			goto nodata;

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		if (!(parse_data_expression
		      (&(*expr) -> data.ns_add.rrdata, cfile, lose)))
			goto nodata;

		if (opcode == expr_ns_add) {
			token = next_token (&val, cfile);
			if (token != COMMA)
				goto nocomma;
			
			if (!(parse_numeric_expression
			      (&(*expr) -> data.ns_add.ttl, cfile,
			       lose))) {
				parse_warn (cfile,
					    "expecting data expression.");
				goto badnsupdate;
			}
		}

		token = next_token (&val, cfile);
		if (token != RPAREN)
			goto norparen;
		break;

	      case OPTION:
	      case CONFIG_OPTION:
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = (token == OPTION
				 ? expr_option
				 : expr_config_option);
		token = next_token (&val, cfile);
		known = 0;
		(*expr) -> data.option = parse_option_name (cfile, 0, &known);
		if (!(*expr) -> data.option) {
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}
		break;

	      case HARDWARE:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_hardware;
		break;

	      case LEASED_ADDRESS:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_leased_address;
		break;

	      case FILENAME:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_filename;
		break;

	      case SERVER_NAME:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_sname;
		break;

	      case LEASE_TIME:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_lease_time;
		break;

	      case TOKEN_NULL:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_null;
		break;

	      case HOST_DECL_NAME:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_host_decl_name;
		break;

	      case UPDATED_DNS_RR:
		token = next_token (&val, cfile);

		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		token = next_token (&val, cfile);
		if (token != STRING) {
			parse_warn (cfile, "expecting string.");
		      bad_rrtype:
			*lose = 1;
			return 0;
		}
		if (!strcasecmp (val, "a"))
			s = "ddns-fwd-name";
		else if (!strcasecmp (val, "ptr"))
			s = "ddns-rev-name";
		else {
			parse_warn (cfile, "invalid DNS rrtype: %s", val);
			goto bad_rrtype;
		}

		token = next_token (&val, cfile);
		if (token != RPAREN)
			goto norparen;

		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_variable_reference;
		(*expr) -> data.variable =
			dmalloc (strlen (s) + 1, MDL);
		if (!(*expr) -> data.variable)
			log_fatal ("can't allocate variable name.");
		strcpy ((*expr) -> data.variable, s);
		break;

	      case PACKET:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_packet;

		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		if (!parse_numeric_expression (&(*expr) -> data.packet.offset,
					       cfile, lose))
			goto nonum;

		token = next_token (&val, cfile);
		if (token != COMMA)
			goto nocomma;

		if (!parse_numeric_expression (&(*expr) -> data.packet.len,
					       cfile, lose))
			goto nonum;

		token = next_token (&val, cfile);
		if (token != RPAREN)
			goto norparen;
		break;
		
	      case STRING:
		token = next_token (&val, cfile);
		if (!make_const_data (expr, (const unsigned char *)val,
				      strlen (val), 1, 1))
			log_fatal ("can't make constant string expression.");
		break;

	      case EXTRACT_INT:
		token = next_token (&val, cfile);	
		token = next_token (&val, cfile);
		if (token != LPAREN) {
			parse_warn (cfile, "left parenthesis expected.");
			*lose = 1;
			return 0;
		}

		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");

		if (!parse_data_expression (&(*expr) -> data.extract_int,
					    cfile, lose)) {
			parse_warn (cfile, "expecting data expression.");
			skip_to_semi (cfile);
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}

		token = next_token (&val, cfile);
		if (token != COMMA) {
			parse_warn (cfile, "comma expected.");
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}

		token = next_token (&val, cfile);
		if (token != NUMBER) {
			parse_warn (cfile, "number expected.");
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}
		switch (atoi (val)) {
		      case 8:
			(*expr) -> op = expr_extract_int8;
			break;

		      case 16:
			(*expr) -> op = expr_extract_int16;
			break;

		      case 32:
			(*expr) -> op = expr_extract_int32;
			break;

		      default:
			parse_warn (cfile,
				    "unsupported integer size %d", atoi (val));
			*lose = 1;
			skip_to_semi (cfile);
			expression_dereference (expr, MDL);
			return 0;
		}

		token = next_token (&val, cfile);
		if (token != RPAREN) {
			parse_warn (cfile, "right parenthesis expected.");
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}
		break;
	
	      case ENCODE_INT:
		token = next_token (&val, cfile);	
		token = next_token (&val, cfile);
		if (token != LPAREN) {
			parse_warn (cfile, "left parenthesis expected.");
			*lose = 1;
			return 0;
		}

		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");

		if (!parse_numeric_expression (&(*expr) -> data.encode_int,
					       cfile, lose)) {
			parse_warn (cfile, "expecting numeric expression.");
			skip_to_semi (cfile);
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}

		token = next_token (&val, cfile);
		if (token != COMMA) {
			parse_warn (cfile, "comma expected.");
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}

		token = next_token (&val, cfile);
		if (token != NUMBER) {
			parse_warn (cfile, "number expected.");
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}
		switch (atoi (val)) {
		      case 8:
			(*expr) -> op = expr_encode_int8;
			break;

		      case 16:
			(*expr) -> op = expr_encode_int16;
			break;

		      case 32:
			(*expr) -> op = expr_encode_int32;
			break;

		      default:
			parse_warn (cfile,
				    "unsupported integer size %d", atoi (val));
			*lose = 1;
			skip_to_semi (cfile);
			expression_dereference (expr, MDL);
			return 0;
		}

		token = next_token (&val, cfile);
		if (token != RPAREN) {
			parse_warn (cfile, "right parenthesis expected.");
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}
		break;
	
	      case NUMBER:
		/* If we're in a numeric context, this should just be a
		   number, by itself. */
		if (context == context_numeric ||
		    context == context_data_or_numeric) {
			next_token (&val, cfile);	/* Eat the number. */
			if (!expression_allocate (expr, MDL))
				log_fatal ("can't allocate expression");
			(*expr) -> op = expr_const_int;
			(*expr) -> data.const_int = atoi (val);
			break;
		}

	      case NUMBER_OR_NAME:
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");

		(*expr) -> op = expr_const_data;
		if (!parse_cshl (&(*expr) -> data.const_data, cfile)) {
			expression_dereference (expr, MDL);
			return 0;
		}
		break;

	      case NS_FORMERR:
		known = FORMERR;
	      ns_const:
		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_const_int;
		(*expr) -> data.const_int = known;
		break;
		
	      case NS_NOERROR:
		known = NOERROR;
		goto ns_const;

	      case NS_NOTAUTH:
		known = NOTAUTH;
		goto ns_const;

	      case NS_NOTIMP:
		known = NOTIMP;
		goto ns_const;

	      case NS_NOTZONE:
		known = NOTZONE;
		goto ns_const;

	      case NS_NXDOMAIN:
		known = NXDOMAIN;
		goto ns_const;

	      case NS_NXRRSET:
		known = NXRRSET;
		goto ns_const;

	      case NS_REFUSED:
		known = REFUSED;
		goto ns_const;

	      case NS_SERVFAIL:
		known = SERVFAIL;
		goto ns_const;

	      case NS_YXDOMAIN:
		known = YXDOMAIN;
		goto ns_const;

	      case NS_YXRRSET:
		known = YXRRSET;
		goto ns_const;

	      case DEFINED:
		token = next_token (&val, cfile);
		token = next_token (&val, cfile);
		if (token != LPAREN)
			goto nolparen;

		token = next_token (&val, cfile);
		if (token != NAME && token != NUMBER_OR_NAME) {
			parse_warn (cfile, "%s can't be a variable name", val);
			skip_to_semi (cfile);
			*lose = 1;
			return 0;
		}

		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_variable_exists;
		(*expr) -> data.variable = dmalloc (strlen (val) + 1, MDL);
		if (!(*expr)->data.variable)
			log_fatal ("can't allocate variable name");
		strcpy ((*expr) -> data.variable, val);
		token = next_token (&val, cfile);
		if (token != RPAREN)
			goto norparen;
		break;

		/* Not a valid start to an expression... */
	      default:
		if (token != NAME && token != NUMBER_OR_NAME)
			return 0;

		token = next_token (&val, cfile);

		/* Save the name of the variable being referenced. */
		cptr = dmalloc (strlen (val) + 1, MDL);
		if (!cptr)
			log_fatal ("can't allocate variable name");
		strcpy (cptr, val);

		/* Simple variable reference, as far as we can tell. */
		token = peek_token (&val, cfile);
		if (token != LPAREN) {
			if (!expression_allocate (expr, MDL))
				log_fatal ("can't allocate expression");
			(*expr) -> op = expr_variable_reference;
			(*expr) -> data.variable = cptr;
			break;
		}

		token = next_token (&val, cfile);
		if (!expression_allocate (expr, MDL))
			log_fatal ("can't allocate expression");
		(*expr) -> op = expr_funcall;
		(*expr) -> data.funcall.name = cptr;

		/* Now parse the argument list. */
		ep = &(*expr) -> data.funcall.arglist;
		do {
			if (!expression_allocate (ep, MDL))
				log_fatal ("can't allocate expression");
			(*ep) -> op = expr_arg;
			if (!parse_expression (&(*ep) -> data.arg.val,
					       cfile, lose, context_any,
					       (struct expression **)0,
					       expr_none)) {
				if (!*lose) {
					parse_warn (cfile,
						    "expecting expression.");
					*lose = 1;
				}
				skip_to_semi (cfile);
				expression_dereference (expr, MDL);
				return 0;
			}
			ep = &((*ep) -> data.arg.next);
			token = next_token (&val, cfile);
		} while (token == COMMA);
		if (token != RPAREN) {
			parse_warn (cfile, "Right parenthesis expected.");
			skip_to_semi (cfile);
			*lose = 1;
			expression_dereference (expr, MDL);
			return 0;
		}
		break;
	}
	return 1;
}

/* Parse an expression. */

int parse_expression (expr, cfile, lose, context, plhs, binop)
	struct expression **expr;
	struct parse *cfile;
	int *lose;
	enum expression_context context;
	struct expression **plhs;
	enum expr_op binop;
{
	enum dhcp_token token;
	const char *val;
	struct expression *rhs = (struct expression *)0, *tmp;
	struct expression *lhs;
	enum expr_op next_op;

	/* Consume the left hand side we were passed. */
	if (plhs) {
		lhs = *plhs;
		*plhs = (struct expression *)0;
	} else
		lhs = (struct expression *)0;

      new_rhs:
	if (!parse_non_binary (&rhs, cfile, lose, context)) {
		/* If we already have a left-hand side, then it's not
		   okay for there not to be a right-hand side here, so
		   we need to flag it as an error. */
		if (lhs) {
			if (!*lose) {
				parse_warn (cfile,
					    "expecting right-hand side.");
				*lose = 1;
				skip_to_semi (cfile);
			}
			expression_dereference (&lhs, MDL);
		}
		return 0;
	}

	/* At this point, rhs contains either an entire subexpression,
	   or at least a left-hand-side.   If we do not see a binary token
	   as the next token, we're done with the expression. */

	token = peek_token (&val, cfile);
	switch (token) {
	      case BANG:
		token = next_token (&val, cfile);
		token = peek_token (&val, cfile);
		if (token != EQUAL) {
			parse_warn (cfile, "! in boolean context without =");
			*lose = 1;
			skip_to_semi (cfile);
			if (lhs)
				expression_dereference (&lhs, MDL);
			return 0;
		}
		next_op = expr_not_equal;
		break;

	      case EQUAL:
		next_op = expr_equal;
		break;

	      case AND:
		next_op = expr_and;
		break;

	      case OR:
		next_op = expr_or;
		break;

	      default:
		next_op = expr_none;
	}

	/* If we have no lhs yet, we just parsed it. */
	if (!lhs) {
		/* If there was no operator following what we just parsed,
		   then we're done - return it. */
		if (next_op == expr_none) {
			*expr = rhs;
			return 1;
		}
		lhs = rhs;
		rhs = (struct expression *)0;
		binop = next_op;
		next_token (&val, cfile);	/* Consume the operator. */
		goto new_rhs;
	}

	/* Now, if we didn't find a binary operator, we're done parsing
	   this subexpression, so combine it with the preceding binary
	   operator and return the result. */
	if (next_op == expr_none) {
		if (!expression_allocate (expr, MDL))
			log_fatal ("Can't allocate expression!");

		(*expr) -> op = binop;
		/* All the binary operators' data union members
		   are the same, so we'll cheat and use the member
		   for the equals operator. */
		(*expr) -> data.equal [0] = lhs;
		(*expr) -> data.equal [1] = rhs;
		return 1;
	}

	/* Eat the operator token - we now know it was a binary operator... */
	token = next_token (&val, cfile);

	/* If the binary operator we saw previously has a lower precedence
	   than the next operator, then the rhs we just parsed for that
	   operator is actually the lhs of the operator with the higher
	   precedence - to get the real rhs, we need to recurse on the
	   new operator. */
 	if (binop != expr_none &&
	    op_precedence (binop, next_op) < 0) {
		tmp = rhs;
		rhs = (struct expression *)0;
		if (!parse_expression (&rhs, cfile, lose, op_context (next_op),
				       &tmp, next_op)) {
			if (!*lose) {
				parse_warn (cfile,
					    "expecting a subexpression");
				*lose = 1;
			}
			return 0;
		}
		next_op = expr_none;
	}

	/* Now combine the LHS and the RHS using binop. */
	tmp = (struct expression *)0;
	if (!expression_allocate (&tmp, MDL))
		log_fatal ("No memory for equal precedence combination.");
	
	/* Store the LHS and RHS. */
	tmp -> data.equal [0] = lhs;
	tmp -> data.equal [1] = rhs;
	tmp -> op = binop;
	
	lhs = tmp;
	tmp = (struct expression *)0;
	rhs = (struct expression *)0;

	/* Recursions don't return until we have parsed the end of the
	   expression, so if we recursed earlier, we can now return what
	   we got. */
	if (next_op == expr_none) {
		*expr = lhs;
		return 1;
	}

	binop = next_op;
	goto new_rhs;
}	

/* option-statement :== identifier DOT identifier <syntax> SEMI
		      | identifier <syntax> SEMI

   Option syntax is handled specially through format strings, so it
   would be painful to come up with BNF for it.   However, it always
   starts as above and ends in a SEMI. */

int parse_option_statement (result, cfile, lookups, option, op)
	struct executable_statement **result;
	struct parse *cfile;
	int lookups;
	struct option *option;
	enum statement_op op;
{
	const char *val;
	enum dhcp_token token;
	const char *fmt;
	struct expression *expr = (struct expression *)0;
	struct expression *tmp;
	int lose;
	struct executable_statement *stmt;
	int ftt = 1;

	token = peek_token (&val, cfile);
	if (token == SEMI) {
		/* Eat the semicolon... */
		token = next_token (&val, cfile);
		goto done;
	}

	if (token == EQUAL) {
		/* Eat the equals sign. */
		token = next_token (&val, cfile);

		/* Parse a data expression and use its value for the data. */
		if (!parse_data_expression (&expr, cfile, &lose)) {
			/* In this context, we must have an executable
			   statement, so if we found something else, it's
			   still an error. */
			if (!lose) {
				parse_warn (cfile,
					    "expecting a data expression.");
				skip_to_semi (cfile);
			}
			return 0;
		}

		/* We got a valid expression, so use it. */
		goto done;
	}

	/* Parse the option data... */
	do {
		/* Set a flag if this is an array of a simple type (i.e.,
		   not an array of pairs of IP addresses, or something
		   like that. */
		int uniform = option -> format [1] == 'A';

		for (fmt = option -> format; *fmt; fmt++) {
			if (*fmt == 'A')
				break;
			tmp = expr;
			expr = (struct expression *)0;
			if (!parse_option_token (&expr, cfile, fmt,
						 tmp, uniform, lookups)) {
				if (tmp)
					expression_dereference (&tmp, MDL);
				return 0;
			}
			if (tmp)
				expression_dereference (&tmp, MDL);
		}
		if (*fmt == 'A') {
			token = peek_token (&val, cfile);
			if (token == COMMA) {
				token = next_token (&val, cfile);
				continue;
			}
			break;
		}
	} while (*fmt == 'A');

      done:
	if (!parse_semi (cfile))
		return 0;
	if (!executable_statement_allocate (result, MDL))
		log_fatal ("no memory for option statement.");
	(*result) -> op = op;
	if (expr && !option_cache (&(*result) -> data.option,
				   (struct data_string *)0, expr, option))
		log_fatal ("no memory for option cache");
	return 1;
}

int parse_option_token (rv, cfile, fmt, expr, uniform, lookups)
	struct expression **rv;
	struct parse *cfile;
	const char *fmt;
	struct expression *expr;
	int uniform;
	int lookups;
{
	const char *val;
	enum dhcp_token token;
	struct expression *t = (struct expression *)0;
	unsigned char buf [4];
	int len;
	unsigned char *ob;
	struct iaddr addr;
	int num;

	switch (*fmt) {
	      case 'U':
		token = next_token (&val, cfile);
		if (!is_identifier (token)) {
			parse_warn (cfile, "expecting identifier.");
			skip_to_semi (cfile);
			return 0;
		}
		if (!make_const_data (&t, (const unsigned char *)val,
				      strlen (val), 1, 1))
			log_fatal ("No memory for %s", val);
		break;

	      case 'X':
		token = peek_token (&val, cfile);
		if (token == NUMBER_OR_NAME || token == NUMBER) {
			if (!expression_allocate (&t, MDL))
				return 0;
			if (!parse_cshl (&t -> data.const_data, cfile)) {
				expression_dereference (&t, MDL);
				return 0;
			}
			t -> op = expr_const_data;
		} else if (token == STRING) {
			token = next_token (&val, cfile);
			if (!make_const_data (&t, (const unsigned char *)val,
					      strlen (val), 1, 1))
				log_fatal ("No memory for \"%s\"", val);
		} else {
			parse_warn (cfile, "expecting string %s.",
				    "or hexadecimal data");
			skip_to_semi (cfile);
			return 0;
		}
		break;
		
	      case 't': /* Text string... */
		token = next_token (&val, cfile);
		if (token != STRING && !is_identifier (token)) {
			parse_warn (cfile, "expecting string.");
			if (token != SEMI)
				skip_to_semi (cfile);
			return 0;
		}
		if (!make_const_data (&t, (const unsigned char *)val,
				      strlen (val), 1, 1))
			log_fatal ("No memory for concatenation");
		break;
		
	      case 'I': /* IP address or hostname. */
		if (lookups) {
			if (!parse_ip_addr_or_hostname (&t, cfile, uniform))
				return 0;
		} else {
			if (!parse_ip_addr (cfile, &addr))
				return 0;
			if (!make_const_data (&t, addr.iabuf, addr.len, 0, 1))
				return 0;
		}
		break;
		
	      case 'T':	/* Lease interval. */
		token = next_token (&val, cfile);
		if (token != INFINITE)
			goto check_number;
		putLong (buf, -1);
		if (!make_const_data (&t, buf, 4, 0, 1))
			return 0;
		break;

	      case 'L': /* Unsigned 32-bit integer... */
	      case 'l':	/* Signed 32-bit integer... */
		token = next_token (&val, cfile);
	      check_number:
		if (token != NUMBER) {
		      need_number:
			parse_warn (cfile, "expecting number.");
			if (token != SEMI)
				skip_to_semi (cfile);
			return 0;
		}
		convert_num (cfile, buf, val, 0, 32);
		if (!make_const_data (&t, buf, 4, 0, 1))
			return 0;
		break;

	      case 's':	/* Signed 16-bit integer. */
	      case 'S':	/* Unsigned 16-bit integer. */
		token = next_token (&val, cfile);
		if (token != NUMBER)
			goto need_number;
		convert_num (cfile, buf, val, 0, 16);
		if (!make_const_data (&t, buf, 2, 0, 1))
			return 0;
		break;

	      case 'b':	/* Signed 8-bit integer. */
	      case 'B':	/* Unsigned 8-bit integer. */
		token = next_token (&val, cfile);
		if (token != NUMBER)
			goto need_number;
		convert_num (cfile, buf, val, 0, 8);
		if (!make_const_data (&t, buf, 1, 0, 1))
			return 0;
		break;

	      case 'f': /* Boolean flag. */
		token = next_token (&val, cfile);
		if (!is_identifier (token)) {
			parse_warn (cfile, "expecting identifier.");
		      bad_flag:
			if (token != SEMI)
				skip_to_semi (cfile);
			return 0;
		}
		if (!strcasecmp (val, "true")
		    || !strcasecmp (val, "on"))
			buf [0] = 1;
		else if (!strcasecmp (val, "false")
			 || !strcasecmp (val, "off"))
			buf [0] = 0;
		else if (!strcasecmp (val, "ignore"))
			buf [0] = 2;
		else {
			parse_warn (cfile, "expecting boolean.");
			goto bad_flag;
		}
		if (!make_const_data (&t, buf, 1, 0, 1))
			return 0;
		break;

	      default:
		parse_warn (cfile, "Bad format %c in parse_option_param.",
			    *fmt);
		skip_to_semi (cfile);
		return 0;
	}
	if (expr) {
		if (!make_concat (rv, expr, t))
			return 0;
		expression_dereference (&t, MDL);
	} else
		*rv = t;
	return 1;
}

int parse_auth_key (key_id, cfile)
	struct data_string *key_id;
	struct parse *cfile;
{
	struct data_string key_data;
	const char *val;
	enum dhcp_token token;
	const struct auth_key *key, *old_key = (struct auth_key *)0;
	struct auth_key *new_key;

	memset (&key_data, 0, sizeof key_data);

	if (!parse_cshl (key_id, cfile))
		return 0;

	key = auth_key_lookup (key_id);

	token = peek_token (&val, cfile);
	if (token == SEMI) {
		if (!key)
			parse_warn (cfile, "reference to undefined key %s",
				    print_hex_1 (key_id -> len,
						 key_id -> data,
						 key_id -> len));
		data_string_forget (key_id, MDL);
	} else {
		if (!parse_cshl (&key_data, cfile))
			return 0;
		if (key) {
			parse_warn (cfile, "redefinition of key %s",
				    print_hex_1 (key_id -> len,
						 key_id -> data,
						 key_id -> len));
			old_key = key;
		}
		new_key = new_auth_key (key_data.len, MDL);
		if (!new_key)
			log_fatal ("No memory for key %s",
				   print_hex_1 (key_id -> len,
						key_id -> data,
						key_id -> len));
		new_key -> length = key_data.len;
		memcpy (new_key -> data, key_data.data, key_data.len);
		enter_auth_key (key_id, new_key);
		data_string_forget (&key_data, MDL);
	}

	parse_semi (cfile);
	return key_id -> len ? 1 : 0;
}

int parse_warn (struct parse *cfile, const char *fmt, ...)
{
	va_list list;
	static char spaces [] = "                                                                                ";
	char lexbuf [256];
	char mbuf [1024];
	char fbuf [1024];
	unsigned i, lix;
	
	do_percentm (mbuf, fmt);
#ifndef NO_SNPRINTF
	snprintf (fbuf, sizeof fbuf, "%s line %d: %s",
		  cfile -> tlname, cfile -> lexline, mbuf);
#else
	sprintf (fbuf, "%s line %d: %s",
		 cfile -> tlname, cfile -> lexline, mbuf);
#endif
	
	va_start (list, fmt);
	vsnprintf (mbuf, sizeof mbuf, fbuf, list);
	va_end (list);

	lix = 0;
	for (i = 0;
	     cfile -> token_line [i] && i < (cfile -> lexchar - 1); i++) {
		if (lix < (sizeof lexbuf) - 1)
			lexbuf [lix++] = ' ';
		if (cfile -> token_line [i] == '\t') {
			for (lix;
			     lix < (sizeof lexbuf) - 1 && (lix & 7); lix++)
				lexbuf [lix] = ' ';
		}
	}
	lexbuf [lix] = 0;

#ifndef DEBUG
	syslog (log_priority | LOG_ERR, mbuf);
	syslog (log_priority | LOG_ERR, cfile -> token_line);
	if (cfile -> lexchar < 81)
		syslog (log_priority | LOG_ERR, "%s^", lexbuf);
#endif

	if (log_perror) {
		write (2, mbuf, strlen (mbuf));
		write (2, "\n", 1);
		write (2, cfile -> token_line, strlen (cfile -> token_line));
		write (2, "\n", 1);
		if (cfile -> lexchar < 81)
			write (2, lexbuf, lix);
		write (2, "^\n", 2);
	}

	cfile -> warnings_occurred = 1;

	return 0;
}

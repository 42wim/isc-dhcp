/* conflex.c

   Lexical scanner for dhcpd config file... */

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
"$Id: conflex.c,v 1.18 1996/08/29 23:02:38 mellon Exp $ Copyright (c) 1995, 1996 The Internet Software Consortium.  All rights reserved.\n";
#endif /* not lint */

#include "dhcpd.h"
#include "dhctoken.h"
#include <ctype.h>

int lexline;
int lexchar;
char *token_line;
char *prev_line;
char *cur_line;
char *tlname;

static char line1 [81];
static char line2 [81];
static int lpos;
static int line;
static int tlpos;
static int tline;
static int token;
static int ugflag;
static char *tval;
static char tokbuf [1500];

#ifdef OLD_LEXER
char comments [4096];
int comment_index;
#endif


static int get_char PROTO ((FILE *));
static int get_token PROTO ((FILE *));
static void skip_to_eol PROTO ((FILE *));
static int read_string PROTO ((FILE *));
static int read_number PROTO ((int, FILE *));
static int read_num_or_name PROTO ((int, FILE *));
static int intern PROTO ((char *, int));

void new_parse (name)
	char *name;
{
	tlname = name;
	lpos = line = 1;
	cur_line = line1;
	prev_line = line2;
	token_line = cur_line;
	cur_line [0] = prev_line [0] = 0;
	warnings_occurred = 0;
}

static int get_char (cfile)
	FILE *cfile;
{
	int c = getc (cfile);
	if (!ugflag) {
		if (c == EOL) {
			if (cur_line == line1) {	
				cur_line = line2;
				prev_line = line1;
			} else {
				cur_line = line2;
				prev_line = line1;
			}
			line++;
			lpos = 1;
			cur_line [0] = 0;
		} else if (c != EOF) {
			if (lpos <= 81) {
				cur_line [lpos - 1] = c;
				cur_line [lpos] = 0;
			}
			lpos++;
		}
	} else
		ugflag = 0;
	return c;		
}

static int get_token (cfile)
	FILE *cfile;
{
	int c;
	int ttok;
	static char tb [2];
	int l, p, u;

	do {
		l = line;
		p = lpos;
		u = ugflag;

		c = get_char (cfile);
#ifdef OLD_LEXER
		if (c == '\n' && p == 1 && !u
		    && comment_index < sizeof comments)
			comments [comment_index++] = '\n';
#endif

		if (isascii (c) && isspace (c))
			continue;
		if (c == '#') {
#ifdef OLD_LEXER
			if (comment_index < sizeof comments)
				comments [comment_index++] = '#';
#endif
			skip_to_eol (cfile);
			continue;
		}
		if (c == '"') {
			lexline = l;
			lexchar = p;
			ttok = read_string (cfile);
			break;
		}
		if ((isascii (c) && isdigit (c)) || c == '-') {
			lexline = l;
			lexchar = p;
			ttok = read_number (c, cfile);
			break;
		} else if (isascii (c) && isalpha (c)) {
			lexline = l;
			lexchar = p;
			ttok = read_num_or_name (c, cfile);
			break;
		} else {
			lexline = l;
			lexchar = p;
			tb [0] = c;
			tb [1] = 0;
			tval = tb;
			ttok = c;
			break;
		}
	} while (1);
	return ttok;
}

int next_token (rval, cfile)
	char **rval;
	FILE *cfile;
{
	int rv;

	if (token) {
		if (lexline != tline)
			token_line = cur_line;
		lexchar = tlpos;
		lexline = tline;
		rv = token;
		token = 0;
	} else {
		rv = get_token (cfile);
		token_line = cur_line;
	}
	if (rval)
		*rval = tval;
#ifdef DEBUG_TOKENS
	fprintf (stderr, "%s:%d ", tval, rv);
#endif
	return rv;
}

int peek_token (rval, cfile)
	char **rval;
	FILE *cfile;
{
	int x;

	if (!token) {
		tlpos = lexchar;
		tline = lexline;
		token = get_token (cfile);
		if (lexline != tline)
			token_line = prev_line;
		x = lexchar; lexchar = tlpos; tlpos = x;
		x = lexline; lexline = tline; tline = x;
	}
	if (rval)
		*rval = tval;
#ifdef DEBUG_TOKENS
	fprintf (stderr, "(%s:%d) ", tval, token);
#endif
	return token;
}

static void skip_to_eol (cfile)
	FILE *cfile;
{
	int c;
	do {
		c = get_char (cfile);
		if (c == EOF)
			return;
#ifdef OLD_LEXER
		if (comment_index < sizeof (comments))
			comments [comment_index++] = c;
#endif
		if (c == EOL) {
			return;
		}
	} while (1);
}

static int read_string (cfile)
	FILE *cfile;
{
	int i;
	int bs = 0;
	int c;

	for (i = 0; i < sizeof tokbuf; i++) {
		c = get_char (cfile);
		if (c == EOF) {
			parse_warn ("eof in string constant");
			break;
		}
		if (bs) {
			bs = 0;
			tokbuf [i] = c;
		} else if (c == '\\')
			bs = 1;
		else if (c == '"')
			break;
		else
			tokbuf [i] = c;
	}
	/* Normally, I'd feel guilty about this, but we're talking about
	   strings that'll fit in a DHCP packet here... */
	if (i == sizeof tokbuf) {
		parse_warn ("string constant larger than internal buffer");
		--i;
	}
	tokbuf [i] = 0;
	tval = tokbuf;
	return STRING;
}

static int read_number (c, cfile)
	int c;
	FILE *cfile;
{
	int seenx = 0;
	int i = 0;
	int token = NUMBER;

	tokbuf [i++] = c;
	for (; i < sizeof tokbuf; i++) {
		c = get_char (cfile);
		if (!seenx && c == 'x') {
			seenx = 1;
#ifndef OLD_LEXER
		} else if (isascii (c) && !isxdigit (c) &&
			   (c == '-' || c == '_' || isalpha (c))) {
			token = NAME;
		} else if (isascii (c) && !isdigit (c) && isxdigit (c)) {
			token = NUMBER_OR_NAME;
#endif
		} else if (!isascii (c) || !isxdigit (c)) {
			ungetc (c, cfile);
			ugflag = 1;
			break;
		}
		tokbuf [i] = c;
	}
	if (i == sizeof tokbuf) {
		parse_warn ("numeric token larger than internal buffer");
		--i;
	}
	tokbuf [i] = 0;
	tval = tokbuf;
	return token;
}

static int read_num_or_name (c, cfile)
	int c;
	FILE *cfile;
{
	int i = 0;
	int rv = NUMBER_OR_NAME;
	tokbuf [i++] = c;
	for (; i < sizeof tokbuf; i++) {
		c = get_char (cfile);
		if (!isascii (c) ||
		    (c != '-' && c != '_' && !isalnum (c))) {
			ungetc (c, cfile);
			ugflag = 1;
			break;
		}
		if (!isxdigit (c))
			rv = NAME;
		tokbuf [i] = c;
	}
	if (i == sizeof tokbuf) {
		parse_warn ("token larger than internal buffer");
		--i;
	}
	tokbuf [i] = 0;
	tval = tokbuf;
	return intern (tval, rv);
}

static int intern (atom, dfv)
	char *atom;
	int dfv;
{
	if (!isascii (atom [0]))
		return dfv;

	switch (tolower (atom [0])) {
	      case 'b':
		if (!strcasecmp (atom + 1, "oot-unknown-clients"))
			return BOOT_UNKNOWN_CLIENTS;
	      case 'c':
		if (!strcasecmp (atom + 1, "lass"))
			return CLASS;
		if (!strcasecmp (atom + 1, "iaddr"))
			return CIADDR;
		break;
	      case 'd':
		if (!strcasecmp (atom + 1, "efault-lease-time"))
			return DEFAULT_LEASE_TIME;
		if (!strncasecmp (atom + 1, "ynamic-bootp", 12)) {
			if (!atom [13])
				return DYNAMIC_BOOTP;
			else if (!strcasecmp (atom + 13, "-lease-cutoff"))
				return DYNAMIC_BOOTP_LEASE_CUTOFF;
			else if (!strcasecmp (atom + 13, "-lease-length"))
				return DYNAMIC_BOOTP_LEASE_LENGTH;
		}
		break;
	      case 'e':
		if (!strcasecmp (atom + 1, "thernet"))
			return ETHERNET;
		if (!strcasecmp (atom + 1, "nds"))
			return ENDS;
		break;
	      case 'f':
		if (!strcasecmp (atom + 1, "ilename"))
			return FILENAME;
		if (!strcasecmp (atom + 1, "ixed-address"))
			return FIXED_ADDR;
		break;
	      case 'g':
		if (!strcasecmp (atom + 1, "iaddr"))
			return GIADDR;
		if (!strcasecmp (atom + 1, "roup"))
			return GROUP;
		if (!strcasecmp (atom + 1, "et-lease-hostnames"))
			return GET_LEASE_HOSTNAMES;
		break;
	      case 'h':
		if (!strcasecmp (atom + 1, "ost"))
			return HOST;
		if (!strcasecmp (atom + 1, "ardware"))
			return HARDWARE;
		break;
	      case 'l':
		if (!strcasecmp (atom + 1, "ease"))
			return LEASE;
		break;
	      case 'm':
		if (!strcasecmp (atom + 1, "ax-lease-time"))
			return MAX_LEASE_TIME;
		break;
	      case 'n':
		if (!strcasecmp (atom + 1, "etmask"))
			return NETMASK;
		if (!strcasecmp (atom + 1, "ext-server"))
			return NEXT_SERVER;
		break;
	      case 'o':
		if (!strcasecmp (atom + 1, "ption"))
			return OPTION;
		if (!strcasecmp (atom + 1, "ne-lease-per-client"))
			return ONE_LEASE_PER_CLIENT;
		break;
	      case 'p':
		if (!strcasecmp (atom + 1, "acket"))
			return PACKET;
		break;
	      case 'r':
		if (!strcasecmp (atom + 1, "ange"))
			return RANGE;
		break;
	      case 's':
		if (!strcasecmp (atom + 1, "tarts"))
			return STARTS;
		if (!strcasecmp (atom + 1, "iaddr"))
			return SIADDR;
		if (!strcasecmp (atom + 1, "ubnet"))
			return SUBNET;
		if (!strcasecmp (atom + 1, "hared-network"))
			return SHARED_NETWORK;
		if (!strcasecmp (atom + 1, "erver-name"))
			return SERVER_NAME;
		if (!strcasecmp (atom + 1, "erver-identifier"))
			return SERVER_IDENTIFIER;
		break;
	      case 't':
		if (!strcasecmp (atom + 1, "imestamp"))
			return TIMESTAMP;
		if (!strcasecmp (atom + 1, "oken-ring"))
			return TOKEN_RING;
		break;
	      case 'u':
		if (!strcasecmp (atom + 1, "id"))
			return UID;
		if (!strcasecmp (atom + 1, "ser-class"))
			return USER_CLASS;
		break;
	      case 'v':
		if (!strcasecmp (atom + 1, "endor-class"))
			return VENDOR_CLASS;
		break;
	      case 'y':
		if (!strcasecmp (atom + 1, "iaddr"))
			return YIADDR;
		break;
	}
	return dfv;
}

/* dhctoken.h

   Tokens for config file lexer and parser. */

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

#define SEMI ';'
#define DOT '.'
#define COLON ':'
#define COMMA ','
#define SLASH '/'
#define LBRACE '{'
#define RBRACE '}'

#define FIRST_TOKEN	HOST
#define HOST		256
#define HARDWARE	257
#define FILENAME	258
#define FIXED_ADDR	259
#define OPTION		260
#define ETHERNET	261
#define STRING		262
#define NUMBER		263
#define NUMBER_OR_NAME	264
#define NAME		265
#define TIMESTAMP	266
#define STARTS		267
#define ENDS		268
#define UID		269
#define CLASS		270
#define LEASE		271
#define RANGE		272
#define PACKET		273
#define CIADDR		274
#define YIADDR		275
#define SIADDR		276
#define GIADDR		277
#define SUBNET		278
#define NETMASK		279
#define DEFAULT_LEASE_TIME 280
#define MAX_LEASE_TIME	281
#define VENDOR_CLASS	282
#define USER_CLASS	283
#define SHARED_NETWORK	284
#define SERVER_NAME	285
#define DYNAMIC_BOOTP	286
#define SERVER_IDENTIFIER 287
#define DYNAMIC_BOOTP_LEASE_CUTOFF 288
#define DYNAMIC_BOOTP_LEASE_LENGTH 289
#define BOOT_UNKNOWN_CLIENTS 290
#define NEXT_SERVER	291
#define TOKEN_RING	292
#define GROUP		293
#define ONE_LEASE_PER_CLIENT 294
#define GET_LEASE_HOSTNAMES 295
#define USE_HOST_DECL_NAMES 296

#define is_identifier(x)	((x) >= FIRST_TOKEN &&	\
				 (x) != STRING &&	\
				 (x) != NUMBER &&	\
				 (x) != EOF)

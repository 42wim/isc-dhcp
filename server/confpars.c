/* confpars.c

   Parser for dhcpd config file... */

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
"$Id: confpars.c,v 1.95 2000/01/08 01:43:52 mellon Exp $ Copyright (c) 1995, 1996 The Internet Software Consortium.  All rights reserved.\n";
#endif /* not lint */

#include "dhcpd.h"

static TIME parsed_time;

/* conf-file :== parameters declarations EOF
   parameters :== <nil> | parameter | parameters parameter
   declarations :== <nil> | declaration | declarations declaration */

isc_result_t readconf ()
{
	int file;
	struct parse *cfile;
	const char *val;
	enum dhcp_token token;
	int declaration = 0;
	int status;

	/* Set up the initial dhcp option universe. */
	initialize_common_option_spaces ();
	initialize_server_option_spaces ();

	root_group.authoritative = 0;

	if ((file = open (path_dhcpd_conf, O_RDONLY)) < 0)
		log_fatal ("Can't open %s: %m", path_dhcpd_conf);

	cfile = (struct parse *)0;
	new_parse (&cfile, file, (char *)0, 0, path_dhcpd_conf);

	do {
		token = peek_token (&val, cfile);
		if (token == EOF)
			break;
		declaration = parse_statement (cfile, &root_group,
					       ROOT_GROUP,
					       (struct host_decl *)0,
					       declaration);
	} while (1);
	token = next_token (&val, cfile); /* Clear the peek buffer */

	status = cfile -> warnings_occurred ? ISC_R_BADPARSE : ISC_R_SUCCESS;

	end_parse (&cfile);
	close (file);
	return status;
}

/* lease-file :== lease-declarations EOF
   lease-statments :== <nil>
   		     | lease-declaration
		     | lease-declarations lease-declaration */

isc_result_t read_leases ()
{
	struct parse *cfile;
	int file;
	const char *val;
	enum dhcp_token token;
	isc_result_t status;

	/* Open the lease file.   If we can't open it, fail.   The reason
	   for this is that although on initial startup, the absence of
	   a lease file is perfectly benign, if dhcpd has been running 
	   and this file is absent, it means that dhcpd tried and failed
	   to rewrite the lease database.   If we proceed and the
	   problem which caused the rewrite to fail has been fixed, but no
	   human has corrected the database problem, then we are left
	   thinking that no leases have been assigned to anybody, which
	   could create severe network chaos. */
	if ((file = open (path_dhcpd_db, O_RDONLY)) < 0) {
		log_error ("Can't open lease database %s: %m -- %s",
			   path_dhcpd_db,
			   "check for failed database rewrite attempt!");
		log_error ("Please read the dhcpd.leases manual page if you");
 		log_fatal ("don't know what to do about this.");
	}

	cfile = (struct parse *)0;
	new_parse (&cfile, file, (char *)0, 0, path_dhcpd_db);

	do {
		token = next_token (&val, cfile);
		if (token == EOF)
			break;
		if (token == LEASE) {
			struct lease *lease;
			lease = parse_lease_declaration (cfile);
			if (lease) {
				enter_lease (lease);
				if (lease -> on_expiry)
					executable_statement_dereference
						(&lease -> on_expiry,
						 "read_leases");
				if (lease -> on_commit)
					executable_statement_dereference
						(&lease -> on_commit,
						 "read_leases");
				if (lease -> on_release)
					executable_statement_dereference
						(&lease -> on_release,
						 "read_leases");
			} else
				parse_warn (cfile,
					    "possibly corrupt lease file");
		} else if (token == HOST) {
			parse_host_declaration (cfile, &root_group);
		} else if (token == GROUP) {
			parse_group_declaration (cfile, &root_group);
		} else {
			log_error ("Corrupt lease file - possible data loss!");
			skip_to_semi (cfile);
		}

	} while (1);

	status = cfile -> warnings_occurred ? ISC_R_BADPARSE : ISC_R_SUCCESS;

	end_parse (&cfile);
	close (file);

	return status;
}

/* statement :== parameter | declaration

   parameter :== timestamp
   	       | DEFAULT_LEASE_TIME lease_time
	       | MAX_LEASE_TIME lease_time
	       | DYNAMIC_BOOTP_LEASE_CUTOFF date
	       | DYNAMIC_BOOTP_LEASE_LENGTH lease_time
	       | BOOT_UNKNOWN_CLIENTS boolean
	       | ONE_LEASE_PER_CLIENT boolean
	       | GET_LEASE_HOSTNAMES boolean
	       | USE_HOST_DECL_NAME boolean
	       | NEXT_SERVER ip-addr-or-hostname SEMI
	       | option_parameter
	       | SERVER-IDENTIFIER ip-addr-or-hostname SEMI
	       | FILENAME string-parameter
	       | SERVER_NAME string-parameter
	       | hardware-parameter
	       | fixed-address-parameter
	       | ALLOW allow-deny-keyword
	       | DENY allow-deny-keyword
	       | USE_LEASE_ADDR_FOR_DEFAULT_ROUTE boolean
	       | AUTHORITATIVE
	       | NOT AUTHORITATIVE
	       | AUTH_KEY key-id key-value

   declaration :== host-declaration
		 | group-declaration
		 | shared-network-declaration
		 | subnet-declaration
		 | VENDOR_CLASS class-declaration
		 | USER_CLASS class-declaration
		 | RANGE address-range-declaration */

int parse_statement (cfile, group, type, host_decl, declaration)
	struct parse *cfile;
	struct group *group;
	int type;
	struct host_decl *host_decl;
	int declaration;
{
	enum dhcp_token token;
	const char *val;
	struct shared_network *share;
	char *t, *n;
	struct expression *expr;
	struct data_string data;
	struct hardware hardware;
	struct executable_statement *et, *ep;
	struct option *option;
	struct option_cache *cache;
	int lose;
	struct data_string key_id;
	int known;

	token = peek_token (&val, cfile);

	switch (token) {
	      case AUTH_KEY:
		memset (&key_id, 0, sizeof key_id);
		if (parse_auth_key (&key_id, cfile)) {
			if (type == HOST_DECL)
				data_string_copy (&host_decl -> auth_key_id,
						  &key_id, "parse_statement");
			data_string_forget (&key_id, "parse_statement");
		}
		break;
	      case HOST:
		next_token (&val, cfile);
		if (type != HOST_DECL && type != CLASS_DECL)
			parse_host_declaration (cfile, group);
		else {
			parse_warn (cfile,
				    "host declarations not allowed here.");
			skip_to_semi (cfile);
		}
		return 1;

	      case GROUP:
		next_token (&val, cfile);
		if (type != HOST_DECL && type != CLASS_DECL)
			parse_group_declaration (cfile, group);
		else {
			parse_warn (cfile,
				    "group declarations not allowed here.");
			skip_to_semi (cfile);
		}
		return 1;

	      case TIMESTAMP:
		next_token (&val, cfile);
		parsed_time = parse_timestamp (cfile);
		break;

	      case SHARED_NETWORK:
		next_token (&val, cfile);
		if (type == SHARED_NET_DECL ||
		    type == HOST_DECL ||
		    type == SUBNET_DECL ||
		    type == CLASS_DECL) {
			parse_warn (cfile, "shared-network parameters not %s.",
				    "allowed here");
			skip_to_semi (cfile);
			break;
		}

		parse_shared_net_declaration (cfile, group);
		return 1;

	      case SUBNET:
		next_token (&val, cfile);
		if (type == HOST_DECL || type == SUBNET_DECL ||
		    type == CLASS_DECL) {
			parse_warn (cfile,
				    "subnet declarations not allowed here.");
			skip_to_semi (cfile);
			return 1;
		}

		/* If we're in a subnet declaration, just do the parse. */
		if (group -> shared_network) {
			parse_subnet_declaration (cfile,
						  group -> shared_network);
			break;
		}

		/* Otherwise, cons up a fake shared network structure
		   and populate it with the lone subnet... */

		share = new_shared_network ("parse_statement");
		if (!share)
			log_fatal ("No memory for shared subnet");
		share -> group = clone_group (group, "parse_statement:subnet");
		share -> group -> shared_network = share;

		parse_subnet_declaration (cfile, share);

		/* share -> subnets is the subnet we just parsed. */
		if (share -> subnets) {
			share -> interface =
				share -> subnets -> interface;

			/* Make the shared network name from network number. */
			n = piaddr (share -> subnets -> net);
			t = malloc (strlen (n) + 1);
			if (!t)
				log_fatal ("no memory for subnet name");
			strcpy (t, n);
			share -> name = t;

			/* Copy the authoritative parameter from the subnet,
			   since there is no opportunity to declare it here. */
			share -> group -> authoritative =
				share -> subnets -> group -> authoritative;
			enter_shared_network (share);
		}
		return 1;

	      case VENDOR_CLASS:
		next_token (&val, cfile);
		if (type == CLASS_DECL) {
			parse_warn (cfile,
				    "class declarations not allowed here.");
			skip_to_semi (cfile);
			break;
		}
		parse_class_declaration (cfile, group, 0);
		return 1;

	      case USER_CLASS:
		next_token (&val, cfile);
		if (type == CLASS_DECL) {
			parse_warn (cfile,
				    "class declarations not allowed here.");
			skip_to_semi (cfile);
			break;
		}
		parse_class_declaration (cfile, group, 1);
		return 1;

	      case CLASS:
		next_token (&val, cfile);
		if (type == CLASS_DECL) {
			parse_warn (cfile,
				    "class declarations not allowed here.");
			skip_to_semi (cfile);
			break;
		}
		parse_class_declaration (cfile, group, 2);
		return 1;

	      case SUBCLASS:
		next_token (&val, cfile);
		if (type == CLASS_DECL) {
			parse_warn (cfile,
				    "class declarations not allowed here.");
			skip_to_semi (cfile);
			break;
		}
		parse_class_declaration (cfile, group, 3);
		return 1;

	      case HARDWARE:
		next_token (&val, cfile);
		parse_hardware_param (cfile, &hardware);
		if (host_decl)
			host_decl -> interface = hardware;
		else
			parse_warn (cfile, "hardware address parameter %s",
				    "not allowed here.");
		break;

	      case FIXED_ADDR:
		next_token (&val, cfile);
		cache = (struct option_cache *)0;
		parse_fixed_addr_param (&cache, cfile);
		if (host_decl)
			host_decl -> fixed_addr = cache;
		else {
			parse_warn (cfile, "fixed-address parameter not %s",
				    "allowed here.");
			option_cache_dereference (&cache, "parse_statement");
		}
		break;

	      case POOL:
		next_token (&val, cfile);
		if (type != SUBNET_DECL && type != SHARED_NET_DECL) {
			parse_warn (cfile, "pool declared outside of network");
		}
		if (type == POOL_DECL) {
			parse_warn (cfile, "pool declared within pool.");
		}
		parse_pool_statement (cfile, group, type);
		return declaration;

	      case RANGE:
		next_token (&val, cfile);
		if (type != SUBNET_DECL || !group -> subnet) {
			parse_warn (cfile,
				    "range declaration not allowed here.");
			skip_to_semi (cfile);
			return declaration;
		}
		parse_address_range (cfile, group, type, (struct pool *)0);
		return declaration;

	      case TOKEN_NOT:
		token = next_token (&val, cfile);
		token = next_token (&val, cfile);
		switch (token) {
		      case AUTHORITATIVE:
			group -> authoritative = 0;
			goto authoritative;
		      default:
			parse_warn (cfile, "expecting assertion");
			skip_to_semi (cfile);
			break;
		}
		break;
	      case AUTHORITATIVE:
		token = next_token (&val, cfile);
		group -> authoritative = 1;
	      authoritative:
		if (type == HOST_DECL)
			parse_warn (cfile, "authority makes no sense here."); 
		parse_semi (cfile);
		break;

		/* "server-identifier" is a special hack, equivalent to
		   "option dhcp-server-identifier". */
	      case SERVER_IDENTIFIER:
		option = dhcp_universe.options [DHO_DHCP_SERVER_IDENTIFIER];
		token = next_token (&val, cfile);
		goto finish_option;

	      case OPTION:
		token = next_token (&val, cfile);
		token = peek_token (&val, cfile);
		if (token == SPACE) {
			if (type != ROOT_GROUP) {
				parse_warn (cfile,
					    "option space definitions %s",
					    " may not be scoped.");
				skip_to_semi (cfile);
				free_option (option, "parse_statement");
				break;
			}
			parse_option_space_decl (cfile);
			return declaration;
		}

		known = 0;
		option = parse_option_name (cfile, 1, &known);
		if (option) {
			token = peek_token (&val, cfile);
			if (token == CODE) {
				if (type != ROOT_GROUP) {
					parse_warn (cfile,
						    "option definitions%s",
						    " may not be scoped.");
					skip_to_semi (cfile);
					free_option (option,
						     "parse_statement");
					break;
				}
				next_token (&val, cfile);
				if (!parse_option_code_definition (cfile,
								   option))
					free_option (option,
						     "parse_statement");
				return declaration;
			}

			/* If this wasn't an option code definition, don't
			   allow an unknown option. */
			if (!known) {
				parse_warn (cfile, "unknown option %s.%s",
					    option -> universe -> name,
					    option -> name);
				skip_to_semi (cfile);
				free_option (option, "parse_statement");
				return declaration;
			}

		      finish_option:
			et = (struct executable_statement *)0;
			if (!parse_option_statement
				(&et, cfile, 1, option,
				 supersede_option_statement))
				return declaration;
			goto insert_statement;
		} else
			return declaration;

		break;

	      case FAILOVER:
		if (type != ROOT_GROUP && type != SHARED_NETWORK) {
			parse_warn (cfile, "failover peers may only be %s",
				    "defined in shared-network");
			log_error ("declarations and the outer scope.");
			skip_to_semi (cfile);
			break;
		}
		token = next_token (&val, cfile);
#if defined (FAILOVER_PROTOCOL)
		parse_failover_peer (cfile, group, type);
#else
		parse_warn (cfile, "No failover support.");
		skip_to_semi (cfile);
#endif
		break;

	      default:
		et = (struct executable_statement *)0;
		if (is_identifier (token)) {
			option = ((struct option *)
				  hash_lookup (server_universe.hash,
					       (const unsigned char *)val, 0));
			if (option) {
				token = next_token (&val, cfile);
				if (!parse_option_statement
					(&et, cfile, 1, option,
					 supersede_option_statement))
					return declaration;
			}
		}

		if (!et) {
			lose = 0;
			if (!parse_executable_statement (&et, cfile, &lose,
							 context_any)) {
				if (!lose) {
				    if (declaration)
					parse_warn (cfile,
						    "expecting a declaration");
				    else
					parse_warn (cfile,
						    "expecting a parameter %s",
						    "or declaration");
					skip_to_semi (cfile);
				}
				return declaration;
			}
		}
		if (!et) {
			parse_warn (cfile, "expecting a %sdeclaration",
				    declaration ? "" :  "parameter or ");
			return declaration;
		}
	      insert_statement:
		if (group -> statements) {
			int multi = 0;

			/* If this set of statements is only referenced
			   by this group, just add the current statement
			   to the end of the chain. */
			for (ep = group -> statements; ep -> next;
			     ep = ep -> next)
				if (ep -> refcnt > 1) /* XXX */
					multi = 1;
			if (!multi) {
				executable_statement_reference
					(&ep -> next, et, "parse_statement");
				return declaration;
			}

			/* Otherwise, make a parent chain, and put the
			   current group statements first and the new
			   statement in the next pointer. */
			ep = (struct executable_statement *)0;
			if (!executable_statement_allocate
			    (&ep, "parse_statement"))
				log_fatal ("No memory for statements.");
			ep -> op = statements_statement;
			executable_statement_reference
				(&ep -> data.statements,
				 group -> statements, "parse_statement");
			executable_statement_reference
				(&ep -> next, et, "parse_statement");
			executable_statement_dereference
				(&group -> statements, "parse_statement");
			executable_statement_reference
				(&group -> statements, ep, "parse_statements");
		} else
			executable_statement_reference
				(&group -> statements, et, "parse_statements");
		return declaration;
	}

	if (declaration) {
		parse_warn (cfile,
			    "parameters not allowed after first declaration.");
		return 1;
	}

	return 0;
}

#if defined (FAILOVER_PROTOCOL)
void parse_failover_peer (cfile, group, type)
	struct parse *cfile;
	struct group *group;
	int type;
{
	enum dhcp_token token;
	const char *val;
	dhcp_failover_state_t *peer;
	u_int32_t *tp;
	char *name;
	u_int32_t split;
	u_int8_t hba [32];
	int hba_len = sizeof hba;
	int i;
	struct expression *expr;

	token = next_token (&val, cfile);
	if (token != PEER) {
		parse_warn (cfile, "expecting \"peer\"");
		skip_to_semi (cfile);
		return;
	}

	token = next_token (&val, cfile);
	if (is_identifier (token) || token == STRING) {
		name = dmalloc (strlen (val) + 1, "peer name");
		if (!name)
			log_fatal ("no memory for peer name %s", name);
		strcpy (name, val);
	} else {
		parse_warn (cfile, "expecting failover peer name.");
		skip_to_semi (cfile);
		return;
	}

	/* See if there's a peer declaration by this name. */
	peer = (dhcp_failover_state_t *)0;
	find_failover_peer (&peer, name);

	token = next_token (&val, cfile);
	if (token == SEMI) {
		dfree (name, "peer name");
		if (type != SHARED_NET_DECL)
			parse_warn (cfile, "failover peer reference not %s",
				    "in shared-network declaration");
		else {
			if (!peer) {
				parse_warn (cfile, "reference to unknown%s%s",
					    " failover peer ", name);
				return;
			}
			group -> shared_network -> failover_peer =
				peer;
		}
		return;
	} else if (token == MY || token == PARTNER) {
		if (!peer) {
			parse_warn (cfile, "reference to unknown%s%s",
				    " failover peer ", name);
			return;
		}
		if (token == MY)
			parse_failover_state (cfile, &peer -> my_state,
					      &peer -> my_stos);
		else
			parse_failover_state (cfile, &peer -> partner_state,
					      &peer -> partner_stos);
		return;
	} else if (token != LBRACE) {
		parse_warn (cfile, "expecting left brace");
		skip_to_semi (cfile);
	}

	/* Make sure this isn't a redeclaration. */
	if (peer) {
		parse_warn (cfile, "redeclaration of failover peer %s", name);
		skip_to_rbrace (cfile, 1);
		return;
	}

	peer = dmalloc (sizeof *peer, "parse_failover_peer");
	if (!peer)
		log_fatal ("no memory for failover peer%s.", name);
	memset (peer, 0, sizeof *peer);

	/* Save the name. */
	peer -> name = name;

	do {
		token = next_token (&val, cfile);
		switch (token) {
		      case RBRACE:
			break;

		      case PRIMARY:
			peer -> i_am = primary;
			break;

		      case SECONDARY:
			peer -> i_am = secondary;
			break;

		      case IDENTIFIER:
			expr = (struct expression *)0;
			if (!parse_ip_addr_or_hostname (&expr, cfile, 0)) {
				skip_to_rbrace (cfile, 1);
				return;
			}
			option_cache (&peer -> address,
				      (struct data_string *)0, expr,
				      (struct option *)0);
			expression_dereference (&expr,
						"parse_failover_peer");
			break;
		      case PORT:
			token = next_token (&val, cfile);
			if (token != NUMBER) {
				parse_warn (cfile, "expecting number");
				skip_to_rbrace (cfile, 1);
			}
			peer -> port = atoi (val);
			if (!parse_semi (cfile)) {
				skip_to_rbrace (cfile, 1);
				return;
			}
			break;

		      case MAX_TRANSMIT_IDLE:
			tp = &peer -> max_transmit_idle;
			goto parse_idle;

		      case MAX_RESPONSE_DELAY:
			tp = &peer -> max_transmit_idle;
		      parse_idle:
			token = next_token (&val, cfile);
			if (token != NUMBER) {
				parse_warn (cfile, "expecting number.");
				skip_to_rbrace (cfile, 1);
				return;
			}
			*tp = atoi (val);
			break;

		      case MAX_UNACKED_UPDATES:
			tp = &peer -> max_flying_updates;
			goto parse_idle;

		      case MCLT:
			tp = &peer -> mclt;
			goto parse_idle;

		      case HBA:
			if (!parse_numeric_aggregate (cfile, hba, &hba_len,
						      COLON, 32, 8)) {
				skip_to_rbrace (cfile, 1);
				return;
			}
			parse_semi (cfile);
		      make_hba:
			peer -> hba = dmalloc (32, "parse_failover_peer");
			if (!peer -> hba) {
				dfree (peer -> name, "parse_failover_peer");
				dfree (peer, "parse_failover_peer");
			}
			memcpy (peer -> hba, hba, 32);
			break;

		      case SPLIT:
			token = next_token (&val, cfile);
			if (token != NUMBER) {
				parse_warn (cfile, "expecting number");
				skip_to_rbrace (cfile, 1);
			}
			split = atoi (val);
			if (!parse_semi (cfile)) {
				skip_to_rbrace (cfile, 1);
				return;
			}
			if (split > 255) {
				parse_warn (cfile, "split must be < 256");
			} else {
				memset (hba, 0, sizeof hba);
				for (i = 0; i < split; i++) {
					if (i < split)
						hba [i / 8] |= (1 << (i & 7));
				}
				goto make_hba;
			}
			break;
			
		      default:
			parse_warn (cfile,
				    "invalid statement in peer declaration");
			skip_to_rbrace (cfile, 1);
			return;
		}
	} while (token != RBRACE);
		
	if (type == SHARED_NET_DECL) {
		group -> shared_network -> failover_peer = peer;
	}
	enter_failover_peer (peer);
}

void parse_failover_state (cfile, state, stos)
	struct parse *cfile;
	enum failover_state *state;
	TIME *stos;
{
	enum dhcp_token token;
	const char *val;
	enum failover_state state_in;
	TIME stos_in;

	token = next_token (&val, cfile);
	switch (token) {
	      case PARTNER_DOWN:
		state_in = partner_down;
		break;

	      case NORMAL:
		state_in = normal;
		break;

	      case COMMUNICATIONS_INTERRUPTED:
		state_in = communications_interrupted;
		break;

	      case POTENTIAL_CONFLICT:
		state_in = potential_conflict;
		break;

	      case RECOVER:
		state_in = recover;
		break;

	      default:
		parse_warn (cfile, "unknown failover state");
		skip_to_semi (cfile);
		return;
	}

	token = next_token (&val, cfile);
	if (token != AT) {
		parse_warn (cfile, "expecting \"at\"");
		skip_to_semi (cfile);
		return;
	}

	stos_in = parse_date (cfile);
	if (!stos_in)
		return;

	/* Now that we've apparently gotten a clean parse, we can trust
	   that this is a state that was fully committed to disk, so
	   we can install it. */
	*stos = stos_in;
	*state = state_in;
}
#endif /* defined (FAILOVER_PROTOCOL) */

void parse_pool_statement (cfile, group, type)
	struct parse *cfile;
	struct group *group;
	int type;
{
	enum dhcp_token token;
	const char *val;
	int done = 0;
	struct pool *pool, **p;
	struct permit *permit;
	struct permit **permit_head;
	int declaration = 0;

	pool = new_pool ("parse_pool_statement");
	if (!pool)
		log_fatal ("no memory for pool.");

	pool -> group = clone_group (group, "parse_pool_statement");

	if (type == SUBNET_DECL)
		pool -> shared_network = group -> subnet -> shared_network;
	else
		pool -> shared_network = group -> shared_network;

#if defined (FAILOVER_PROTOCOL)
	/* Inherit the failover peer from the shared network. */
	if (pool -> shared_network -> failover_peer)
	    omapi_object_reference ((omapi_object_t **)
				    &pool -> failover_peer, 
				    (omapi_object_t *)
				    pool -> shared_network -> failover_peer,
				    "parse_pool_statement");
#endif

	if (!parse_lbrace (cfile))
		return;
	do {
		token = peek_token (&val, cfile);
		switch (token) {
		      case NO:
			next_token (&val, cfile);
			token = next_token (&val, cfile);
			if (token != FAILOVER ||
			    (token = next_token (&val, cfile)) != PEER) {
				parse_warn (cfile,
					    "expecting \"failover peer\".");
				skip_to_semi (cfile);
				continue;
			}
#if defined (FAILOVER_PROTOCOL)
			if (pool -> failover_peer)
				omapi_object_dereference
					((omapi_object_t **)
					 &pool -> failover_peer,
					 "parse_pool_statement");
			/* XXX Flag error if there's no failover peer? */
#endif
			break;
				
		      case RANGE:
			next_token (&val, cfile);
			parse_address_range (cfile, group, type, pool);
			break;
		      case ALLOW:
			permit_head = &pool -> permit_list;
		      get_permit:
			permit = new_permit ("parse_pool_statement");
			if (!permit)
				log_fatal ("no memory for permit");
			next_token (&val, cfile);
			token = next_token (&val, cfile);
			switch (token) {
			      case UNKNOWN:
				permit -> type = permit_unknown_clients;
			      get_clients:
				if (next_token (&val, cfile) != CLIENTS) {
					parse_warn (cfile,
						    "expecting \"clients\"");
					skip_to_semi (cfile);
					free_permit (permit,
						     "parse_pool_statement");
					continue;
				}
				break;
				
			      case UNKNOWN_CLIENTS:
				permit -> type = permit_unknown_clients;
				break;

			      case KNOWN:
				permit -> type = permit_known_clients;
				goto get_clients;
				
			      case AUTHENTICATED:
				permit -> type = permit_authenticated_clients;
				goto get_clients;
				
			      case UNAUTHENTICATED:
				permit -> type =
					permit_unauthenticated_clients;
				goto get_clients;

			      case ALL:
				permit -> type = permit_all_clients;
				goto get_clients;
				break;
				
			      case DYNAMIC:
				permit -> type = permit_dynamic_bootp_clients;
				if (next_token (&val, cfile) != BOOTP) {
					parse_warn (cfile,
						    "expecting \"bootp\"");
					skip_to_semi (cfile);
					free_permit (permit,
						     "parse_pool_statement");
					continue;
				}
				goto get_clients;
				
			      case MEMBERS:
				if (next_token (&val, cfile) != OF) {
					parse_warn (cfile, "expecting \"of\"");
					skip_to_semi (cfile);
					free_permit (permit,
						     "parse_pool_statement");
					continue;
				}
				if (next_token (&val, cfile) != STRING) {
					parse_warn (cfile,
						    "expecting class name.");
					skip_to_semi (cfile);
					free_permit (permit,
						     "parse_pool_statement");
					continue;
				}
				permit -> type = permit_class;
				permit -> class = find_class (val);
				if (!permit -> class)
					parse_warn (cfile,
						    "no such class: %s", val);
				break;

			      default:
				parse_warn (cfile, "expecting permit type.");
				skip_to_semi (cfile);
				break;
			}
			while (*permit_head)
				permit_head = &((*permit_head) -> next);
			*permit_head = permit;
			parse_semi (cfile);
			break;

		      case DENY:
			permit_head = &pool -> prohibit_list;
			goto get_permit;
			
		      case RBRACE:
			next_token (&val, cfile);
			done = 1;
			break;

		      default:
			declaration = parse_statement (cfile, pool -> group,
						       POOL_DECL,
						       (struct host_decl *)0,
						       declaration);
			break;
		}
	} while (!done);

#if defined (FAILOVER_PROTOCOL)
	/* We can't do failover on a pool that supports dynamic bootp,
	   because BOOTP doesn't support leases, and failover absolutely
	   depends on lease timing. */
	if (pool -> failover_peer) {
		for (permit = pool -> permit_list;
		     permit; permit = permit -> next) {
			if (permit -> type == permit_dynamic_bootp_clients ||
			    permit -> type == permit_all_clients) {
				  dynamic_bootp_clash:
				parse_warn (cfile,
					    "pools with failover peers %s",
					    "may not permit dynamic bootp.");
				log_error ("Either write a \"no failover\"%s",
					   "statement and use disjoint");
				log_error ("pools, or don't permit dynamic%s",
					   "bootp.");
				log_error ("This is a protocol limitation,%s",
					   "not an ISC DHCP limitation, so");
				log_error ("please don't request an %s",
					   "enhancement or ask why this is.");
				goto clash_testing_done;
			}
		}
		if (!pool -> permit_list) {
			if (!pool -> prohibit_list)
				goto dynamic_bootp_clash;

			for (permit = pool -> prohibit_list; permit;
			     permit = permit -> next) {
				if (permit -> type ==
				    permit_dynamic_bootp_clients ||
				    permit -> type == permit_all_clients)
					goto clash_testing_done;
			}
		}
	}
      clash_testing_done:				
#endif /* FAILOVER_PROTOCOL */

	if (type == SUBNET_DECL)
		pool -> shared_network = group -> subnet -> shared_network;
	else
		pool -> shared_network = group -> shared_network;

	p = &pool -> shared_network -> pools;
	for (; *p; p = &((*p) -> next))
		;
	*p = pool;
}

/* boolean :== ON SEMI | OFF SEMI | TRUE SEMI | FALSE SEMI */

int parse_boolean (cfile)
	struct parse *cfile;
{
	enum dhcp_token token;
	const char *val;
	int rv;

	token = next_token (&val, cfile);
	if (!strcasecmp (val, "true")
	    || !strcasecmp (val, "on"))
		rv = 1;
	else if (!strcasecmp (val, "false")
		 || !strcasecmp (val, "off"))
		rv = 0;
	else {
		parse_warn (cfile,
			    "boolean value (true/false/on/off) expected");
		skip_to_semi (cfile);
		return 0;
	}
	parse_semi (cfile);
	return rv;
}

/* Expect a left brace; if there isn't one, skip over the rest of the
   statement and return zero; otherwise, return 1. */

int parse_lbrace (cfile)
	struct parse *cfile;
{
	enum dhcp_token token;
	const char *val;

	token = next_token (&val, cfile);
	if (token != LBRACE) {
		parse_warn (cfile, "expecting left brace.");
		skip_to_semi (cfile);
		return 0;
	}
	return 1;
}


/* host-declaration :== hostname RBRACE parameters declarations LBRACE */

void parse_host_declaration (cfile, group)
	struct parse *cfile;
	struct group *group;
{
	const char *val;
	enum dhcp_token token;
	struct host_decl *host;
	char *name;
	int declaration = 0;
	int dynamicp = 0;
	int deleted = 0;
	isc_result_t status;

	token = peek_token (&val, cfile);
	if (token != LBRACE) {
		name = parse_host_name (cfile);
		if (!name)
			return;
	} else {
		name = (char *)0;
	}

	host = (struct host_decl *)dmalloc (sizeof (struct host_decl),
					    "parse_host_declaration");
	if (!host)
		log_fatal ("can't allocate host decl struct %s.", name);
	memset (host, 0, sizeof *host);
	host -> name = name;
	host -> group = clone_group (group, "parse_host_declaration");

	if (!parse_lbrace (cfile))
		return;

	do {
		token = peek_token (&val, cfile);
		if (token == RBRACE) {
			token = next_token (&val, cfile);
			break;
		}
		if (token == EOF) {
			token = next_token (&val, cfile);
			parse_warn (cfile, "unexpected end of file");
			break;
		}
		/* If the host declaration was created by the server,
		   remember to save it. */
		if (token == DYNAMIC) {
			dynamicp = 1;
			token = next_token (&val, cfile);
			if (!parse_semi (cfile))
				break;
			continue;
		}
		/* If the host declaration was created by the server,
		   remember to save it. */
		if (token == DELETED) {
			deleted = 1;
			token = next_token (&val, cfile);
			if (!parse_semi (cfile))
				break;
			continue;
		}

		if (token == GROUP) {
			struct group_object *go;
			token = next_token (&val, cfile);
			token = next_token (&val, cfile);
			if (token != STRING && !is_identifier (token)) {
				parse_warn (cfile,
					    "expecting string or identifier.");
				skip_to_rbrace (cfile, 1);
				break;
			}
			go = ((struct group_object *)
			      hash_lookup (group_name_hash,
					   (const unsigned char *)val,
					   strlen (val)));
			if (!go) {
			    parse_warn (cfile, "unknown group %s in host %s",
					val, host -> name);
			} else {
				if (host -> named_group)
					omapi_object_dereference
						((omapi_object_t **)
						 &host -> named_group,
						 "parse_host_declaration");
				omapi_object_reference
					((omapi_object_t **)
					 &host -> named_group,
					 (omapi_object_t *)go,
					 "parse_host_declaration");
			}
			if (!parse_semi (cfile))
				break;
			continue;
		}

		if (token == UID) {
			const char *s;
			unsigned char *t = 0;
			unsigned len;

			token = next_token (&val, cfile);
			data_string_forget (&host -> client_identifier,
					    "parse_host_declaration");

			/* See if it's a string or a cshl. */
			token = peek_token (&val, cfile);
			if (token == STRING) {
				token = next_token (&val, cfile);
				s = val;
				len = strlen (val);
				host -> client_identifier.terminated = 1;
			} else {
				len = 0;
				t = parse_numeric_aggregate
					(cfile,
					 (unsigned char *)0, &len, ':', 16, 8);
				if (!t) {
					parse_warn (cfile,
						    "expecting hex list.");
					skip_to_semi (cfile);
				}
				s = (const char *)t;
			}
			if (!buffer_allocate
			    (&host -> client_identifier.buffer,
			     len + host -> client_identifier.terminated,
			     "parse_host_declaration"))
				log_fatal ("no memory for uid for host %s.",
					   host -> name);
			host -> client_identifier.data =
				host -> client_identifier.buffer -> data;
			host -> client_identifier.len = len;
			memcpy (host -> client_identifier.buffer -> data, s,
				len + host -> client_identifier.terminated);
			if (t)
				dfree (t, "parse_host_declaration");

			if (!parse_semi (cfile))
				break;
			continue;
		}
		declaration = parse_statement (cfile, host -> group,
					       HOST_DECL, host,
					       declaration);
	} while (1);

	if (deleted) {
		struct host_decl *hp =
			(struct host_decl *)
			hash_lookup (host_name_hash,
				     (unsigned char *)host -> name,
				     strlen (host -> name));
		if (hp) {
			delete_host (hp, 0);
		}
		dfree (host -> name, "parse_host_declaration");
		if (host -> group)
			free_group (host -> group, "parse_host_declaration");
		dfree (host, "parse_host_declaration");
	} else {
		if (host -> named_group && host -> named_group -> group) {
			if (host -> group -> statements ||
			    (host -> group -> authoritative !=
			     host -> named_group -> group -> authoritative))
				host -> group -> next =
					host -> named_group -> group;
			else {
				dfree (host -> group,
				       "parse_host_declaration");
				host -> group = host -> named_group -> group;
			}
		}
				
		if (dynamicp)
			host -> flags |= HOST_DECL_DYNAMIC;
		else
			host -> flags |= HOST_DECL_STATIC;

		status = enter_host (host, dynamicp, 0);
		if (status != ISC_R_SUCCESS)
			parse_warn (cfile, "host %s: %s", host -> name,
				    isc_result_totext (status));
	}
}

/* class-declaration :== STRING LBRACE parameters declarations RBRACE
*/

struct class *parse_class_declaration (cfile, group, type)
	struct parse *cfile;
	struct group *group;
	int type;
{
	const char *val;
	enum dhcp_token token;
	struct class *class = (struct class *)0, *pc;
	int declaration = 0;
	int lose = 0;
	struct data_string data;
	const char *name;
	struct executable_statement *stmt = (struct executable_statement *)0;
	struct expression *expr;
	int new = 1;

	token = next_token (&val, cfile);
	if (token != STRING) {
		parse_warn (cfile, "Expecting class name");
		skip_to_semi (cfile);
		return (struct class *)0;
	}

	/* See if there's already a class with the specified name. */
	pc = (struct class *)find_class (val);

	/* If this isn't a subclass, we're updating an existing class. */
	if (pc && type != 0 && type != 1 && type != 3) {
		class = pc;
		new = 0;
		pc = (struct class *)0;
	}

	/* If this _is_ a subclass, there _must_ be a class with the
	   same name. */
	if (!pc && (type == 0 || type == 1 || type == 3)) {
		parse_warn (cfile, "no class named %s", val);
		skip_to_semi (cfile);
		return (struct class *)0;
	}

	/* The old vendor-class and user-class declarations had an implicit
	   match.   We don't do the implicit match anymore.   Instead, for
	   backward compatibility, we have an implicit-vendor-class and an
	   implicit-user-class.   vendor-class and user-class declarations
	   are turned into subclasses of the implicit classes, and the
	   submatch expression of the implicit classes extracts the contents of
	   the vendor class or user class. */
	if (type == 0 || type == 1) {
		data.len = strlen (val);
		data.buffer = (struct buffer *)0;
		if (!buffer_allocate (&data.buffer,
				      data.len + 1, "parse_class_declaration"))
			log_fatal ("no memoy for class name.");
		data.data = &data.buffer -> data [0];
		data.terminated = 1;

		name = type ? "implicit-vendor-class" : "implicit-user-class";
	} else if (type == 2) {
		char *tname;
		if (!(tname = dmalloc (strlen (val) + 1,
				       "parse_class_declaration")))
			log_fatal ("No memory for class name %s.", val);
		strcpy (tname, val);
		name = tname;
	} else {
		name = (char *)0;
	}

	/* If this is a straight subclass, parse the hash string. */
	if (type == 3) {
		token = peek_token (&val, cfile);
		if (token == STRING) {
			token = next_token (&val, cfile);
			data.len = strlen (val);
			data.buffer = (struct buffer *)0;
			if (!buffer_allocate (&data.buffer, data.len + 1,
					     "parse_class_declaration"))
				return (struct class *)0;
			data.terminated = 1;
			data.data = &data.buffer -> data [0];
			strcpy ((char *)data.buffer -> data, val);
		} else if (token == NUMBER_OR_NAME || token == NUMBER) {
			memset (&data, 0, sizeof data);
			if (!parse_cshl (&data, cfile))
				return (struct class *)0;
		} else {
			parse_warn (cfile, "Expecting string or hex list.");
			return (struct class *)0;
		}
	}

	/* See if there's already a class in the hash table matching the
	   hash data. */
	if (type == 0 || type == 1 || type == 3)
		class = ((struct class *)
			 hash_lookup (pc -> hash, data.data, data.len));

	/* If we didn't find an existing class, allocate a new one. */
	if (!class) {
		/* Allocate the class structure... */
		class = (struct class *)dmalloc (sizeof (struct class),
						 "parse_class_declaration");
		if (!class)
			log_fatal ("No memory for class %s.", val);
		memset (class, 0, sizeof *class);
		if (pc) {
			class -> group = pc -> group;
			class -> superclass = pc;
			class -> lease_limit = pc -> lease_limit;
			if (class -> lease_limit) {
				class -> billed_leases =
					dmalloc (class -> lease_limit *
						 sizeof (struct lease *),
						 "parse_class_declaration");
				if (!class -> billed_leases)
					log_fatal ("no memory for billing");
				memset (class -> billed_leases, 0,
					(class -> lease_limit *
					 sizeof class -> billed_leases));
			}
			data_string_copy (&class -> hash_string, &data,
					  "parse_class_declaration");
			if (!pc -> hash)
				pc -> hash = new_hash ();
			add_hash (pc -> hash,
				  class -> hash_string.data,
				  class -> hash_string.len,
				  (unsigned char *)class);
		} else {
			class -> group =
				clone_group (group, "parse_class_declaration");
		}

		/* If this is an implicit vendor or user class, add a
		   statement that causes the vendor or user class ID to
		   be sent back in the reply. */
		if (type == 0 || type == 1) {
			stmt = ((struct executable_statement *)
				dmalloc (sizeof (struct executable_statement),
					 "implicit user/vendor class"));
			if (!stmt)
				log_fatal ("no memory for class statement.");
			memset (stmt, 0, sizeof *stmt);
			stmt -> op = supersede_option_statement;
			if (option_cache_allocate (&stmt -> data.option,
						   "parse_class_statement")) {
				stmt -> data.option -> data = data;
				stmt -> data.option -> option =
					dhcp_universe.options
					[type
					? DHO_VENDOR_CLASS_IDENTIFIER
					: DHO_USER_CLASS];
			}
			class -> statements = stmt;
		}

		/* Save the name, if there is one. */
		class -> name = name;
	}

	if (type == 0 || type == 1 || type == 3)
		data_string_forget (&data, "parse_class_declaration");

	/* Spawned classes don't have their own settings. */
	if (class -> superclass) {
		token = peek_token (&val, cfile);
		if (token == SEMI) {
			next_token (&val, cfile);
			return class;
		}
		/* Give the subclass its own group. */
		class -> group = clone_group (class -> group,
					      "parse_class_declaration");
	}

	if (!parse_lbrace (cfile))
		return (struct class *)0;

	do {
		token = peek_token (&val, cfile);
		if (token == RBRACE) {
			token = next_token (&val, cfile);
			break;
		} else if (token == EOF) {
			token = next_token (&val, cfile);
			parse_warn (cfile, "unexpected end of file");
			break;
		} else if (token == MATCH) {
			if (pc) {
				parse_warn (cfile,
					    "invalid match in subclass.");
				skip_to_semi (cfile);
				break;
			}
			if (class -> expr) {
				parse_warn (cfile, "can't override match.");
				skip_to_semi (cfile);
				break;
			}
			token = next_token (&val, cfile);
			token = peek_token (&val, cfile);
			if (token != IF)
				goto submatch;
			token = next_token (&val, cfile);
			parse_boolean_expression (&class -> expr, cfile,
						  &lose);
			if (lose)
				break;
#if defined (DEBUG_EXPRESSION_PARSE)
			print_expression ("class match", class -> expr);
#endif
			parse_semi (cfile);
		} else if (token == SPAWN) {
			if (pc) {
				parse_warn (cfile,
					    "invalid spawn in subclass.");
				skip_to_semi (cfile);
				break;
			}
			token = next_token (&val, cfile);
			token = next_token (&val, cfile);
			if (token != WITH) {
				parse_warn (cfile,
					    "expecting with after spawn");
				skip_to_semi (cfile);
				break;
			}
			class -> spawning = 1;
		      submatch:
			if (class -> submatch) {
				parse_warn (cfile,
					    "can't override existing %s.",
					    "submatch/spawn");
				skip_to_semi (cfile);
				break;
			}
			parse_data_expression (&class -> submatch,
					       cfile, &lose);
			if (lose)
				break;
#if defined (DEBUG_EXPRESSION_PARSE)
			print_expression ("class submatch",
					  class -> submatch);
#endif
			parse_semi (cfile);
		} else if (token == LEASE) {
			next_token (&val, cfile);
			token = next_token (&val, cfile);
			if (token != LIMIT) {
				parse_warn (cfile, "expecting \"limit\"");
				if (token != SEMI)
					skip_to_semi (cfile);
				break;
			}
			token = next_token (&val, cfile);
			if (token != NUMBER) {
				parse_warn (cfile, "expecting a number");
				if (token != SEMI)
					skip_to_semi (cfile);
				break;
			}
			class -> lease_limit = atoi (val);
			class -> billed_leases =
				dmalloc (class -> lease_limit *
					 sizeof (struct lease *),
					 "parse_class_declaration");
			if (!class -> billed_leases)
				log_fatal ("no memory for billed leases.");
			memset (class -> billed_leases, 0,
				(class -> lease_limit *
				 sizeof class -> billed_leases));
			have_billing_classes = 1;
			parse_semi (cfile);
		} else {
			declaration = parse_statement (cfile, class -> group,
						       CLASS_DECL,
						       (struct host_decl *)0,
						       declaration);
		}
	} while (1);
	if (type == 2 && new) {
		if (!collections -> classes)
			collections -> classes = class;
		else {
			struct class *cp;
			for (cp = collections -> classes;
			     cp -> nic; cp = cp -> nic)
				;
			cp -> nic = class;
		}
	}
	return class;
}

/* shared-network-declaration :==
			hostname LBRACE declarations parameters RBRACE */

void parse_shared_net_declaration (cfile, group)
	struct parse *cfile;
	struct group *group;
{
	const char *val;
	enum dhcp_token token;
	struct shared_network *share;
	char *name;
	int declaration = 0;

	share = new_shared_network ("parse_shared_net_declaration");
	if (!share)
		log_fatal ("No memory for shared subnet");
	share -> pools = (struct pool *)0;
	share -> next = (struct shared_network *)0;
	share -> interface = (struct interface_info *)0;
	share -> group = clone_group (group, "parse_shared_net_declaration");
	share -> group -> shared_network = share;

	/* Get the name of the shared network... */
	token = peek_token (&val, cfile);
	if (token == STRING) {
		token = next_token (&val, cfile);

		if (val [0] == 0) {
			parse_warn (cfile, "zero-length shared network name");
			val = "<no-name-given>";
		}
		name = malloc (strlen (val) + 1);
		if (!name)
			log_fatal ("no memory for shared network name");
		strcpy (name, val);
	} else {
		name = parse_host_name (cfile);
		if (!name)
			return;
	}
	share -> name = name;

	if (!parse_lbrace (cfile))
		return;

	do {
		token = peek_token (&val, cfile);
		if (token == RBRACE) {
			token = next_token (&val, cfile);
			if (!share -> subnets) {
				parse_warn (cfile,
					    "empty shared-network decl");
				return;
			}
			enter_shared_network (share);
			return;
		} else if (token == EOF) {
			token = next_token (&val, cfile);
			parse_warn (cfile, "unexpected end of file");
			break;
		} else if (token == INTERFACE) {
			token = next_token (&val, cfile);
			token = next_token (&val, cfile);
			new_shared_network_interface (cfile, share, val);
			if (!parse_semi (cfile))
				break;
			continue;
		}

		declaration = parse_statement (cfile, share -> group,
					       SHARED_NET_DECL,
					       (struct host_decl *)0,
					       declaration);
	} while (1);
}

/* subnet-declaration :==
	net NETMASK netmask RBRACE parameters declarations LBRACE */

void parse_subnet_declaration (cfile, share)
	struct parse *cfile;
	struct shared_network *share;
{
	const char *val;
	enum dhcp_token token;
	struct subnet *subnet, *t, *u;
	struct iaddr iaddr;
	unsigned char addr [4];
	unsigned len = sizeof addr;
	int declaration = 0;
	struct interface_info *ip;

	subnet = new_subnet ("parse_subnet_declaration");
	if (!subnet)
		log_fatal ("No memory for new subnet");
	subnet -> shared_network = share;
	subnet -> group = clone_group (share -> group,
				       "parse_subnet_declaration");
	subnet -> group -> subnet = subnet;

	/* Get the network number... */
	if (!parse_numeric_aggregate (cfile, addr, &len, DOT, 10, 8))
		return;
	memcpy (iaddr.iabuf, addr, len);
	iaddr.len = len;
	subnet -> net = iaddr;

	token = next_token (&val, cfile);
	if (token != NETMASK) {
		parse_warn (cfile, "Expecting netmask");
		skip_to_semi (cfile);
		return;
	}

	/* Get the netmask... */
	if (!parse_numeric_aggregate (cfile, addr, &len, DOT, 10, 8))
		return;
	memcpy (iaddr.iabuf, addr, len);
	iaddr.len = len;
	subnet -> netmask = iaddr;

	enter_subnet (subnet);

	if (!parse_lbrace (cfile))
		return;

	do {
		token = peek_token (&val, cfile);
		if (token == RBRACE) {
			token = next_token (&val, cfile);
			break;
		} else if (token == EOF) {
			token = next_token (&val, cfile);
			parse_warn (cfile, "unexpected end of file");
			break;
		} else if (token == INTERFACE) {
			token = next_token (&val, cfile);
			token = next_token (&val, cfile);
			new_shared_network_interface (cfile, share, val);
			if (!parse_semi (cfile))
				break;
			continue;
		}
		declaration = parse_statement (cfile, subnet -> group,
					       SUBNET_DECL,
					       (struct host_decl *)0,
					       declaration);
	} while (1);

	/* Add the subnet to the list of subnets in this shared net. */
	if (!share -> subnets)
		share -> subnets = subnet;
	else {
		u = (struct subnet *)0;
		for (t = share -> subnets;
		     t -> next_sibling; t = t -> next_sibling) {
			if (subnet_inner_than (subnet, t, 0)) {
				if (u)
					u -> next_sibling = subnet;
				else
					share -> subnets = subnet;
				subnet -> next_sibling = t;
				return;
			}
			u = t;
		}
		t -> next_sibling = subnet;
	}
}

/* group-declaration :== RBRACE parameters declarations LBRACE */

void parse_group_declaration (cfile, group)
	struct parse *cfile;
	struct group *group;
{
	const char *val;
	enum dhcp_token token;
	struct group *g;
	int declaration = 0;
	struct group_object *t;
	isc_result_t status;
	char *name = NULL;
	int deletedp = 0;
	int dynamicp = 0;
	int staticp = 0;

	g = clone_group (group, "parse_group_declaration");

	token = peek_token (&val, cfile);
	if (is_identifier (token) || token == STRING) {
		next_token (&val, cfile);
		
		name = dmalloc (strlen (val) + 1, "parse_group_declaration");
		if (!name)
			log_fatal ("no memory for group decl name %s", val);
		strcpy (name, val);
	}		

	if (!parse_lbrace (cfile))
		return;

	do {
		token = peek_token (&val, cfile);
		if (token == RBRACE) {
			token = next_token (&val, cfile);
			break;
		} else if (token == EOF) {
			token = next_token (&val, cfile);
			parse_warn (cfile, "unexpected end of file");
			break;
		} else if (token == DELETED) {
			token = next_token (&val, cfile);
			parse_semi (cfile);
			deletedp = 1;
		} else if (token == DYNAMIC) {
			token = next_token (&val, cfile);
			parse_semi (cfile);
			dynamicp = 1;
		} else if (token == STATIC) {
			token = next_token (&val, cfile);
			parse_semi (cfile);
			staticp = 1;
		}
		declaration = parse_statement (cfile, g, GROUP_DECL,
					       (struct host_decl *)0,
					       declaration);
	} while (1);

	if (name) {
		if (deletedp) {
			if (group_name_hash) {
				t = ((struct group_object *)
				     hash_lookup (group_name_hash,
						  (unsigned char *)name,
						  strlen (name)));
				if (t) {
					delete_group (t, 0);
				}
			}
		} else {
			t = dmalloc (sizeof *t, "parse_group_declaration");
			if (!t)
				log_fatal ("no memory for group decl %s", val);
			memset (t, 0, sizeof *t);
			t -> type = dhcp_type_group;
			t -> group = g;
			t -> name = name;
			t -> flags = ((staticp ? GROUP_OBJECT_STATIC : 0) |
				      (dynamicp ? GROUP_OBJECT_DYNAMIC : 0) |
				      (deletedp ? GROUP_OBJECT_DELETED : 0));
			supersede_group (t, 0);
		}
	}
}

/* fixed-addr-parameter :== ip-addrs-or-hostnames SEMI
   ip-addrs-or-hostnames :== ip-addr-or-hostname
			   | ip-addrs-or-hostnames ip-addr-or-hostname */

int parse_fixed_addr_param (oc, cfile)
	struct option_cache **oc;
	struct parse *cfile;
{
	const char *val;
	enum dhcp_token token;
	struct expression *expr = (struct expression *)0;
	struct expression *tmp, *new;
	int status;

	do {
		tmp = (struct expression *)0;
		if (parse_ip_addr_or_hostname (&tmp, cfile, 1)) {
			if (expr) {
				new = (struct expression *)0;
				status = make_concat (&new, expr, tmp);
				expression_dereference
					(&expr, "parse_fixed_addr_param");
				expression_dereference
					(&tmp, "parse_fixed_addr_param");
				if (status)
					return 0;
				expr = new;
			} else
				expr = tmp;
		} else {
			if (expr)
				expression_dereference
					(&expr, "parse_fixed_addr_param");
			return 0;
		}
		token = peek_token (&val, cfile);
		if (token == COMMA)
			token = next_token (&val, cfile);
	} while (token == COMMA);

	if (!parse_semi (cfile)) {
		if (expr)
			expression_dereference (&expr,
						"parse_fixed_addr_param");
		return 0;
	}
	status = option_cache (oc, (struct data_string *)0, expr,
			       (struct option *)0);
	expression_dereference (&expr, "parse_fixed_addr_param");
	return status;
}

/* timestamp :== date

   Timestamps are actually not used in dhcpd.conf, which is a static file,
   but rather in the database file and the journal file.  (Okay, actually
   they're not even used there yet). */

TIME parse_timestamp (cfile)
	struct parse *cfile;
{
	TIME rv;

	rv = parse_date (cfile);
	return rv;
}
		
/* lease_declaration :== LEASE ip_address LBRACE lease_parameters RBRACE

   lease_parameters :== <nil>
		      | lease_parameter
		      | lease_parameters lease_parameter

   lease_parameter :== STARTS date
		     | ENDS date
		     | TIMESTAMP date
		     | HARDWARE hardware-parameter
		     | UID hex_numbers SEMI
		     | DDNS_FWD_NAME hostname
		     | DDNS_REV_NAME hostname
		     | HOSTNAME hostname SEMI
		     | CLIENT_HOSTNAME hostname SEMI
		     | CLASS identifier SEMI
		     | DYNAMIC_BOOTP SEMI */

struct lease *parse_lease_declaration (cfile)
	struct parse *cfile;
{
	const char *val;
	enum dhcp_token token;
	unsigned char addr [4];
	unsigned len = sizeof addr;
	int seenmask = 0;
	int seenbit;
	char tbuf [32];
	static struct lease lease;
	struct executable_statement *on;
	int lose;
	TIME t;

	/* Zap the lease structure... */
	memset (&lease, 0, sizeof lease);

	/* Get the address for which the lease has been issued. */
	if (!parse_numeric_aggregate (cfile, addr, &len, DOT, 10, 8))
		return (struct lease *)0;
	memcpy (lease.ip_addr.iabuf, addr, len);
	lease.ip_addr.len = len;

	if (!parse_lbrace (cfile))
		return (struct lease *)0;

	do {
		token = next_token (&val, cfile);
		if (token == RBRACE)
			break;
		else if (token == EOF) {
			parse_warn (cfile, "unexpected end of file");
			break;
		}
		strncpy (tbuf, val, sizeof tbuf);
		tbuf [(sizeof tbuf) - 1] = 0;

		/* Parse any of the times associated with the lease. */
		switch (token) {
		      case STARTS:
		      case ENDS:
		      case TIMESTAMP:
		      case TSTP:
		      case TSFP:
			t = parse_date (cfile);
			switch (token) {
			      case STARTS:
				seenbit = 1;
				lease.starts = t;
				break;
			
			      case ENDS:
				seenbit = 2;
				lease.ends = t;
				break;
				
			      case TIMESTAMP:
				seenbit = 4;
				lease.timestamp = t;
				break;

			      case TSTP:
				seenbit = 65536;
				lease.tstp = t;
				break;
				
			      case TSFP:
				seenbit = 131072;
				lease.tsfp = t;
				break;
				
			      default: /* for gcc, we'll never get here. */
			}
			break;

			/* Colon-seperated hexadecimal octets... */
		      case UID:
			seenbit = 8;
			token = peek_token (&val, cfile);
			if (token == STRING) {
				unsigned char *tuid;
				token = next_token (&val, cfile);
				lease.uid_len = strlen (val);
				tuid = (unsigned char *)malloc (lease.uid_len);
				if (!tuid) {
					log_error ("no space for uid");
					return (struct lease *)0;
				}
				memcpy (tuid, val, lease.uid_len);
				lease.uid = tuid;
			} else {
				lease.uid_len = 0;
				lease.uid = (parse_numeric_aggregate
					     (cfile, (unsigned char *)0,
					      &lease.uid_len, ':', 16, 8));
				if (!lease.uid) {
					log_error ("no space for uid");
					return (struct lease *)0;
				}
				if (lease.uid_len == 0) {
					lease.uid = (unsigned char *)0;
					parse_warn (cfile, "zero-length uid");
					seenbit = 0;
					parse_semi (cfile);
					break;
				}
			}
			parse_semi (cfile);
			if (!lease.uid) {
				log_fatal ("No memory for lease uid");
			}
			break;
			
		      case CLASS:
			seenbit = 32;
			token = next_token (&val, cfile);
			if (!is_identifier (token)) {
				if (token != SEMI)
					skip_to_rbrace (cfile, 1);
				return (struct lease *)0;
			}
			parse_semi (cfile);
			/* for now, we aren't using this. */
			break;

		      case HARDWARE:
			seenbit = 64;
			parse_hardware_param (cfile,
					      &lease.hardware_addr);
			break;

		      case DYNAMIC_BOOTP:
			seenbit = 128;
			lease.flags |= BOOTP_LEASE;
			parse_semi (cfile);
			break;
			
		      case PEER:
			token = next_token (&val, cfile);
			if (token != IS) {
				parse_warn (cfile, "expecting \"is\".");
				skip_to_rbrace (cfile, 1);
				return (struct lease *)0;
			}
			if (token != OWNER) {
				parse_warn (cfile, "expecting \"owner\".");
				skip_to_rbrace (cfile, 1);
				return (struct lease *)0;
			}
			seenbit = 262144;
			lease.flags |= PEER_IS_OWNER;
			parse_semi (cfile);
			break;

		      case ABANDONED:
			seenbit = 256;
			lease.flags |= ABANDONED_LEASE;
			parse_semi (cfile);
			break;

		      case HOSTNAME:
			seenbit = 512;
			token = peek_token (&val, cfile);
			if (token == STRING)
				lease.hostname = parse_string (cfile);
			else {
				lease.hostname = parse_host_name (cfile);
				if (lease.hostname)
					parse_semi (cfile);
			}
			if (!lease.hostname) {
				seenbit = 0;
				return (struct lease *)0;
			}
			break;
			
		      case CLIENT_HOSTNAME:
			seenbit = 1024;
			token = peek_token (&val, cfile);
			if (token == STRING)
				lease.client_hostname = parse_string (cfile);
			else {
				lease.client_hostname =
					parse_host_name (cfile);
				if (lease.client_hostname)
					parse_semi (cfile);
			}
			if (!lease.client_hostname) {
				seenbit = 0;
				return (struct lease *)0;
			}
			break;
			
		      case BILLING:
			seenbit = 2048;
			token = next_token (&val, cfile);
			if (token == CLASS) {
				token = next_token (&val, cfile);
				if (token != STRING) {
					parse_warn (cfile, "expecting string");
					if (token != SEMI)
						skip_to_semi (cfile);
					token = BILLING;
					break;
				}
				lease.billing_class = find_class (val);
				if (!lease.billing_class)
					parse_warn (cfile,
						    "unknown class %s", val);
				parse_semi (cfile);
			} else if (token == SUBCLASS) {
				lease.billing_class =
					(parse_class_declaration
					 (cfile, (struct group *)0, 3));
			} else {
				parse_warn (cfile, "expecting \"class\"");
				if (token != SEMI)
					skip_to_semi (cfile);
			}
			break;

		      case ON:
			on = (struct executable_statement *)0;
			lose = 0;
			if (!parse_on_statement (&on, cfile, &lose)) {
				skip_to_rbrace (cfile, 1);
				return (struct lease *)0;
			}
			if ((on -> data.on.evtypes & ON_EXPIRY) &&
			    on -> data.on.statements) {
				seenbit = 16384;
				executable_statement_reference
					(&lease.on_expiry,
					 on -> data.on.statements,
					 "parse_lease_declaration");
			}
			if ((on -> data.on.evtypes & ON_RELEASE) &&
			    on -> data.on.statements) {
				seenbit = 32768;
				executable_statement_reference
					(&lease.on_release,
					 on -> data.on.statements,
					 "parse_lease_declaration");
			}
			executable_statement_dereference
				(&on, "parse_lease_declaration");
			break;
			
		      default:
			skip_to_semi (cfile);
			seenbit = 0;
			return (struct lease *)0;
		}

		if (seenmask & seenbit) {
			parse_warn (cfile,
				    "Too many %s parameters in lease %s\n",
				    tbuf, piaddr (lease.ip_addr));
		} else
			seenmask |= seenbit;

	} while (1);
	return &lease;
}

/* address-range-declaration :== ip-address ip-address SEMI
			       | DYNAMIC_BOOTP ip-address ip-address SEMI */

void parse_address_range (cfile, group, type, pool)
	struct parse *cfile;
	struct group *group;
	int type;
	struct pool *pool;
{
	struct iaddr low, high, net;
	unsigned char addr [4];
	unsigned len = sizeof addr;
	enum dhcp_token token;
	const char *val;
	int dynamic = 0;
	struct subnet *subnet;
	struct shared_network *share;
	struct pool *p;

	if ((token = peek_token (&val, cfile)) == DYNAMIC_BOOTP) {
		token = next_token (&val, cfile);
		dynamic = 1;
	}

	/* Get the bottom address in the range... */
	if (!parse_numeric_aggregate (cfile, addr, &len, DOT, 10, 8))
		return;
	memcpy (low.iabuf, addr, len);
	low.len = len;

	/* Only one address? */
	token = peek_token (&val, cfile);
	if (token == SEMI)
		high = low;
	else {
	/* Get the top address in the range... */
		if (!parse_numeric_aggregate (cfile, addr, &len, DOT, 10, 8))
			return;
		memcpy (high.iabuf, addr, len);
		high.len = len;
	}

	token = next_token (&val, cfile);
	if (token != SEMI) {
		parse_warn (cfile, "semicolon expected.");
		skip_to_semi (cfile);
		return;
	}

	if (type == SUBNET_DECL) {
		subnet = group -> subnet;
		share = subnet -> shared_network;
	} else {
		share = group -> shared_network;
		for (subnet = share -> subnets;
		     subnet; subnet = subnet -> next_sibling) {
			net = subnet_number (low, subnet -> netmask);
			if (addr_eq (net, subnet -> net))
				break;
		}
		if (!subnet) {
			parse_warn (cfile, "address range not on network %s",
				    group -> shared_network -> name);
			log_error ("Be sure to place pool statement after %s",
				   "related subnet declarations.");
			return;
		}
	}

	if (!pool) {
		struct pool *last;

#if defined (FAILOVER_PROTOCOL)
		if (pool -> failover_peer && dynamic) {
			/* Doctor, do you think I'm overly sensitive
			   about getting bug reports I can't fix? */
			parse_warn (cfile, "dynamic-bootp flag is %s",
				    "not permitted for address");
			log_error ("range declarations where there is %s",
				   "a failover");
			log_error ("peer in scope.   If you wish to %s",
				   "declare an");
			log_error ("address range from which dynamic %s",
				   "bootp leases");
			log_error ("can be allocated, please declare %s",
				   "it within a");
			log_error ("pool declaration that also contains %s",
				   "the \"no");
			log_error ("failover\" statement.   The %s",
				   "failover protocol");
			log_error ("itself does not permit dynamic %s",
				   "bootp - this");
			log_error ("is not a limitation specific to %s",
				   "the ISC DHCP");
			log_error ("server.   Please don't ask me to %s",
				   "defend this");
			log_error ("until you have read and really tried %s",
				   "to understand");
			log_error ("the failover protocol specification.");

			/* We don't actually bomb at this point - instead,
			   we let parse_lease_file notice the error and
			   bomb at that point - it's easier. */
		}
#endif /* FAILOVER_PROTOCOL */
		
		/* If we're permitting dynamic bootp for this range,
		   then look for a pool with an empty prohibit list and
		   a permit list with one entry which permits dynamic
		   bootp. */
		for (pool = share -> pools; pool; pool = pool -> next) {
			if ((!dynamic &&
			     !pool -> permit_list && !pool -> prohibit_list) ||
			    (dynamic &&
			     !pool -> prohibit_list &&
			     pool -> permit_list &&
			     !pool -> permit_list -> next &&
			     (pool -> permit_list -> type ==
			      permit_all_clients))) {
				break;
			}
			last = pool;
		}

		/* If we didn't get a pool, make one. */
		if (!pool) {
			pool = new_pool ("parse_address_range");
			if (!pool)
				log_fatal ("no memory for ad-hoc pool.");
			if (dynamic) {
				pool -> permit_list =
					new_permit ("parse_address_range");
				if (!pool -> permit_list)
					log_fatal ("no memory for ad-hoc %s.",
						   "permit");
				pool -> permit_list -> type =
					permit_all_clients;
			}
			if (share -> pools)
				last -> next = pool;
			else
				share -> pools = pool;
			pool -> shared_network = share;
			pool -> group = clone_group (share -> group,
						     "parse_address_range");
		}
	}

	/* Create the new address range... */
	new_address_range (low, high, subnet, pool);
}

/* allow-deny-keyword :== BOOTP
   			| BOOTING
			| DYNAMIC_BOOTP
			| UNKNOWN_CLIENTS */

int parse_allow_deny (oc, cfile, flag)
	struct option_cache **oc;
	struct parse *cfile;
	int flag;
{
	enum dhcp_token token;
	const char *val;
	unsigned char rf = flag;
	struct expression *data = (struct expression *)0;
	int status;

	if (!make_const_data (&data, &rf, 1, 0, 1))
		return 0;

	token = next_token (&val, cfile);
	switch (token) {
	      case BOOTP:
		status = option_cache (oc, (struct data_string *)0, data,
				       &server_options [SV_ALLOW_BOOTP]);
		break;

	      case BOOTING:
		status = option_cache (oc, (struct data_string *)0, data,
				       &server_options [SV_ALLOW_BOOTING]);
		break;

	      case DYNAMIC_BOOTP:
		status = option_cache (oc, (struct data_string *)0, data,
				       &server_options [SV_DYNAMIC_BOOTP]);
		break;

	      case UNKNOWN_CLIENTS:
		status = (option_cache
			  (oc, (struct data_string *)0, data,
			   &server_options [SV_BOOT_UNKNOWN_CLIENTS]));
		break;

	      case DUPLICATES:
		status = option_cache (oc, (struct data_string *)0, data,
				       &server_options [SV_DUPLICATES]);
		break;

	      case DECLINES:
		status = option_cache (oc, (struct data_string *)0, data,
				       &server_options [SV_DECLINES]);
		break;

	      default:
		parse_warn (cfile, "expecting allow/deny key");
		skip_to_semi (cfile);
		return 0;
	}
	parse_semi (cfile);
	return status;
}


/* Site-specific definitions.

   For supported systems, you shouldn't need to make any changes here.
   However, you may want to, in order to deal with site-specific
   differences. */

/* Add any site-specific definitions and inclusions here... */

/* #include <site-foo-bar.h> */
/* #define SITE_FOOBAR */

/* Define this if you don't want dhcpd to run as a daemon and do want
   to see all its output printed to stdout instead of being logged via
   syslog().   This also makes dhcpd use the dhcpd.conf in its working
   directory and write the dhcpd.leases file there. */

/* #define DEBUG */

/* Define this to see what the parser is parsing.   You probably don't
   want to see this. */

/* #define DEBUG_TOKENS */

/* Define this to see dumps of incoming and outgoing packets.    This
   slows things down quite a bit... */

/* #define DEBUG_PACKET */

/* Define this if you want to see dumps of expression evaluation. */

/* #define DEBUG_EXPRESSIONS */

/* Define this if you want to see dumps of find_lease() in action. */

/* #define DEBUG_FIND_LEASE */

/* Define this if you want to see dumps of parsed expressions. */

/* #define DEBUG_EXPRESSION_PARSE */

/* Define this if you want to watch the class matching process. */

/* #define DEBUG_CLASS_MATCHING */

/* Define this if you want to track memory usage for the purpose of
   noticing memory leaks quickly. */

/* #define DEBUG_MEMORY_LEAKAGE */
/* #define DEBUG_MEMORY_LEAKAGE_ON_EXIT */

/* Define this if you want exhaustive (and very slow) checking of the
   malloc pool for corruption. */

/* #define DEBUG_MALLOC_POOL */

/* Define this if you want to see a message every time a lease's state
   changes. */
/* #define DEBUG_LEASE_STATE_TRANSITIONS */

/* Define this if you want to maintain a history of the last N operations
   that changed reference counts on objects.   This can be used to debug
   cases where an object is dereferenced too often, or not often enough. */

/* #define DEBUG_RC_HISTORY */

/* Define this if you want to see the history every cycle. */

/* #define DEBUG_RC_HISTORY_EXHAUSTIVELY */

/* This is the number of history entries to maintain - by default, 256. */

/* #define RC_HISTORY_MAX 10240 */

/* Define this if you want dhcpd to dump core when a non-fatal memory
   allocation error is detected (i.e., something that would cause a
   memory leak rather than a memory smash). */

/* #define POINTER_DEBUG */

/* Define this if you want debugging output for DHCP failover protocol
   messages. */

/* #define DEBUG_FAILOVER_MESSAGES */

/* Define this to include contact messages in failover message debugging.
   The contact messages are sent once per second, so this can generate a
   lot of log entries. */

/* #define DEBUG_FAILOVER_CONTACT_MESSAGES */

/* Define this if you want debugging output for DHCP failover protocol
   event timeout timing. */

/* #define DEBUG_FAILOVER_TIMING */

/* Define this if you want to include contact message timing, which is
   performed once per second and can generate a lot of log entries. */

/* #define DEBUG_FAILOVER_CONTACT_TIMING */

/* Define this if you want all leases written to the lease file, even if
   they are free leases that have never been used. */

/* #define DEBUG_DUMP_ALL_LEASES */

/* Define this if you want to debug checksum calculations */
/* #define DEBUG_CHECKSUM */

/* Define this if you want to verbosely debug checksum calculations */
/* #define DEBUG_CHECKSUM_VERBOSE */


/* Define this if you want DHCP failover protocol support in the DHCP
   server. */

/* #define FAILOVER_PROTOCOL */

/* Define this if you want DNS update functionality to be available. */

#define NSUPDATE

/* Define this if you want the dhcpd.pid file to go somewhere other than
   the default (which varies from system to system, but is usually either
   /etc or /var/run. */

/* #define _PATH_DHCPD_PID	"/var/run/dhcpd.pid" */

/* Define this if you want the dhcpd.leases file (the dynamic lease database)
   to go somewhere other than the default location, which is normally
   /etc/dhcpd.leases. */

/* #define _PATH_DHCPD_DB	"/etc/dhcpd.leases" */

/* Define this if you want the dhcpd.conf file to go somewhere other than
   the default location.   By default, it goes in /etc/dhcpd.conf. */

/* #define _PATH_DHCPD_CONF	"/etc/dhcpd.conf" */

/* Network API definitions.   You do not need to choose one of these - if
   you don't choose, one will be chosen for you in your system's config
   header.    DON'T MESS WITH THIS UNLESS YOU KNOW WHAT YOU'RE DOING!!! */

/* Define USE_SOCKETS to use the standard BSD socket API.

   On many systems, the BSD socket API does not provide the ability to
   send packets to the 255.255.255.255 broadcast address, which can
   prevent some clients (e.g., Win95) from seeing replies.   This is
   not a problem on Solaris.

   In addition, the BSD socket API will not work when more than one
   network interface is configured on the server.

   However, the BSD socket API is about as efficient as you can get, so if
   the aforementioned problems do not matter to you, or if no other
   API is supported for your system, you may want to go with it. */

/* #define USE_SOCKETS */

/* Define this to use the Sun Streams NIT API.

   The Sun Streams NIT API is only supported on SunOS 4.x releases. */

/* #define USE_NIT */

/* Define this to use the Berkeley Packet Filter API.

   The BPF API is available on all 4.4-BSD derivatives, including
   NetBSD, FreeBSD and BSDI's BSD/OS.   It's also available on
   DEC Alpha OSF/1 in a compatibility mode supported by the Alpha OSF/1
   packetfilter interface. */

/* #define USE_BPF */

/* Define this to use the raw socket API.

   The raw socket API is provided on many BSD derivatives, and provides
   a way to send out raw IP packets.   It is only supported for sending
   packets - packets must be received with the regular socket API.
   This code is experimental - I've never gotten it to actually transmit
   a packet to the 255.255.255.255 broadcast address - so use it at your
   own risk. */

/* #define USE_RAW_SOCKETS */

/* Define this to keep the old program name (e.g., "dhcpd" for
   the DHCP server) in place of the (base) name the program was
   invoked with. */

/* #define OLD_LOG_NAME */

/* Define this to change the logging facility used by dhcpd. */

/* #define DHCPD_LOG_FACILITY LOG_DAEMON */


/* Define this if you want to be able to execute external commands
   during conditional evaluation. */

/* #define ENABLE_EXECUTE */

/* Define this if you aren't debugging and you want to save memory
   (potentially a _lot_ of memory) by allocating leases in chunks rather
   than one at a time. */

#define COMPACT_LEASES

/* Define this if you want to be able to save and playback server operational
   traces. */

/* #define TRACING */

/* Define this if you want a DHCPv6 server to send replies to the
   source port of the message it received.  This is useful for testing
   but is only included for backwards compatibility. */
/* #define REPLY_TO_SOURCE_PORT */

/* Define this if you want to allow domain list in domain-name option.
   RFC2132 does not allow that behavior, but it is somewhat used due
   to historic reasons. Note that it may be removed some time in the
   future. */

#define ACCEPT_LIST_IN_DOMAIN_NAME

/* In RFC3315 section 17.2.2 stated that if the server was not going
   to be able to assign any addresses to any IAs in a subsequent Request
   from a client that the server should not include any IAs.  This
   requirement was removed in an errata from August 2010.  Define the
   following if you want the pre-errata version.  
   You should only enable this option if you have clients that
   require the original functionality. */

/* #define RFC3315_PRE_ERRATA_2010_08 */

/* In previous versions of the code when the server generates a NAK
   it doesn't attempt to determine if the configuration included a
   server ID for that client.  Defining this option causes the server
   to make a modest effort to determine the server id when building
   a NAK as a response.  This effort will only check the first subnet
   and pool associated with a shared subnet and will not check for
   host declarations.  With some configurations the server id
   computed for a NAK may not match that computed for an ACK. */

/* #define SERVER_ID_FOR_NAK */

/* When processing a request do a simple check to compare the
   server id the client sent with the one the server would send.
   In order to minimize the complexity of the code the server
   only checks for a server id option in the global and subnet
   scopes.  Complicated configurations may result in differnet
   server ids for this check and when the server id for a reply
   packet is determined, which would prohibit the server from
   responding.

   The primary use for this option is when a client broadcasts
   a request but requires the response to come from one of the
   failover peers.  An example of this would be when a client
   reboots while its lease is still active - in this case both
   servers will normally respond.  Most of the time the client
   won't check the server id and can use either of the responses.
   However if the client does check the server id it may reject
   the response if it came from the wrong peer.  If the timing
   is such that the "wrong" peer responds first most of the time
   the client may not get an address for some time.

   Currently this option is only available when failover is in
   use.

   Care should be taken before enabling this option. */

/* #define SERVER_ID_CHECK */

/* In the v6 server code log the addresses as they are assigned
   to make it easier for an admin to see what has beend done.
   This default to off to avoid changes to what is currently
   logged. */

/* #define LOG_V6_ADDRESSES */

/* Define the default prefix length passed from the client to
   the script when modifying an IPv6 IA_NA or IA_TA address.
   The two most useful values are 128 which is what the current
   specifications call for or 64 which is what has been used in
   the past.  For most OSes 128 will indicate that the address
   is a host address and doesn't include any on-link information.
   64 indicates that the first 64 bits are the subnet or on-link
   prefix. */
#define DHCLIENT_DEFAULT_PREFIX_LEN 64

/* Enable conversion at startup of leases from FTS_BACKUP to FTS_FREE
   when either their pool has no configured failover peer or 
   FAILOVER_PROTOCOL is not enabled.  This allows the leases to be
   reclaimed by the server after a pool's configuration has changed
   from failover to standalone. Prior to this such leases would remain
   stuck in the backup state. */
/* #define CONVERT_BACKUP_TO_FREE */

/* Use the older factors for scoring a lease in the v6 client code.
   The new factors cause the client to choose more bindings (IAs)
   over more addresse within a binding.  Most uses will get a
   single address in a single binding and only get an adverstise
   from a single server and there won't be a difference. */
/* #define USE_ORIGINAL_CLIENT_LEASE_WEIGHTS */

/* Print out specific error messages for dhclient, dhcpd
   or dhcrelay when processing an incorrect command line.  This
   is included for those that might require the exact error
   messages, as we don't expect that is necessary it is on by
   default. */
#define PRINT_SPECIFIC_CL_ERRORS

/* Limit the value of a file descriptor the serve will use
   when accepting a connecting request.  This can be used to
   limit the number of TCP connections that the server will
   allow at one time.  A value of 0 means there is no limit.*/
#define MAX_FD_VALUE 200

/* Enable enforcement of the require option statement as documented
 * in man page.  Instructs the dhclient, when in -6 mode, to discard
 * offered leases that do not contain all options specified as required
 * in the client's configuration file. The client already enforces this
 * in -4 mode. */
/* #define ENFORCE_DHCPV6_CLIENT_REQUIRE */

/* Enable the invocation of the client script with a FAIL state code
 * by dhclient when running in one-try mode (-T) and the attempt to
 * obtain the desired lease(s) fails. Applies to IPv4 mode only. */
/* #define CALL_SCRIPT_ON_ONETRY_FAIL */

/* lpf.c

   Linux packet filter code, contributed by Brian Murrel at Interlinx
   Support Services in Vancouver, B.C. */

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
"$Id: lpf.c,v 1.13.2.4 1999/11/03 19:50:15 mellon Exp $ Copyright (c) 1995, 1996, 1998, 1999 The Internet Software Consortium.  All rights reserved.\n";
#endif /* not lint */

#include "dhcpd.h"
#if defined (USE_LPF_SEND) || defined (USE_LPF_RECEIVE)
#include <sys/ioctl.h>
#include <sys/uio.h>

#include <asm/types.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <netinet/in_systm.h>
#include "includes/netinet/ip.h"
#include "includes/netinet/udp.h"
#include "includes/netinet/if_ether.h"

/* Reinitializes the specified interface after an address change.   This
   is not required for packet-filter APIs. */

#ifdef USE_LPF_SEND
void if_reinitialize_send (info)
	struct interface_info *info;
{
}
#endif

#ifdef USE_LPF_RECEIVE
void if_reinitialize_receive (info)
	struct interface_info *info;
{
}
#endif

/* Called by get_interface_list for each interface that's discovered.
   Opens a packet filter for each interface and adds it to the select
   mask. */

int if_register_lpf (info)
	struct interface_info *info;
{
	int sock;
	char filename[50];
	int b;
	struct sockaddr sa;

	/* Make an LPF socket. */
	if ((sock = socket(PF_PACKET, SOCK_PACKET, htons(ETH_P_ALL))) < 0) {
		if (errno == ENOPROTOOPT || errno == EPROTONOSUPPORT ||
		    errno == ESOCKTNOSUPPORT || errno == EPFNOSUPPORT ||
		    errno == EAFNOSUPPORT || errno == EINVAL)
			log_fatal ("socket: %m - make sure %s %s %s!",
				   "CONFIG_PACKET (Packet socket)",
				   "and CONFIG_FILTER (Socket Filtering) are",
				   "enabled in your kernel configuration");
		log_fatal ("Open a socket for LPF: %m");
	}

	/* Bind to the interface name */
	memset (&sa, 0, sizeof sa);
	sa.sa_family = AF_PACKET;
	strncpy (sa.sa_data, (const char *)info -> ifp, sizeof sa.sa_data);
	if (bind (sock, &sa, sizeof sa)) {
		if (errno == ENOPROTOOPT || errno == EPROTONOSUPPORT ||
		    errno == ESOCKTNOSUPPORT || errno == EPFNOSUPPORT ||
		    errno == EAFNOSUPPORT || errno == EINVAL)
			log_fatal ("bind: %m - make sure %s %s %s!",
				   "CONFIG_PACKET (Packet socket)",
				   "and CONFIG_FILTER (Socket Filtering) are",
				   "enabled in your kernel configuration");
		log_fatal ("Bind socket to interface: %m");
	}

	return sock;
}
#endif /* USE_LPF_SEND || USE_LPF_RECEIVE */

#ifdef USE_LPF_SEND
void if_register_send (info)
	struct interface_info *info;
{
	/* If we're using the lpf API for sending and receiving,
	   we don't need to register this interface twice. */
#ifndef USE_LPF_RECEIVE
	info -> wfdesc = if_register_lpf (info, interface);
#else
	info -> wfdesc = info -> rfdesc;
#endif
	if (!quiet_interface_discovery)
		log_info ("Sending on   LPF/%s/%s%s%s",
		      info -> name,
		      print_hw_addr (info -> hw_address.htype,
				     info -> hw_address.hlen,
				     info -> hw_address.haddr),
		      (info -> shared_network ? "/" : ""),
		      (info -> shared_network ?
		       info -> shared_network -> name : ""));
}
#endif /* USE_LPF_SEND */

#ifdef USE_LPF_RECEIVE
/* Defined in bpf.c.   We can't extern these in dhcpd.h without pulling
   in bpf includes... */
extern struct sock_filter dhcp_bpf_filter [];
extern int dhcp_bpf_filter_len;
extern struct sock_filter dhcp_bpf_tr_filter [];
extern int dhcp_bpf_tr_filter_len;

static void lpf_gen_filter_setup (struct interface_info *);
static void lpf_tr_filter_setup (struct interface_info *);

void if_register_receive (info)
	struct interface_info *info;
{
	/* Open a LPF device and hang it on this interface... */
	info -> rfdesc = if_register_lpf (info);

	if (info -> hw_address.htype == HTYPE_IEEE802)
		lpf_tr_filter_setup (info);
	else
		lpf_gen_filter_setup (info);

	if (!quiet_interface_discovery)
		log_info ("Listening on LPF/%s/%s%s%s",
			  info -> name,
			  print_hw_addr (info -> hw_address.htype,
					 info -> hw_address.hlen,
					 info -> hw_address.haddr),
			  (info -> shared_network ? "/" : ""),
			  (info -> shared_network ?
			   info -> shared_network -> name : ""));
}

static void lpf_gen_filter_setup (info)
	struct interface_info *info;
{
	struct sock_fprog p;

	/* Set up the bpf filter program structure.    This is defined in
	   bpf.c */
	p.len = dhcp_bpf_filter_len;
	p.filter = dhcp_bpf_filter;

        /* Patch the server port into the LPF  program...
	   XXX changes to filter program may require changes
	   to the insn number(s) used below! XXX */
	dhcp_bpf_filter [8].k = ntohs (local_port);

	if (setsockopt (info -> rfdesc, SOL_SOCKET, SO_ATTACH_FILTER, &p,
			sizeof p) < 0) {
		if (errno == ENOPROTOOPT || errno == EPROTONOSUPPORT ||
		    errno == ESOCKTNOSUPPORT || errno == EPFNOSUPPORT ||
		    errno == EAFNOSUPPORT)
			log_fatal ("socket: %m - make sure %s %s %s!",
				   "CONFIG_PACKET (Packet socket)",
				   "and CONFIG_FILTER (Socket Filtering) are",
				   "enabled in your kernel configuration");
		log_fatal ("Can't install packet filter program: %m");
	}
}

static void lpf_tr_filter_setup (info)
	struct interface_info *info;
{
	struct sock_fprog p;

	/* Set up the bpf filter program structure.    This is defined in
	   bpf.c */
	p.len = dhcp_bpf_tr_filter_len;
	p.filter = dhcp_bpf_tr_filter;

        /* Patch the server port into the LPF  program...
	   XXX changes to filter program may require changes
	   XXX to the insn number(s) used below!
	   XXX Token ring filter is null - when/if we have a filter 
	   XXX that's not, we'll need this code.
	   XXX dhcp_bpf_filter [?].k = ntohs (local_port); */

	if (setsockopt (info -> rfdesc, SOL_SOCKET, SO_ATTACH_FILTER, &p,
			sizeof p) < 0) {
		if (errno == ENOPROTOOPT || errno == EPROTONOSUPPORT ||
		    errno == ESOCKTNOSUPPORT || errno == EPFNOSUPPORT ||
		    errno == EAFNOSUPPORT)
			log_fatal ("socket: %m - make sure %s %s %s!",
				   "CONFIG_PACKET (Packet socket)",
				   "and CONFIG_FILTER (Socket Filtering) are",
				   "enabled in your kernel configuration");
		log_fatal ("Can't install packet filter program: %m");
	}
}
#endif /* USE_LPF_RECEIVE */

#ifdef USE_LPF_SEND
ssize_t send_packet (interface, packet, raw, len, from, to, hto)
	struct interface_info *interface;
	struct packet *packet;
	struct dhcp_packet *raw;
	size_t len;
	struct in_addr from;
	struct sockaddr_in *to;
	struct hardware *hto;
{
	int bufp = 0;
	unsigned char buf [1500];
	struct sockaddr sa;
	int result;

	if (!strcmp (interface -> name, "fallback"))
		return send_fallback (interface, packet, raw,
				      len, from, to, hto);

	/* Assemble the headers... */
	assemble_hw_header (interface, buf, &bufp, hto);
	assemble_udp_ip_header (interface, buf, &bufp, from.s_addr,
				to -> sin_addr.s_addr, to -> sin_port,
				(unsigned char *)raw, len);
	memcpy (buf + bufp, raw, len);

	/* For some reason, SOCK_PACKET sockets can't be connected,
	   so we have to do a sentdo every time. */
	memset (&sa, 0, sizeof sa);
	sa.sa_family = AF_PACKET;
	strncpy (sa.sa_data,
		 (const char *)interface -> ifp, sizeof sa.sa_data);

	result = sendto (interface -> wfdesc, buf, bufp + len, 0,
			 &sa, sizeof sa);
	if (result < 0)
		log_error ("send_packet: %m");
	return result;
}
#endif /* USE_LPF_SEND */

#ifdef USE_LPF_RECEIVE
ssize_t receive_packet (interface, buf, len, from, hfrom)
	struct interface_info *interface;
	unsigned char *buf;
	size_t len;
	struct sockaddr_in *from;
	struct hardware *hfrom;
{
	int nread;
	int length = 0;
	int offset = 0;
	unsigned char ibuf [1500];
	int bufix = 0;

	length = read (interface -> rfdesc, ibuf, sizeof ibuf);
	if (length <= 0)
		return length;

	bufix = 0;
	/* Decode the physical header... */
	offset = decode_hw_header (interface, ibuf, bufix, hfrom);

	/* If a physical layer checksum failed (dunno of any
	   physical layer that supports this, but WTH), skip this
	   packet. */
	if (offset < 0) {
		return 0;
	}

	bufix += offset;
	length -= offset;

	/* Decode the IP and UDP headers... */
	offset = decode_udp_ip_header (interface, ibuf, bufix,
				       from, (unsigned char *)0, length);

	/* If the IP or UDP checksum was bad, skip the packet... */
	if (offset < 0)
		return 0;

	bufix += offset;
	length -= offset;

	/* Copy out the data in the packet... */
	memcpy (buf, &ibuf [bufix], length);
	return length;
}

int can_unicast_without_arp (ip)
	struct interface_info *ip;
{
	return 1;
}

int can_receive_unicast_unconfigured (ip)
	struct interface_info *ip;
{
	return 1;
}

void maybe_setup_fallback ()
{
	struct interface_info *fbi;
	fbi = setup_fallback ();
	if (fbi) {
		if_register_fallback (fbi);
		add_protocol ("fallback", fallback_interface -> wfdesc,
			      fallback_discard, fallback_interface);
	}
}
#endif

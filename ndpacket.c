/*-
 * Copyright (c) 2015-2017 Alexandre Fenyo <alex@fenyo.net> - http://www.fenyo.net
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ndpacket.c 188 2015-04-09 00:29:54Z fenyo $
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/kdb.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>

#include "ndpacket.h"
#include "ndconf.h"
#include "ndparse.h"

// Reply to neighbor solicitations with a specific neighbor advertisement, in order
// to let the uplink router send packets to a downlink router, that may or may not
// be the current host that run ndproxy.
// The current host, the uplink router and the downlink router must be attached
// to a common layer-2 link with broadcast multi-access capability.

// Neighbor solicitation messages are multicast to the solicited-node multicast
// address of the target address. Since we do not know the target address,
// we can not join the corresponding group. So, to capture the solicitation messages,
// the uplink interface must be set in permanent promiscuous mode and MLD snooping
// must be disabled on the switches that share the layer-2 link relative to
// the uplink interface. Note that MLD snooping must not be disabled entirely on
// each switch, but only on the corresponding vlan.

// called by pfil_run_hooks() @ ip6_input.c:ip_input()
int packet(void *packet_arg, struct mbuf **packet_mp, struct ifnet *packet_ifnet,
		  const int packet_dir, struct inpcb *packet_inpcb) {
  struct mbuf *m = NULL, *mreply = NULL;
  struct ip6_hdr *ip6, *ip6reply;
  struct icmp6_hdr *icmp6;
  struct in6_addr srcaddr, dstaddr;
  int output_flags = 0;
  int maxlen, ret, i;

#ifdef DEBUG_NDPROXY
  // when debuging, increment counter of received packets from the uplink interface
  ndproxy_conf_count = ++ndproxy_conf_count < 0 ? 1 : ndproxy_conf_count;
#endif
  
  if (packet_mp == NULL) {
    printf("NDPROXY ERROR: no mbuf\n");
    return 0;
  }
  m = *packet_mp;

  // locate start of IPv6 header data
  ip6 = mtod(m, struct ip6_hdr *);

  // handle only packets originating from the uplink interface
  if (strcmp(if_name(packet_ifnet), ndproxy_conf_str_uplink_interface)) {
#ifdef DEBUG_NDPROXY
    printf("NDPROXY DEBUG: packets from uplink interface: %s - %d\n", if_name(packet_ifnet), ndproxy_conf_count);
#endif
    return 0;
  }

  // Handle only packets originating from one of the uplink router addresses.
  // Note that different source addresses can be choosen from the same uplink router, depending on the packet
  // that triggered the address resolution process or depending on other external factors.
  // Here are some cases when it can happen:
  // - the uplink router may have multiple interfaces;
  // - there may be multiple uplink routers;
  // - many routers choose to use a link-local address when sending neighbor solicitations,
  //   but when an administrator of such a router, also having a global address assigned on the same link,
  //   tries to send packets (echo request, for instance) to an on-link destination global address,
  //   the source address of the echo request packet prompting the solicitation may be global-scoped according
  //   to the selection algorithm described in RFC-6724. Therefore, the source address of the Neighbor Solicitation

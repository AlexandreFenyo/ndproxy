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
  //   packet should also be selected in the same global scope, according to RFC-4861 (§7.2.2);
  // - when the uplink router does not yet know its own address, it must use the unspecified address,
  //   according to RFC-4861.
  // So, it can not be assumed that an uplink router will always use the same IPv6 address to send
  // neighbor solicitations. Every assigned addresses to the downlink interface of the uplink router
  // should then be declared to ndproxy via sysctl (net.inet6.ndproxyconf_uplink_ipv6_addresses).
  // Since the unsolicited address can be used by many different nodes, another node than the uplink router could
  // make use of such a source IP. This is why if such a node exists, the unsolicited address should not be
  // declared in the net.inet6.ndproxyconf_uplink_ipv6_addresses sysctl configuration parameter.
  for (i = 0; i < ndproxy_conf_uplink_ipv6_naddresses; i++) {
#ifdef DEBUG_NDPROXY
    printf("NDPROXY INFO: compare: ");
    printf_ip6addr(ndproxy_conf_uplink_ipv6_addresses + i, false);
    printf(" (uplink router address) with ");
    #if (__FreeBSD_version < 1200000)
    printf_ip6addr(&ip6->ip6_src, false);
    #else
    printf_ip6addr((struct in6_addr *) (void *) &ip6->ip6_src, false);
    #endif
    printf(" (source address)\n");
#endif
    #if (__FreeBSD_version < 1200000)
    if (IN6_ARE_ADDR_EQUAL(ndproxy_conf_uplink_ipv6_addresses + i, &ip6->ip6_src)) break;
    #else
    if (IN6_ARE_ADDR_EQUAL(ndproxy_conf_uplink_ipv6_addresses + i, (struct in6_addr *) (void *) &ip6->ip6_src)) break;
    #endif
  }
  if (i == ndproxy_conf_uplink_ipv6_naddresses) {
#ifdef DEBUG_NDPROXY
    printf("NDPROXY INFO: not from uplink router - from: ");
    #if (__FreeBSD_version < 1200000)
    printf_ip6addr(&ip6->ip6_src, false);
    #else
    printf_ip6addr((struct in6_addr *) (void *) &ip6->ip6_src, false);
    #endif
    printf(" - %d\n", ndproxy_conf_count);
#endif
    return 0;
  }

#ifdef DEBUG_NDPROXY
  printf("NDPROXY DEBUG: got packet from uplink router - %d\n", ndproxy_conf_count);
#endif
  
  // For security reasons, we explicitely reject neighbor solicitation packets containing any extension header:
  // such a packet is mainly unattended:
  //   Fragmentation:
  //     According to RFC-6980, IPv6 fragmentation header is forbidden in all neighbor discovery messages.
  //   Hop-by-hop header:
  //     commonly used for jumbograms or for MLD. Should not involve neighbor solicitation packets.
  //   Destination mobility headers:
  //     commonly used for mobility, we do not support these headers.
  //   Routing header:
  //     commonly used for mobility or source routing, we do not support these headers.
  //   AH & ESP headers:
  //     securing the neighbor discovery process is not done with IPsec but with the SEcure Neighbor
  //     Discovery protocol (RFC-3971). We can not support RFC-3971, since proxifying ND packets is
  //     some kind of a spoofing process.
  // look for an ICMPv6 payload and no IPv6 extension header
  if (ip6->ip6_nxt != IPPROTO_ICMPV6) return 0;
#ifdef DEBUG_NDPROXY
  printf("NDPROXY DEBUG: got ICMPv6 from uplink router - %d\n", ndproxy_conf_count);
#endif

  // locate start of ICMPv6 header data
  icmp6 = (struct icmp6_hdr *) ((caddr_t) ip6 + sizeof(struct ip6_hdr));

  // check the checksum
  const u_int16_t sum = icmp6->icmp6_cksum;
  icmp6->icmp6_cksum = 0;
  if (sum != in6_cksum(m, IPPROTO_ICMPV6, sizeof(struct ip6_hdr),
		       m->m_len - sizeof(struct ip6_hdr))) {
    icmp6->icmp6_cksum = sum;
    printf("NDPROXY ERROR: bad checksum\n");
    return 0;
  }
  icmp6->icmp6_cksum = sum;

  if (icmp6->icmp6_type != ND_NEIGHBOR_SOLICIT || icmp6->icmp6_code) return 0;
#ifdef DEBUG_NDPROXY
  printf("NDPROXY DEBUG: got neighbor solicitation from ");
  #if (__FreeBSD_version < 1200000)
  printf_ip6addr(&ip6->ip6_src, true);
  #else
  printf_ip6addr((struct in6_addr *) (void *) &ip6->ip6_src, true);
  #endif
  printf("\n");
#endif

  // create a new mbuf to send a neighbor advertisement
  // ICMPv6 options are rounded up to 8 bytes alignment
  maxlen = (sizeof(struct ip6_hdr) + sizeof(struct nd_neighbor_advert) +
	    sizeof(struct nd_opt_hdr) + packet_ifnet->if_addrlen + 7) & ~7;
  if (max_linkhdr + maxlen > MCLBYTES) {
    printf("NDPROXY ERROR: reply length > MCLBYTES\n");
    return 0;
  }
  if (max_linkhdr + maxlen > MHLEN)
    mreply = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
  else
    mreply = m_gethdr(M_NOWAIT, MT_DATA);
  if (mreply == NULL) {
    printf("NDPROXY ERROR: no more mbufs (ENOBUFS)\n");
    return 0;
  }

  // this is a newly created packet
  mreply->m_pkthdr.rcvif = NULL;

  // packet content:
  // IPv6 header + ICMPv6 Neighbor Advertisement including target address + target link-layer ICMPv6 address option
  mreply->m_pkthdr.len = mreply->m_len = (sizeof(struct ip6_hdr) + sizeof(struct nd_neighbor_advert)
					  + sizeof(struct nd_opt_hdr) + packet_ifnet->if_addrlen + 7) & ~7;

  // reserve space for the link-layer header
  mreply->m_data += max_linkhdr;

  // fill in the destination address we want to reply to
  struct sockaddr_in6 dst_sa;
  bzero(&dst_sa, sizeof(struct sockaddr_in6));
  dst_sa.sin6_family = AF_INET6;
  dst_sa.sin6_len = sizeof(struct sockaddr_in6);
  dst_sa.sin6_addr = ip6->ip6_src;
  if ((ret = in6_setscope(&dst_sa.sin6_addr, packet_ifnet, NULL))) {
    printf("NDPROXY ERROR: can not set source scope id (err=%d)\n", ret);
    m_freem(mreply);
    return 0;
  }

  // According to RFC-4861 (§7.2.4), "The Target Address of the advertisement is copied from the Target Address
  // of the solicitation. [...] If the source of the solicitation is the unspecified address, the
  // node MUST [...] multicast the advertisement to the all-nodes address.".
  #if (__FreeBSD_version < 1200000)
  if (!IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src)) dstaddr = ip6->ip6_src;
  #else
  if (!IN6_IS_ADDR_UNSPECIFIED((struct in6_addr *) (void *) &ip6->ip6_src)) dstaddr = ip6->ip6_src;
  #endif
  else {
    // Check compliance to RFC-4861: "If the IP source address is the unspecified address, the IP
    // destination address is a solicited-node multicast address.".
    if (ip6->ip6_dst.s6_addr16[0] == IPV6_ADDR_INT16_MLL &&
	ip6->ip6_dst.s6_addr32[1] == 0 &&
	ip6->ip6_dst.s6_addr32[2] == IPV6_ADDR_INT32_ONE &&
	ip6->ip6_dst.s6_addr8[12] == 0xff) {
#ifdef DEBUG_NDPROXY
      printf("NDPROXY DEBUG: unspecified source address and solicited-node multicast destination address\n");
#endif
    } else {
      printf("NDPROXY ERROR: destination address should be a solicited multicast can not set source scope id (err=%d)\n", ret);
      m_freem(mreply);
      return 0;
    }

    output_flags |= M_MCAST;
    dstaddr = in6addr_linklocal_allnodes;
  }
  if ((ret = in6_setscope(&dstaddr, packet_ifnet, NULL))) {
    printf("NDPROXY ERROR: can not set destination scope id (err=%d)\n", ret);
    m_freem(mreply);
    return 0;
  }
    
  // first, apply the RFC-3484 default address selection algorithm to get a source address for the advertisement packet.
#if (__FreeBSD_version < 1100000)
  ret = in6_selectsrc(&dst_sa, NULL, NULL, NULL, NULL, NULL, &srcaddr);
#else
    uint32_t _dst_sa_scopeid;
    struct in6_addr _dst_sa;
    in6_splitscope(&dst_sa.sin6_addr, &_dst_sa, &_dst_sa_scopeid);
    ret = in6_selectsrc_addr(RT_DEFAULT_FIB, &_dst_sa,
			     _dst_sa_scopeid, packet_ifnet, &srcaddr, NULL);
#endif
  if (ret && (ret != EHOSTUNREACH || in6_addrscope(&ip6->ip6_src) == IPV6_ADDR_SCOPE_LINKLOCAL)) {
    printf("NDPROXY ERROR: can not select a source address to reply (err=%d), source scope is %x\n",
	   ret, in6_addrscope(&ip6->ip6_src));
    m_freem(mreply);
    return 0;
  }
  if (ret) {
    // secondly, try to reply with a link-local address attached to the receiving interface
    struct in6_ifaddr *llifaddr = in6ifa_ifpforlinklocal(packet_ifnet, 0);
    if (llifaddr == NULL)
      printf("NDPROXY WARNING: no link-local address attached to the receiving interface\n");
    
#ifdef DEBUG_NDPROXY
    printf("NDPROXY INFO: no address in requested scope, using a link-local address to reply\n");
#endif
    if (llifaddr != NULL) {
      // use the link-local address
      srcaddr = (llifaddr->ia_addr).sin6_addr;
      ifa_free((struct ifaddr *) llifaddr);
    } else
      // No link-local address, we may for instance currently be verifying that the link-local stateless
      // autoconfiguration address is unused.
      // Then, we temporary use the unspecified address (::).
      bzero(&srcaddr, sizeof srcaddr);
    
    // Since we have no source address in the same scope of the destination address of the request packet,
    // we can not simply reply to the source address of the request packet.
    // Then we reply to the link-local all nodes multicast address (ff02::1).
    output_flags |= M_MCAST;
    dstaddr = in6addr_linklocal_allnodes;
    if ((ret = in6_setscope(&dstaddr, packet_ifnet, NULL))) {
      printf("NDPROXY ERROR: can not set destination scope id (err=%d)\n", ret);
      m_freem(mreply);
      return 0;
    }
  }

#ifdef DEBUG_NDPROXY
  printf("NDPROXY DEBUG: source address used to reply: "); printf_ip6addr(&srcaddr, true); printf("\n");  
#endif

  struct nd_neighbor_solicit *nd_ns = (struct nd_neighbor_solicit *) (ip6 + 1);

  // fill in the IPv6 header
  ip6reply = mtod(mreply, struct ip6_hdr *);
  ip6reply->ip6_flow = 0;
  ip6reply->ip6_vfc &= ~IPV6_VERSION_MASK;
  ip6reply->ip6_vfc |= IPV6_VERSION;
  ip6reply->ip6_plen = htons((u_short) (mreply->m_len - sizeof(struct ip6_hdr)));
  ip6reply->ip6_nxt = IPPROTO_ICMPV6;
  ip6reply->ip6_hlim = 255;
  ip6reply->ip6_dst = dstaddr;
  ip6reply->ip6_src = srcaddr;

  // fill in the ICMPv6 neighbor advertisement header
  struct nd_neighbor_advert *nd_na = (struct nd_neighbor_advert *) (ip6reply + 1);  
  nd_na->nd_na_type = ND_NEIGHBOR_ADVERT;
  nd_na->nd_na_code = 0;

  nd_na->nd_na_flags_reserved = 0;
  // According to RFC-4861 (§7.2.4), "If the source of the solicitation is the unspecified address, the
  // node MUST set the Solicited flag to zero [...]"
  if (!IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src)) nd_na->nd_na_flags_reserved = ND_NA_FLAG_SOLICITED;

  // According to RFC-4861 (§7.2.4), "If the Target Address is either an anycast address or a unicast
  // address for which the node is providing proxy service, [...] the Override flag SHOULD
  // be set to zero."
  // Thus, we do not set the ND_NA_FLAG_OVERRIDE flag in nd_na->nd_na_flags_reserved.

  nd_na->nd_na_flags_reserved |= ND_NA_FLAG_ROUTER;

  // according to RFC-4861 (§7.2.3), the target address can not be a multicast address
  if (IN6_IS_ADDR_MULTICAST(&nd_ns->nd_ns_target)) {
    printf("NDPROXY WARNING: rejecting multicast target address\n");
    m_freem(mreply);
    return 0;
  }
  
  // we send a solicited neighbor advertisement relative to the target contained in the received neighbor solicitation
  nd_na->nd_na_target = nd_ns->nd_ns_target;

  // do not manage packets relative to exception target addresses
  for (i = 0; i < ndproxy_conf_exception_ipv6_naddresses; i++)
    if (IN6_ARE_ADDR_EQUAL(ndproxy_conf_exception_ipv6_addresses + i, &nd_na->nd_na_target)) {
#ifdef DEBUG_NDPROXY
      printf("NDPROXY INFO: rejecting target\n");
#endif
      m_freem(mreply);
      return 0;
    } else {
#ifdef DEBUG_NDPROXY
      printf("NDPROXY INFO: accepting target: ");
      printf_ip6addr(ndproxy_conf_exception_ipv6_addresses + i, false);
      printf(" - ");
      printf_ip6addr(&nd_na->nd_na_target, false);
      printf("\n");
#endif
    }

  // proxy to the downlink router: fill in the target link-layer address option with the MAC downlink router address
  int optlen = sizeof(struct nd_opt_hdr) + ETHER_ADDR_LEN;
  struct nd_opt_hdr *nd_opt = (struct nd_opt_hdr *) (nd_na + 1);
  // roundup to 8 bytes alignment
  optlen = (optlen + 7) & ~7;
  bzero((caddr_t) nd_opt, optlen);
  nd_opt->nd_opt_type = ND_OPT_TARGET_LINKADDR;
  nd_opt->nd_opt_len = optlen >> 3;
  bcopy(&ndproxy_conf_downlink_mac_address, (caddr_t) (nd_opt + 1), ETHER_ADDR_LEN);
#ifdef DEBUG_NDPROXY
  printf("NDPROXY INFO: mac option: ");
  printf_macaddr_network_format(&ndproxy_conf_downlink_mac_address);
  printf("\n");
#endif
  
  // compute outgoing packet checksum
  nd_na->nd_na_cksum = 0;
  nd_na->nd_na_cksum = in6_cksum(mreply, IPPROTO_ICMPV6, sizeof(struct ip6_hdr),
				 mreply->m_len - sizeof(struct ip6_hdr));

#ifdef DEBUG_NDPROXY
  printf("NDPROXY DEBUG: src="); printf_ip6addr(&ip6reply->ip6_src, false); printf(" / ");
  printf("dst="); printf_ip6addr(&ip6reply->ip6_dst, false); printf("\n");
#endif

  struct ip6_moptions im6o;
  if (output_flags & M_MCAST) {
    bzero(&im6o, sizeof im6o);
    im6o.im6o_multicast_hlim = 255;
    im6o.im6o_multicast_loop = false;
    im6o.im6o_multicast_ifp = NULL;
  }
  
  // send router advertisement
  if ((ret = ip6_output(mreply, NULL, NULL, output_flags, output_flags & M_MCAST ? &im6o : NULL, NULL, NULL))) {
    printf("NDPROXY DEBUG: can not send packet (err=%d)\n", ret);
#ifdef DEBUG_NDPROXY
    kdb_backtrace();
    m_freem(mreply);
    return 0;
#endif
  } else {
#ifdef DEBUG_NDPROXY
    printf("NDPROXY DEBUG: reply sent\n");
#endif
  }

#ifndef DEBUG_NDPROXY
  // when NOT debuging, increment counter for each neighbor advertisement sent
  ndproxy_conf_count = ++ndproxy_conf_count < 0 ? 1 : ndproxy_conf_count;
#endif

  // do not process this packet by upper layers to avoid sending another advertissement
  return 1;
}

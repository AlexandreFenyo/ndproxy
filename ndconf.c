/*-
 * Copyright (c) 2015 Alexandre Fenyo <alex@fenyo.net> - http://www.fenyo.net
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
 * $Id: ndconf.c 173 2015-03-30 00:10:36Z fenyo $
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <net/if.h>
#include <net/pfil.h>
#include <net/bpf.h>
#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>
#include <netinet6/scope6_var.h>
#include <netinet6/in6_ifattach.h>
#include <netinet6/nd6.h>
#include <net/route.h>
#include <net/ethernet.h>
#include <sys/ctype.h>

#include "ndconf.h"

// packets handled counter
int ndproxy_conf_count = 0;

// uplink interface name
char ndproxy_conf_str_uplink_interface[IFNAMSIZ] = "";

// uplink router IPv6 link-local or global addresses
struct in6_addr ndproxy_conf_uplink_ipv6_addresses[CONF_NUPLINK_MAX];
int ndproxy_conf_uplink_ipv6_naddresses = 0;

// IPv6 link-local or global exceptions address list
struct in6_addr ndproxy_conf_exception_ipv6_addresses[CONF_NEXCEPTIONS_MAX];
int ndproxy_conf_exception_ipv6_naddresses = 0;

// downlink router MAC address
struct ether_addr ndproxy_conf_downlink_mac_address;
bool ndproxy_conf_downlink_mac_address_isset = false;

// uplink router MAC address
struct ether_addr ndproxy_conf_uplink_mac_address;
bool ndproxy_conf_uplink_mac_address_isset = false;

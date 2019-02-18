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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/pfil.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <netinet/in.h>

#ifdef PFIL_VERSION
#include <netinet6/ip6_var.h>
#endif

#include "ndproxy.h"
#include "ndconf.h"
#include "ndparse.h"
#include "ndpacket.h"

static int hook_added = false;

#ifdef PFIL_VERSION

static pfil_hook_t pfh_hook;

static void register_hook() {
  struct pfil_hook_args pha;
  struct pfil_link_args pla;

  if (hook_added) return;

  pha.pa_version = PFIL_VERSION;
  pha.pa_type = PFIL_TYPE_IP6;
  pha.pa_flags = PFIL_IN;
  pha.pa_modname = "ndproxy";
  pha.pa_ruleset = NULL;
  pha.pa_rulname = "default-in6";
  pha.pa_func = packet;
  pfh_hook = pfil_add_hook(&pha);

  pla.pa_version = PFIL_VERSION;
  pla.pa_flags = PFIL_IN | PFIL_HEADPTR | PFIL_HOOKPTR;
  pla.pa_hook = pfh_hook;
  pla.pa_head = V_inet6_pfil_head;
  pfil_link(&pla);

  hook_added = true;
}

static void unregister_hook() {
  if (!hook_added) return;
  pfil_remove_hook(pfh_hook);
}

#else

static struct pfil_head *pfh_inet6 = NULL;

// when module is loaded from /boot/loader.conf, pfh_inet6 is not already initialized,
// so postpone registration
static void register_hook() {
  if (hook_added) return;
  
  if (pfh_inet6 == NULL) {
    if ((pfh_inet6 = pfil_head_get(PFIL_TYPE_AF, AF_INET6)) == NULL) {
#ifdef DEBUG_NDPROXY
      uprintf("NDPROXY WARNING: pfil_head_get returned null\n");
      printf("NDPROXY WARNING: pfil_head_get returned null\n");
#endif
      return;
    }
  }

  const int ret = pfil_add_hook(packet, NULL, PFIL_IN | PFIL_WAITOK, pfh_inet6);
  if (ret) {
#ifdef DEBUG_NDPROXY
    uprintf("NDPROXY WARNING: can not add hook (err=%d)\n", ret);
    printf("NDPROXY WARNING: can not add hook (err=%d)\n", ret);
#endif
    return;
  }
  hook_added = true;
}

static void unregister_hook() {
  int ret;

  if (hook_added && (ret = pfil_remove_hook(packet, NULL, PFIL_IN | PFIL_WAITOK, pfh_inet6))) {
#ifdef DEBUG_NDPROXY
    uprintf("NDPROXY WARNING: can not remove hook (err=%d)\n", ret);
    printf("NDPROXY WARNING: can not remove hook (err=%d)\n", ret);
#endif
  }
}

#endif

// called when the module is loaded or unloaded
static int event_handler(struct module *module, const int event, void *arg) {
  switch (event) {
  case MOD_LOAD:
    register_hook();
#ifdef DEBUG_NDPROXY
    uprintf("NDPROXY loaded\n");
    printf("NDPROXY loaded\n");
#endif
    return 0;
    // NOTREACHED
    break;

  case MOD_UNLOAD:
    unregister_hook();
#ifdef DEBUG_NDPROXY
    uprintf("NDPROXY unloaded\n");
    printf("NDPROXY unloaded\n");
#endif
    return 0;
    // NOTREACHED
    break;

  default:
    return EOPNOTSUPP;
    // NOTREACHED
    break;
  }
}

// declare module data

static moduledata_t ndproxy_conf = {
  "ndproxy",     // module name
  event_handler, // event handler
  NULL           // extra data
};
DECLARE_MODULE(ndproxy, ndproxy_conf, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);

// declare sysctl interface used to configure the behaviour of the module

SYSCTL_DECL(_net_inet6);

#define GENERIC_CB_STRING                                            \
  if (arg1 == NULL) {                                                \
    printf("NDPROXY ERROR: conf arg is null\n");                     \
    return EFAULT;                                                   \
  }                                                                  \
                                                                     \
  if (strlen((char *) arg1) > sizeof conf_str - 1) return E2BIG;     \
                                                                     \
  strncpy(conf_str, (char *) arg1, sizeof conf_str);                 \
  conf_str[(sizeof conf_str) - 1] = '\0';                            \
                                                                     \
  ret = SYSCTL_OUT(req, conf_str, sizeof conf_str);                  \
  if (ret || !req->newptr) return ret;                               \
                                                                     \
  /* the caller asks to set a new value */			     \
                                                                     \
  if ((req->newlen - req->newidx) >= arg2) return EINVAL;            \
  arg2 = (req->newlen - req->newidx);                                \
  ret = SYSCTL_IN(req, arg1, arg2);                                  \
  ((char *)arg1)[arg2] = '\0';                                       \
  if (ret) return ret;

////////////////////////////////////////////////////////////////////////////////
// net.inet6.ndproxyconf_uplink_interface

// declare the sysctl node named net.inet6.ndproxyconf_uplink_interface
SYSCTL_STRING(_net_inet6, OID_AUTO, ndproxyconf_uplink_interface, CTLFLAG_RW, ndproxy_conf_str_uplink_interface, sizeof ndproxy_conf_str_uplink_interface, "uplink interface name");

////////////////////////////////////////////////////////////////////////////////
// net.inet6.ndproxyconf_{up,down}link_mac_address

// storage string for the sysctl node named net.inet6.ndproxyconf_{up,down}link_mac_address
#if 0
// reserved for a future use
static char ndproxy_conf_str_uplink_mac_address[MACMAXSIZE + 1] = "";
#endif
static char ndproxy_conf_str_downlink_mac_address[MACMAXSIZE + 1] = "";

// get or update the value of the sysctl node named net.inet6.ndproxyconf_{up,down}link_mac_address
static int cb_string_mac_addr(SYSCTL_HANDLER_ARGS, char xconf_str[MACMAXSIZE + 1], struct ether_addr *xconf_val, bool *xconf_isset) {
  char conf_str[MACMAXSIZE + 1];
  struct ether_addr _ndproxy_conf_link_mac_address;
  int ret;

  register_hook();

  GENERIC_CB_STRING;

  if (!strlen(xconf_str)) {
    *xconf_isset = false;
    return 0;
  }

  char *curp = xconf_str;
  char tmpstr[18];
  strcpy(tmpstr, curp);
  ret = parse_mac(tmpstr, &_ndproxy_conf_link_mac_address);
  if (!ret) {
    *xconf_isset = true;
#ifdef DEBUG_NDPROXY
    printf("NDPROXY INFO: parsed address: [");
    printf_macaddr_network_format(&_ndproxy_conf_link_mac_address);
    printf("]\n");
#endif
  } else {
    strncpy(xconf_str, conf_str, sizeof conf_str);
    xconf_str[sizeof conf_str - 1] = 0;
    return EINVAL;
  }

  *xconf_val = _ndproxy_conf_link_mac_address;
  *xconf_isset = true;
  return 0;
}

// get or update the value of the sysctl node named net.inet6.ndproxyconf_downlink_mac_address
static int cb_string_downlink_mac_addr(SYSCTL_HANDLER_ARGS) {
  return cb_string_mac_addr(oidp, arg1, arg2, req, ndproxy_conf_str_downlink_mac_address, &ndproxy_conf_downlink_mac_address, &ndproxy_conf_downlink_mac_address_isset);
}

#if 0
// reserved for a future use
// get or update the value of the sysctl node named net.inet6.ndproxyconf_uplink_mac_address
static int cb_string_uplink_mac_addr(SYSCTL_HANDLER_ARGS) {
  return cb_string_mac_addr(oidp, arg1, arg2, req, ndproxy_conf_str_uplink_mac_address, &ndproxy_conf_uplink_mac_address, &ndproxy_conf_uplink_mac_address_isset);
}
#endif

// declare the sysctl node named net.inet6.ndproxyconf_{up,down}link_mac_address
// format: NN:NN:NN:NN:NN:NN
SYSCTL_OID(_net_inet6, OID_AUTO, ndproxyconf_downlink_mac_address, CTLTYPE_STRING | CTLFLAG_MPSAFE | CTLFLAG_RW, ndproxy_conf_str_downlink_mac_address, sizeof ndproxy_conf_str_downlink_mac_address, cb_string_downlink_mac_addr, "S", "downlink mac adress");
// the uplink mac address is reserved for a future use when it could be used to filter uplink packets instead of using the uplink ipv6 addresses
// SYSCTL_OID(_net_inet6, OID_AUTO, ndproxyconf_uplink_mac_address, CTLTYPE_STRING | CTLFLAG_MPSAFE | CTLFLAG_RW, ndproxy_conf_str_uplink_mac_address, sizeof ndproxy_conf_str_uplink_mac_address, cb_string_uplink_mac_addr, "S", "uplink mac adress");

////////////////////////////////////////////////////////////////////////////////
// net.inet6.ndproxyconf_exception_ipv6_addresses && net.inet6.ndproxyconf_uplink_ipv6_addresses

// storage string for the sysctl node named net.inet6.ndproxyconf_exception_ipv6_addresses
static char ndproxy_conf_str_exception_ipv6_addresses[CONF_NEXCEPTIONS_SIZE] = "";

// storage string for the sysctl node named net.inet6.ndproxyconf_uplink_ipv6_addresses
static char ndproxy_conf_str_uplink_ipv6_addresses[CONF_NUPLINK_SIZE] = "";

// get or update the value of the sysctl node named net.inet6.ndproxyconf_{uplink,exception}_ipv6_addresses
static int cb_string_list(SYSCTL_HANDLER_ARGS, int nentries_size, int nentries_max, char *ndproxy_conf_str_ipv6_addresses, struct in6_addr *ndproxy_conf_ipv6_addresses, int *ndproxy_conf_ipv6_naddresses) {
  char conf_str[nentries_size];
  struct in6_addr _ndproxy_conf_ipv6_addresses[nentries_max];
  int _ndproxy_conf_ipv6_naddresses = 0;
  int ret;

  register_hook();
  
  GENERIC_CB_STRING;

  if (!strlen(ndproxy_conf_str_ipv6_addresses)) {
    *ndproxy_conf_ipv6_naddresses = 0;
    return 0;
  }

  char *curp = ndproxy_conf_str_ipv6_addresses;
  char *delim;
  do {
    char tmpstr[nentries_size];
    delim = strchr(curp, ';');
    if (delim != NULL) {
      strncpy(tmpstr, curp, delim - curp);
      tmpstr[delim - curp] = 0;
    } else strcpy(tmpstr, curp);
    ret = parse_ipv6(tmpstr, _ndproxy_conf_ipv6_addresses + _ndproxy_conf_ipv6_naddresses);
    if (!ret) {
#ifdef DEBUG_NDPROXY
      printf("NDPROXY INFO: parsed address: [");
      printf_ip6addr_network_format(_ndproxy_conf_ipv6_addresses + _ndproxy_conf_ipv6_naddresses);
      printf("]\n");
#endif
    } else {
      strncpy(ndproxy_conf_str_ipv6_addresses, conf_str, nentries_size);
      ndproxy_conf_str_ipv6_addresses[nentries_size - 1] = 0;
      return EINVAL;
    }
    _ndproxy_conf_ipv6_naddresses++;
  } while (delim != NULL && (curp = ++delim) < (char *) (ndproxy_conf_str_ipv6_addresses + nentries_size) && _ndproxy_conf_ipv6_naddresses < nentries_max);

  if (delim) {
      strncpy(ndproxy_conf_str_ipv6_addresses, conf_str, nentries_size);
      ndproxy_conf_str_ipv6_addresses[nentries_size - 1] = 0;
      return EINVAL;
  }

  bcopy(_ndproxy_conf_ipv6_addresses, ndproxy_conf_ipv6_addresses, _ndproxy_conf_ipv6_naddresses * sizeof(struct in6_addr));
  *ndproxy_conf_ipv6_naddresses = _ndproxy_conf_ipv6_naddresses;
  
  return 0;
}

static int cb_string_list_exception(SYSCTL_HANDLER_ARGS) {
  return cb_string_list(oidp, arg1, arg2, req, CONF_NEXCEPTIONS_SIZE, CONF_NEXCEPTIONS_MAX, ndproxy_conf_str_exception_ipv6_addresses, ndproxy_conf_exception_ipv6_addresses, &ndproxy_conf_exception_ipv6_naddresses);
}

static int cb_string_list_uplink(SYSCTL_HANDLER_ARGS) {
  return cb_string_list(oidp, arg1, arg2, req, CONF_NUPLINK_SIZE, CONF_NUPLINK_MAX, ndproxy_conf_str_uplink_ipv6_addresses, ndproxy_conf_uplink_ipv6_addresses, &ndproxy_conf_uplink_ipv6_naddresses);
}

// declare the sysctl node named net.inet6.ndproxyconf_exception_ipv6_addresses
// format: NNNN:NNNN:NNNN:NNNN:NNNN:NNNN:{NNNN:NNNN,XXX.XXX.XXX.XXX};...;...
SYSCTL_OID(_net_inet6, OID_AUTO, ndproxyconf_exception_ipv6_addresses, CTLTYPE_STRING | CTLFLAG_MPSAFE | CTLFLAG_RW, ndproxy_conf_str_exception_ipv6_addresses, sizeof ndproxy_conf_str_exception_ipv6_addresses, cb_string_list_exception, "S", "do not proxy this list of IPv6 adresses");

// declare the sysctl node named net.inet6.ndproxyconf_uplink_ipv6_addresses
// format: NNNN:NNNN:NNNN:NNNN:NNNN:NNNN:{NNNN:NNNN,XXX.XXX.XXX.XXX};...;...
SYSCTL_OID(_net_inet6, OID_AUTO, ndproxyconf_uplink_ipv6_addresses, CTLTYPE_STRING | CTLFLAG_MPSAFE | CTLFLAG_RW, ndproxy_conf_str_uplink_ipv6_addresses, sizeof ndproxy_conf_str_uplink_ipv6_addresses, cb_string_list_uplink, "S", "uplink router IPv6 adresses");

////////////////////////////////////////////////////////////////////////////////
// net.inet6.ndproxycount

static int cb_count(SYSCTL_HANDLER_ARGS) {
  register_hook();
  
#ifdef DEBUG_NDPROXY
    printf("NDPROXY INFO: count\n");
#endif

    return sysctl_handle_int(oidp, arg1, arg2, req);
}

SYSCTL_OID(_net_inet6, OID_AUTO, ndproxycount, CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RW, &ndproxy_conf_count, 0, cb_count, "I", "fire an event");

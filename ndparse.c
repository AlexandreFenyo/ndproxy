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
#include <sys/socket.h>
#include <sys/ctype.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet6/scope6_var.h>

#if (__FreeBSD_version < 1100000)
#include <netinet6/in6_var.h>
#endif

#include "ndparse.h"
#include "ndconf.h"

static const char hexdigits[] = "0123456789abcdef";
static int digit2int(const char digit) {
  return strchr(hexdigits, digit) - hexdigits;
}

// This Ethernet address parser only handles the hexadecimal representation made of 6 groups of 2 hexadecimal
// numbers separated by colons: "XX:XX:XX:XX:XX:XX".
// Other representations will return -1.
int parse_mac(char *str, struct ether_addr *retaddr) {
  if (strlen(str) != MACMAXSIZE) return -1;
  for (int i = 0; i < 6; i++) {
    if ((i < 5 && str[3 * i + 2] != ':') || !isxdigit(str[3 * i]) || !isxdigit(str[3 * i + 1])) return -1;
    retaddr->octet[i] = (digit2int(tolower(str[3 * i])) << 4) + digit2int(tolower(str[3 * i + 1]));
  }
  return 0;
}

// This IPv6 address parser handles any valid textual representation according to RFC-4291 and RFC-5952.
// Other representations will return -1.
//
// note that str input parameter has been modified when the function call returns
//
// parse_ipv6(char *str, struct in6_addr *retaddr)
// parse textual representation of IPv6 addresses
// str:     input arg
// retaddr: output arg
int parse_ipv6(char *str, struct in6_addr *retaddr) {
  bool compressed_field_found = false;
  unsigned char *_retaddr = (unsigned char *) retaddr;
  char *_str = str;
  char *delim;

  bzero((void *) retaddr, sizeof(struct in6_addr));
  if (!strlen(str) || strchr(str, ':') == NULL || (str[0] == ':' && str[1] != ':') ||
      (strlen(str) >= 2 && str[strlen(str) - 1] == ':' && str[strlen(str) - 2] != ':')) return -1;

  // convert transitional to standard textual representation
  if (strchr(str, '.')) {
    int ipv4bytes[4];
    char *curp = strrchr(str, ':');
    if (curp == NULL) return -1;
    char *_curp = ++curp;
    for (int i = 0; i < 4; i++) {
      char *nextsep = strchr(_curp, '.');
      if (_curp[0] == '0' || (i < 3 && nextsep == NULL) || (i == 3 && nextsep != NULL)) return -1;
      if (nextsep != NULL) *nextsep = 0;
      for (int j = 0; j < strlen(_curp); j++) if (_curp[j] < '0' || _curp[j] > '9') return -1;
      if (strlen(_curp) > 3) return -1;
      const long val = strtol(_curp, NULL, 10);
      if (val < 0 || val > 255) return -1;
      ipv4bytes[i] = val;
      _curp = nextsep + 1;
    }
    sprintf(curp, "%x%02x:%x%02x", ipv4bytes[0], ipv4bytes[1], ipv4bytes[2], ipv4bytes[3]);
  }	

  // parse standard textual representation
  do {
    if ((delim = strchr(_str, ':')) == _str || (delim == NULL && !strlen(_str))) {
      if (delim == str) _str++;
      else if (delim == NULL) return 0;
      else {
	if (compressed_field_found == true) return -1;
	if (delim == str + strlen(str) - 1 && _retaddr != (unsigned char *) (retaddr + 1)) return 0;
	compressed_field_found = true;
	_str++;
	int cnt = 0;
	for (char *__str = _str; *__str; ) if (*(__str++) == ':') cnt++;
	unsigned char *__retaddr = - 2 * ++cnt + (unsigned char *) (retaddr + 1);
	if (__retaddr <= _retaddr) return -1;
	_retaddr = __retaddr;
      }
    } else {
      char hexnum[4] = "0000";
      if (delim == NULL) delim = str + strlen(str);
      if (delim - _str > 4) return -1;
      for (int i = 0; i < delim - _str; i++)
	if (!isxdigit(_str[i])) return -1;
	else hexnum[4 - (delim - _str) + i] = tolower(_str[i]);
      _str = delim + 1;
      *(_retaddr++) = (digit2int(hexnum[0]) << 4) + digit2int(hexnum[1]);
      *(_retaddr++) = (digit2int(hexnum[2]) << 4) + digit2int(hexnum[3]);
    }
  } while (_str < str + strlen(str));
  return 0;
}

void printf_ip6addr(const struct in6_addr *addrp, const bool clear_scope) {
  struct in6_addr tmpaddr = *addrp;
  char addrstr[INET6_ADDRSTRLEN + 1];

  if (clear_scope) in6_clearscope(&tmpaddr);
  ip6_sprintf(addrstr, &tmpaddr);
  addrstr[INET6_ADDRSTRLEN] = 0;
  printf("%s", addrstr);
}

void printf_ip6addr_network_format(const struct in6_addr *addrp) {
  struct in6_addr tmpaddr = *addrp;

  in6_clearscope(&tmpaddr);

  //  for (int i = 0; i < 4; i++) printf("addr[%d] = %x\n", i, tmpaddr.__u6_addr.__u6_addr32[i]);
  for (int i = 0; i < 16; i++) {
    printf("%02X", ((const unsigned char *) addrp)[i]);
    if (i%2 && i != 15) printf(":");
  }
}

void printf_macaddr_network_format(const struct ether_addr *addrp) {
  char addrstr[MACMAXSIZE + 1];
  sprintf(addrstr, "%02X:%02X:%02X:%02X:%02X:%02X", addrp->octet[0], addrp->octet[1], addrp->octet[2], addrp->octet[3], addrp->octet[4], addrp->octet[5]);
  printf("%s", addrstr);
}

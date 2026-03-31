#pragma once

#include <netinet/in.h>

#include <string>

namespace chat {

/// Finner lokal IPv4 brukt mot «default route» (UDP connect-trick). For broadcast/unicast-annonsering.
bool get_local_ipv4(in_addr& out);

std::string format_ipv4(const in_addr& addr);

}  // namespace chat

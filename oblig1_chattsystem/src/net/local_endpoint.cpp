#include "chat/net/local_endpoint.hpp"

#include "chat/net/errno_util.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace chat {

bool get_local_ipv4(in_addr& out) {
    const int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        std::cerr << errno_message("socket(get_local_ipv4)") << "\n";
        return false;
    }
    sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(53);
    if (::inet_pton(AF_INET, "8.8.8.8", &serv.sin_addr) != 1) {
        std::cerr << "inet_pton(get_local_ipv4) feilet.\n";
        ::close(s);
        return false;
    }
    if (::connect(s, reinterpret_cast<sockaddr*>(&serv), sizeof(serv)) < 0) {
        std::cerr << errno_message("connect(get_local_ipv4)") << "\n";
        ::close(s);
        return false;
    }
    sockaddr_in name{};
    socklen_t len = sizeof(name);
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&name), &len) < 0) {
        std::cerr << errno_message("getsockname(get_local_ipv4)") << "\n";
        ::close(s);
        return false;
    }
    out = name.sin_addr;
    ::close(s);
    return true;
}

std::string format_ipv4(const in_addr& addr) {
    char buf[INET_ADDRSTRLEN];
    if (::inet_ntop(AF_INET, &addr, buf, sizeof(buf)) == nullptr) {
        return {};
    }
    return std::string{buf};
}

}  // namespace chat

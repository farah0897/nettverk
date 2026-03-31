#pragma once

#include <cstddef>
#include <cstdint>

#include <span>

#include <netinet/in.h>

namespace chat {

/// RAII for UDP-socket: bind, sendto/recvfrom, `SO_BROADCAST`, `set_reuseaddr`, ikke-blokkerende valgfritt.
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    bool open();
    void close();

    bool bind(std::uint16_t port);
    bool set_broadcast(bool enable);
    bool set_reuse_address(bool enable);

    int fd() const;

    bool send_to(std::span<const std::byte> data, const sockaddr_in& dest);
    bool recv_from(std::span<std::byte> buffer, sockaddr_in& from, std::size_t& out_len);

    /// Må kalles etter `bind()` på mottakerporten for multicast.
    bool multicast_add_membership(const in_addr& group, const in_addr& interface_addr);
    bool multicast_drop_membership(const in_addr& group, const in_addr& interface_addr);
    bool set_multicast_ttl(std::uint8_t ttl);
    bool set_multicast_loop(bool enable);
    /// Utsendingsgrensesnitt for multicast (vanligvis lokal IPv4).
    bool set_multicast_outbound_interface(const in_addr& interface_addr);

    /// Feildiagnostikk for siste feil i `UdpSocket`-operasjoner.
    int last_errno() const { return last_errno_; }

private:
    int fd_{-1};
    int last_errno_{0};
};

}  // namespace chat

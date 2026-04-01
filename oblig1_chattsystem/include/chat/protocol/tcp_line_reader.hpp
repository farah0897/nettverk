#pragma once

#include "chat/protocol/limits.hpp"
#include "chat/net/tcp_connection.hpp"

#include <cstddef>
#include <string>

namespace chat {

enum class TcpLineReadStatus {
    LineReady,      ///< `out` inneholder linjeinnhold (uten `\n`)
    NeedMoreData,   ///< Ikke-blokkerende socket: vent på flere bytes
    Disconnected,   ///< Peer lukket forbindelsen
    Error,          ///< Ugyldig linje eller I/O-feil (annet enn EAGAIN)
};

/// Leser linjedelte meldinger fra TCP (avslutter med `\n`). `\r` foran `\n` strippes.
/// Maks én logisk linje per kall: lengde uten avsluttende `\n` er begrenset til `kMaxPacketBytes`
/// (samme grense som SECP), for å unngå buffer overflow og uendelige linjer.
class TcpLineReader {
public:
    explicit TcpLineReader(TcpConnection& connection) : conn_{&connection} {}

    /// Blokkerer til full linje, eller til feil / lukket forbindelse / linje for lang.
    /// Ved lukket forbindelse før `\n`: `false`, `connection().last_errno()` kan være 0 og `recv` ga 0.
    [[nodiscard]] bool read_line(std::string& out);

    /// For ikke-blokkerende sockets og `poll`: returnerer `LineReady`, `NeedMoreData`, osv.
    [[nodiscard]] TcpLineReadStatus try_read_line(std::string& out);

    TcpConnection& connection() { return *conn_; }
    const TcpConnection& connection() const { return *conn_; }

    /// Tømmer intern buffer (f.eks. ved bytte modus).
    void clear_pending() { pending_.clear(); }

private:
    TcpConnection* conn_{nullptr};
    std::string pending_{};

    static constexpr std::size_t kMaxPending =
        kMaxPacketBytes + 2;  // plass til `\r\n` uten å overskride logisk maksgrense
};

}  // namespace chat

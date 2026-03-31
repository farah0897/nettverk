# CODE_WALKTHROUGH.md — teknisk studieguide (chattsystem)

Dette dokumentet er skrevet som en **studieguide** for deg som student: målet er at du skal kunne lese koden i dette prosjektet og forstå den “innenfra” — som om du hadde skrevet den selv. Jeg forklarer både **hva** som skjer, **hvorfor** det er designet slik, og hvordan det mapper direkte til oppgaveteksten (broadcast/multicast/unicast, robusthet, inputvalidering og buffer overflow prevention).

> Protokollnote: Kodebasen er nå refaktorert til å følge **RFC USNChat01 / SECP**. Det betyr at meldinger er **UTF-8 tekst** på formen `TYPE|ROOM|USERNAME|PAYLOAD\n`, og at **all UDP-trafikk går på port 50000**.

> Viktig avgrensning: Jeg beskriver kun det som **faktisk finnes i kodebasen**. Der noe er uferdig/ubrukt (f.eks. `ChatApplication`, `CliController`, `PollMultiplexer`, `SocketAddress` uten `.cpp`), peker jeg det ut eksplisitt.

---

## Innholdsfortegnelse

- [1. Prosjektoversikt](#1-prosjektoversikt)
- [2. Overordnet arkitektur](#2-overordnet-arkitektur)
- [3. Fil-for-fil forklaring](#3-fil-for-fil-forklaring)
- [4. Klasse-for-klasse / struct-for-struct forklaring](#4-klasse-for-klasse--struct-for-struct-forklaring)
- [5. Funksjonsforklaring](#5-funksjonsforklaring)
- [6. Nettverkslogikk i detalj](#6-nettverkslogikk-i-detalj)
- [7. Meldingsprotokoll](#7-meldingsprotokoll)
- [8. Tråder og samtidighet](#8-tråder-og-samtidighet)
- [9. Krav-for-krav mapping mot oppgaven](#9-krav-for-krav-mapping-mot-oppgaven)
- [10. Feilhåndtering og sikkerhet](#10-feilh%C3%A5ndtering-og-sikkerhet)
- [11. Programflyt steg for steg](#11-programflyt-steg-for-steg)
- [12. Hvordan jeg kan lese koden smartest](#12-hvordan-jeg-kan-lese-koden-smartest)
- [13. Muntlig forberedelse](#13-muntlig-forberedelse)
- [14. Forbedringsforslag](#14-forbedringsforslag)

---

## 1. Prosjektoversikt

### Hva programmet gjør

Dette er en CLI-basert chatteklient i **C++23** som bruker **POSIX UDP sockets**. Programmet kjører flere parallelle “tjenester” samtidig:

- **Discovery / presence**: `PRESENCE|-|username|ip` via **UDP broadcast**.
- **USN Chat (globalt rom)**: `CHAT|USN Chat|username|message` via **UDP broadcast**.
- **Åpne grupperom**:
  - annonseres via `ROOM_ANNOUNCE|room-name|owner|OPEN` via **UDP broadcast**
  - chat i rommet via **UDP multicast** til `239.0.0.1:50000` (RFC-forenkling), filtrert på `ROOM`
- **Lukkede rom (1-1 / små grupper)**:
  - inviteres via `INVITE|room-name|owner|CLOSED;to=invitedUser` via **UDP unicast**
  - chat i rommet via `CHAT|room-name|username|message` via **UDP unicast** mellom deltakerne

På toppen har du en tekstmeny (`run_text_menu_loop`) som lar brukeren trigge sending, opprette rom, joine rom, invitere andre, osv. Mottak av nettverkspakker skjer kontinuerlig i en egen mottakstråd.

### Hvordan det henger sammen med oppgaven

Oppgaven krever eksplisitt støtte for:

- publisering av egen informasjon
- åpent fellesrom via UDP broadcast
- åpne grupperom via UDP multicast (med annonsering)
- 1-1 via UDP unicast
- robust feilhåndtering, inputvalidering, buffer overflow prevention

Denne kodebasen har valgt å implementere dette som **flere små services** (discovery/USN Chat/group/direct) som deler:

- et felles socket-lag (`UdpSocket`)
- en felles mottaks-løkke (`run_receive_multiplex_loop`) som parser SECP-linjer og ruter på TYPE/ROOM

Det er et bevisst designvalg: det blir lettere å forklare og feilsøke hver del for seg, og det blir veldig tydelig *hvilken del av oppgaven* som løses hvor.

### Hvilke deler som bruker broadcast, multicast og unicast

RFC USNChat01/SECP bruker faste porter i `include/chat/protocol/ports.hpp`:

- **All UDP**: `kUdpPort = 50000`
- **TCP (garanterte rom)**: `kTcpPort = 50001` (ikke implementert i denne kodebasen)

Transportvalg per romtype:

- **Broadcast** (presence, globalt rom, romannonser): UDP broadcast → `:50000`
- **Multicast** (åpne rom): UDP multicast → `239.0.0.1:50000`, TTL=1
- **Unicast** (lukkede rom): UDP unicast mellom deltakere → `:50000`

Hvor dette faktisk brukes:

- **Broadcast send/recv (SECP)**
  - `DiscoveryService::send_presence_broadcast()` og routing i `run_receive_multiplex_loop(...)`
  - `LobbyService::send_chat()` og `LobbyService::on_lobby_chat(...)`
  - `GroupRoomCoordinator::broadcast_owned_advert()` og `GroupRoomCoordinator::on_room_announce(...)`
- **Multicast join/leave + chat (SECP)**
  - `GroupRoomCoordinator::bind_multicast_channel()` bruker `UdpSocket::multicast_add_membership()` (`IP_ADD_MEMBERSHIP`)
  - `GroupRoomCoordinator::leave_multicast_membership_unlocked()` bruker `UdpSocket::multicast_drop_membership()` (`IP_DROP_MEMBERSHIP`)
  - `GroupRoomCoordinator::send_group_chat()` sender SECP `CHAT|room|...` til multicast-gruppen
  - Mottak av multicast i denne kodebasen skjer via samme UDP-socket og filtrering på `ROOM` (RFC-forenkling).
- **Unicast (SECP)**
  - `DirectMessageService::invite_user()` / `accept_invite()` / `decline_invite()` / `send_private_chat()`
  - Routing av `INVITE`/`CHAT` skjer i `run_receive_multiplex_loop(...)` via `DirectMessageService::on_invite(...)` / `on_room_chat(...)`

### Hvilke hovedmoduler prosjektet består av

En praktisk modul-inndeling (slik du bør tenke om kodebasen):

- **App/orkestrering**: `src/main.cpp` + `src/app/receive_multiplex_loop.cpp` + `src/app/text_menu.cpp`
- **Protokoll (SECP)**: `include/chat/protocol/secp.hpp` + `src/protocol/secp.cpp`
- **Nettverk/IO**: `include/chat/net/udp_socket.hpp` + `src/net/udp_socket.cpp` + `include/chat/net/*`
- **State**: `include/chat/state/user_directory.hpp` + `src/state/user_directory.cpp`
- **Services**: `src/services/*` (discovery, USN Chat, group, direct)
- **Logging og tråd-sikker output**: `include/chat/log/logger.hpp`, `src/log/logger.cpp`, `include/chat/app/sync_output.hpp`

---

## 2. Overordnet arkitektur

### Arkitekturen i én setning

Programmet er en **event-drevet UDP-klient** med en **mottakstråd** (poll/recv), flere **periodiske tråder** (heartbeat og gruppeannonser), og en **hovedtråd** som kjører CLI-menyen — med felles protokoll-lag og defensiv parsing.

### Hvilke moduler som har ansvar for hva

- `**main.cpp**`: oppstart, socket-konfig, lager services, kobler callbacks til utskrift, starter tråder, og gjør clean shutdown.
- `**run_receive_multiplex_loop**`: *kun* mottak og routing av rå UDP-datagrammer til riktig service.
- **Services**:
  - `DiscoveryService`: presence encode/decode og oppdaterer `UserDirectory`
  - `LobbyService`: encode/decode av USN Chat-meldinger + callback
  - `GroupRoomCoordinator`: alt med grupperom (annonser, registry, multicast socket, join/leave, group chat)
  - `DirectMessageService`: alt med privat 1-1 (sessions, invite/accept/reject/chat, resend og pruning)
- `**UdpSocket**`: eneste stedet som gjør `socket/bind/sendto/recvfrom/setsockopt`.
- `**SECP parser/builder**`: `parse_secp_line(...)` / `build_secp_line(...)` i `src/protocol/secp.cpp`.

Dette er et typisk godt “skoleprosjekt-design”: du kan forklare komponenter separat, men de er fortsatt koblet sammen på en enkel måte.

### Hvordan data flyter gjennom systemet (fra nett → app)

Den mest sentrale ideen er: **Nettverkstråden gjør minimalt**.

1. `poll()` venter på data på UDP-socket-fd-en.
2. Når fd er klar, gjør den `recv_from()` inn i en fast buffer (`kMaxPacketBytes`).
3. Bufferen parses som en SECP-linje: `parse_secp_line(...)`.
4. Meldingen routes på `TYPE`/`ROOM` til riktig service, som oppdaterer state og/eller trigger callback.

Dette er viktig for robusthet: ingen kompleks logikk i recv-loop betyr færre buger og mindre risiko for at en service-feil “dreper” hele mottaksflyten.

### Hvordan programmet starter opp

Oppstartssekvensen i `src/main.cpp` er i praksis “arkitektur-dokumentet”:

- validerer brukernavn (`trim_in_place`, sjekk tom)
- finner lokal IPv4 (`get_local_ipv4`)
- oppretter og konfigurerer én UDP socket:
  - `udp_sock` på `50000` (broadcast + reuseaddr + bind)
- oppretter state:
  - `UserDirectory users` (setter `self_username`)
- oppretter tjenester:
  - `DiscoveryService discovery{users, udp_sock}` (setter identity)
  - `LobbyService lobby{udp_sock}` (setter on_message callback for USN Chat)
  - `GroupRoomCoordinator groups(udp_sock, local_ip, username)` (setter group msg callback)
  - `DirectMessageService direct{udp_sock, users, username}` (setter status + private msg callback)
- sender én initial presence broadcast
- starter tråder:
  - `receiver`: kjører `run_receive_multiplex_loop(...)`
  - `heartbeat`: periodisk presence, prune, resend, prune pending
  - `group_advert_loop`: periodisk `groups.broadcast_owned_advert()`
- kjører CLI:
  - `run_text_menu_loop(...)`
- shutdown:
  - `running=false`, request_stop, close sockets, `groups.leave_room()`, join tråder

### Hvordan tråder og nettverkslogikk er organisert

Trådmodell (det du bør kunne forklare muntlig):

- **Main thread**: blokkerer på stdin (meny). Den gjør aldri `recvfrom`.
- **Receiver thread**: blokkerer i `poll()` på socket-fd-er. Når data kommer, gjør den én `recv_from` og kaller `service.on_datagram(...)`.
- **Heartbeat thread**: tidstyrt ved `InterruptibleSleep`. Den gjør “vedlikehold”:
  - `discovery.send_presence_broadcast()`
  - `discovery.prune_stale_peers()`
  - `direct.prune_pending(std::chrono::seconds{30})`
  - `direct.resend_outgoing_pending(std::chrono::seconds{6}, std::chrono::seconds{45})`
- **Group advert thread**: tidstyrt. Den annonserer rom hvis du eier et.

Hvorfor det er valgt slik:

- Du unngår at CLI-lesing og nettverksmottak “blokkerer” hverandre.
- Du unngår å skrive en kompleks state-machine i én tråd.
- Du får enkel, stabil oppførsel: mottak skjer uansett hva brukeren gjør i menyen.

---

## 3. Fil-for-fil forklaring

Jeg går først gjennom “toppen” (main + app), så protokoll/nett/state, så services.

### `CMakeLists.txt`

- **Ansvar**: definerer bygg for `chatapp` og hvilke filer som inngår.
- **Viktig**: viser hvilke `.cpp` som faktisk er en del av programmet. Alt i `build/` er generert og ikke kildekode.
- **Designvalg**: C++23, strenge advarsler `-Wall -Wextra -Wpedantic`.

### `src/main.cpp`

- **Ansvar**: orkestrerer alt. Dette er “hoveddokumentasjonen” for runtime-arkitekturen.
- **Viktige funksjoner**: `main(...)`.
- **Samarbeid**:
  - setter opp `UdpSocket`-instanser
  - binder dem til portene fra `include/chat/protocol/ports.hpp`
  - lager services og setter callbacks som printer til konsoll
  - starter tråder og stopper dem rent
- **Hvorfor viktig**: her ser du direkte hvilke porter/typer som brukes hvor, og trådmodellen.

### `include/chat/protocol/ports.hpp`

- **Ansvar**: samler alle “fast porter” i én header.
- **Hvorfor viktig**: uten dette blir portene “magiske tall” rundt om i koden. Nå kan du peke på én sannhet.

### `include/chat/protocol/limits.hpp`

- **Ansvar**: maksgrenser for felt/pakker.
- **Hvorfor viktig**: dette er kjernen i buffer overflow prevention og defensiv parsing.

### `include/chat/protocol/secp.hpp` og `src/protocol/secp.cpp`

- **Ansvar**: RFC USNChat01 / SECP (tekstprotokoll).
  - Parser meldingslinjer `TYPE|ROOM|USERNAME|PAYLOAD\n`
  - Bygger meldingslinjer på samme format
  - Validerer lengder og forbyr `|`/newline inne i felter
  - Ukjente typer / malformed meldinger droppes

### `include/chat/net/udp_socket.hpp` og `src/net/udp_socket.cpp`

- **Ansvar**: RAII wrapper for UDP + multicast/broadcast `setsockopt`.
- **Hvorfor viktig**: isolerer syscalls + errno-håndtering.

### `include/chat/net/local_endpoint.hpp` og `src/net/local_endpoint.cpp`

- **Ansvar**: finne lokal IPv4 og formatere IPv4.
- **Hvorfor viktig**: brukes for identity og multicast interface.

### `include/chat/net/errno_util.hpp`

- **Ansvar**: bygge meningsfulle errno-meldinger.

### (Fjernet) `include/chat/net/packet_util.hpp`

- **Status**: fjernet i SECP-refaktoreringen (binær frame encode/decode brukes ikke lenger).

### `include/chat/state/user_directory.hpp` og `src/state/user_directory.cpp`

- **Ansvar**: trådsikker katalog over oppdagede brukere.

### `include/chat/services/discovery_service.hpp` og `src/services/discovery_service.cpp`

- **Ansvar**: presence via broadcast og oppdatering av `UserDirectory`.

### `include/chat/services/lobby_service.hpp` og `src/services/lobby_service.cpp`

- **Ansvar**: fellesrom via broadcast med callback til UI.

### `include/chat/services/group_room_coordinator.hpp` og `src/services/group_room_coordinator.cpp`

- **Ansvar**: grupperom (annonsering + registry + multicast join/leave + group chat).

### `include/chat/services/direct_message_service.hpp` og `src/services/direct_message_service.cpp`

- **Ansvar**: lukkede rom over UDP unicast (INVITE + CHAT) + resend/prune.

### `include/chat/app/receive_multiplex_loop.hpp` og `src/app/receive_multiplex_loop.cpp`

- **Ansvar**: `poll()` + `recvfrom` routing til services.

### `include/chat/app/text_menu.hpp` og `src/app/text_menu.cpp`

- **Ansvar**: CLI-meny og inputvalidering.

### `include/chat/app/sync_output.hpp` og `src/app/sync_output.cpp`

- **Ansvar**: global `chat::cout_mutex`.

### `include/chat/app/interruptible_sleep.hpp`

- **Ansvar**: stoppbar vent for clean shutdown.

### `include/chat/log/logger.hpp` og `src/log/logger.cpp`

- **Ansvar**: trådsikker logging av send/recv og feil.

### `include/chat/util/string_trim.hpp`

- **Ansvar**: trimming av strings.

### Filer som finnes, men ikke er “fullt i bruk”

- `include/chat/app/application.hpp` (`ChatApplication`) — **ingen `.cpp**`, ikke brukt.
- `include/chat/app/cli_controller.hpp` (`CliController`) — **ingen `.cpp**`, ikke brukt.
- `include/chat/net/poll_multiplexer.hpp` (`PollMultiplexer`) — **ingen `.cpp**`, ikke brukt.
- `include/chat/net/socket_address.hpp` (`SocketAddress`) — **ingen `.cpp**`, ikke brukt.

---

## 4. Klasse-for-klasse / struct-for-struct forklaring

Denne delen er ment som “huskekort”: Hvis du kan disse typene, kan du forklare hele løsningen.

### SECP melding (`SecpMessage`) og parsing (`parse_secp_line`)

- **Formål**: SECP beskriver meldinger som *tekstlinjer* med 4 felt:
  - `TYPE|ROOM|USERNAME|PAYLOAD\n`
- **Hvorfor dere gjør det**:
  - RFC-en krever dette formatet, og det er lett å debugge (lesbart i logg).
  - `parse_secp_line(...)` gjør defensiv validering (riktig antall felt, lengdegrenser, og forbyr `|`/newline inne i felt).
- **Viktige typer**:
  - `PRESENCE|-|username|ip`
  - `ROOM_ANNOUNCE|room-name|owner|OPEN`
  - `INVITE|room-name|owner|CLOSED;to=invitedUser`
  - `CHAT|room-name|username|message`

### `UdpSocket` (`include/chat/net/udp_socket.hpp`, `src/net/udp_socket.cpp`)

- **Formål**: RAII + “ett sted” for `sendto/recvfrom/bind/setsockopt`.
- **Viktigste medlemsvariabler**:
  - `fd`_: socket-handle
  - `last_errno_`: sist feil (brukes til logging)
- **Viktigste metoder**:
  - `open()`, `close()`, `bind(port)`
  - `set_broadcast(true)` (`SO_BROADCAST`)
  - `set_reuse_address(true)` (`SO_REUSEADDR`)
  - `send_to(...)` (EINTR-retry, størrelse-sjekk)
  - `recv_from(...)` (EINTR-retry, sjekker `AF_INET`)
  - multicast: `IP_ADD_MEMBERSHIP`, `IP_DROP_MEMBERSHIP`, `IP_MULTICAST_IF`, TTL, LOOP
- **Hvorfor valgt**:
  - Gir tydelig feilhåndtering via `last_errno()`.
  - Hindrer kopiering av lavnivå socket-kode mellom tjenester.

### `UserDirectory` (`include/chat/state/user_directory.hpp`, `src/state/user_directory.cpp`)

- **Formål**: trådsikker “telefonbok” username → adresse + lastSeen.
- **Viktigste medlemsvariabler**:
  - `peers_`: `unordered_map<string, PeerInfo>`
  - `mutex_`
  - `self_username_` (slik at du ikke ser deg selv som peer)
- **Viktigste metoder**:
  - `upsert(username, from)`
  - `prune_stale(max_age)`
  - `find_address(username)` (brukes av 1-1)
  - `snapshot_peers()` (kopi for CLI)
- **Hvorfor valgt**:
  - Oppgaven krever både (a) visning av aktive brukere og (b) å velge mottaker for 1-1.

### `DiscoveryService` (`include/chat/services/discovery_service.hpp`, `src/services/discovery_service.cpp`)

- **Formål**: presence via broadcast (`50000`).
- **Viktig robusthetsvalg**: payload-IPv4 må matche avsender-IP (drop ellers).
- **Hvorfor**:
  - gjør discovery mer pålitelig og litt mer motstandsdyktig mot “feilkonfig” og enkel spoofing.

### `LobbyService` (`include/chat/services/lobby_service.hpp`, `src/services/lobby_service.cpp`)

- **Formål**: fellesrom via broadcast (`50000`).
- **Hvorfor**:
  - dette er “klassisk” UDP-broadcast chat; dere løser det med type-filter + parsing.

### `GroupRoomCoordinator` (`include/chat/services/group_room_coordinator.hpp`, `src/services/group_room_coordinator.cpp`)

- **Formål**: åpne grupperom.
- **Internt ansvar**:
  - romannonser (broadcast `50000`) + registry med stale pruning (`25s`)
  - multicast join/leave + mottak/sending av `GroupChat`
  - lagring av “eget rom” (`owned_`) og aktivt rom (`active_room_id_`)
- **Begrensning**: én aktiv gruppe om gangen (bevisst forenkling).

### `DirectMessageService` (`include/chat/services/direct_message_service.hpp`, `src/services/direct_message_service.cpp`)

- **Formål**: lukket rom (INVITE + CHAT) via unicast (`50000`).
- **Kjerneidé**: session state machine:
  - `OutgoingPending` → (accept) → `Active`
  - `IncomingPending` → (accept) → `Active` eller (reject) → slettet
- **Robusthetsvalg**:
  - resend av utgående invites
  - expire/prune av pending sessions
  - endpoint-match på accept/reject/chat

### `InterruptibleSleep` (`include/chat/app/interruptible_sleep.hpp`)

- **Formål**: periodiske tråder kan stoppe umiddelbart ved shutdown.

### `Logger` + `cout_mutex` (`include/chat/log/logger.hpp`, `include/chat/app/sync_output.hpp`)

- **Formål**:
  - `cout_mutex`: holde terminalutskrift lesbar på tvers av tråder
  - `Logger`: tydelig “send/recv broadcast/multicast/unicast” + parse/socket errors

---

## 5. Funksjonsforklaring

Denne delen er skrevet som en “kontrollflyt-guide”: hva skjer, i hvilken rekkefølge, og hvorfor.

### `main(...)` (`src/main.cpp`)

- **Hva funksjonen gjør**: init, start tråder, kjør meny, shutdown.
- **Input**: `argv[1]` som brukernavn.
- **Output**: kjørende chat-klient.
- **Stegvis logikk**:
  1. trimmer og validerer brukernavn
  2. finner lokal IPv4 (`get_local_ipv4`)
  3. åpner og binder sockets:
    - én UDP-socket på `50000` med `SO_BROADCAST` + `SO_REUSEADDR`
  4. oppretter `UserDirectory` og services
  5. setter callbacks som printer trådsikkert
  6. sender initial presence
  7. starter:
    - receiver-tråd (`run_receive_multiplex_loop`)
    - heartbeat-tråd (presence/prune/resend)
    - group-advert-tråd (annonsering)
  8. kjører `run_text_menu_loop`
  9. clean shutdown (stop flag, close sockets, `leave_room`, join tråder)
- **Hvorfor**: gir tydelig og robust livsløp (start → drift → stopp).

### `run_receive_multiplex_loop(...)` (`src/app/receive_multiplex_loop.cpp`)

- **Hva funksjonen gjør**: multiplex’er mottak på flere sockets med `poll()`.
- **Input**: sockets + service-instansene.
- **Stegvis logikk**:
  - bygger poll-listen hver iterasjon (inkl. multicast-fd hvis aktiv)
  - ved `POLLIN`:
    - `recv_from` inn i buffer (`kMaxPacketBytes`)
    - kaller `on_datagram` i riktig service
  - ved discovery-mottak kalles `prune_stale_peers()`
- **Hvorfor**:
  - én mottakstråd er enklere enn mange, og `poll()` er standard POSIX-løsning.

### SECP parsing (i mottaksloopen)

- **Input**: rå bytes.
- **Output**: `optional<Packet>` + `DecodeError`.
- **Stegvis logikk**:
  - sjekker headerlengde
  - sjekker magic/version
  - type-sjekk (1..8)
  - payload_len-sjekk mot `kMaxPacketBytes`
  - sjekker at datagram inneholder hele payload
- **Hvorfor**:
  - dette er “første forsvarslinje” mot ugyldige pakker.

### `GroupRoomCoordinator::bind_multicast_channel(...)`

- **Hva funksjonen gjør**: setter opp en multicast-socket som både kan sende og motta.
- **Konkret**:
  - `SO_REUSEADDR`
  - `bind(port)`
  - `IP_MULTICAST_IF` til `local_if`_
  - `IP_ADD_MEMBERSHIP` (join)
  - `IP_MULTICAST_TTL=1`, `IP_MULTICAST_LOOP=1`
- **Hvorfor**:
  - multicast krever eksplisitt membership og interface-konfig.

### `DirectMessageService::resend_outgoing_pending(...)`

- **Hva funksjonen gjør**: retransmit av utgående invites for å tåle UDP-tap.
- **Hvorfor**:
  - uten dette kan en invitasjon forsvinne uten feedback.

---

## 6. Nettverkslogikk i detalj

### UDP i dette prosjektet

- UDP er best-effort: dere aksepterer at chatmeldinger kan tapes, men gjør kontrolltrafikk mer robust (presence og invites).
- Dere bruker `kMaxPacketBytes` og defensiv decode/parse for å unngå krasj.

### Broadcast

- Presence, global chat og room announce sendes til `INADDR_BROADCAST` på `50000`.
- Broadcast sockets må ha `SO_BROADCAST` og er satt opp i `main.cpp`.

### Multicast

- Gruppechat sendes til `239.255.42.X:42xxx`.
- Join/leave gjøres med `IP_ADD_MEMBERSHIP`/`IP_DROP_MEMBERSHIP` i `UdpSocket`.
- `GroupRoomCoordinator` lager en egen `mcast_sock_` som binder til rommets port.

### Unicast

- INVITE/CHAT (lukkede rom) sendes til peer IP på `50000`.
- `DirectMessageService` validerer endpoint-match ved mottak (robusthet og enkel sikkerhet).

---

## 7. Meldingsprotokoll

### Header (ramme)

Magic “CH”, version 1, type, payload length (little-endian).

### Payload-format

- Short string: 1-byte lengde + bytes (brukes for små felt).
- Long string: 2-byte lengde + bytes (brukes for chattekst og reason).

### Robusthet (hva dere faktisk gjør)

- maksgrenser fra `include/chat/protocol/limits.hpp`
- UTF-8 validering (`valid_utf8`)
- parse-funksjoner krever `rem == 0`
- services dropper ugyldige pakker uten crash

---

## 8. Tråder og samtidighet

### Threads

- main: CLI (stdin)
- receiver: `poll()` + `recv_from` + `on_datagram`
- heartbeat: presence + prune + direct resend/prune
- group advert loop: periodisk `broadcast_owned_advert`

### Delte data og locking

- `UserDirectory`, `GroupRoomCoordinator`, `DirectMessageService` har hver sin mutex.
- `cout_mutex` beskytter terminalutskrift; koden unngår å holde den mens den blokkerer på `getline`.

---

## 9. Krav-for-krav mapping mot oppgaven

Se også `kvalitetssjekk.md` for en mer “checklist”-aktig gjennomgang.

- **Publisere egen info**: `DiscoveryService` + `UserDirectory` + heartbeat i `main.cpp`
- **Åpent globalt rom (broadcast)**: `LobbyService` + menyvalg 2 (“USN Chat”)
- **Åpne grupperom**: `GroupRoomCoordinator` (advert broadcast + multicast chat)
- **1-1 unicast**: `DirectMessageService` (invite/accept/reject/chat + resend/prune)
- **Robusthet**: `parse_secp_line` + `UdpSocket` + recv-loop
- **Inputvalidering**: `text_menu.cpp` + service-validering
- **Buffer overflow prevention**: `kMaxPacketBytes` + protokoll-lengder + defensive checks

---

## 10. Feilhåndtering og sikkerhet

### Nettverksfeil

- `UdpSocket::last_errno()` brukes til logging og debugging.
- `Logger::socket_error(...)` gir forklarbar output.

### Ugyldige pakker

- droppes ved decode/parse-feil (ingen crash).

### Sikkerhetsvurdering (styrker/svakheter)

- **Styrker**: defensive lengdesjekker, UTF-8 validering, endpoint-match i direct.
- **Svakheter**: ingen autentisering/kryptering; broadcast/multicast kan blokkeres; session_id er ikke kryptografisk.

---

## 11. Programflyt steg for steg

### Startup

- init sockets/services/tråder → send initial presence → meny.

### Presence

- periodic broadcast → mottak → `UserDirectory::upsert` → stale pruning.

### USN Chat (globalt rom)

- menyvalg 2 → broadcast → mottak → callback print.

### Grupperom

- create → join multicast + advert broadcast → join_room → multicast chat send/recv.

### Privat 1-1

- invite → pending → accept/reject → active → unicast chat.

---

## 12. Hvordan jeg kan lese koden smartest

Anbefalt rekkefølge:

1. `src/main.cpp`
2. `include/chat/protocol/secp.hpp` + `src/protocol/secp.cpp`
3. `include/chat/net/udp_socket.hpp` + `src/net/udp_socket.cpp`
4. `src/app/receive_multiplex_loop.cpp`
5. services: discovery → USN Chat → group → direct
6. `src/app/text_menu.cpp`

---

## 13. Muntlig forberedelse

Du bør kunne forklare:

- broadcast vs multicast vs unicast og hvor i koden det skjer
- UDP-tap og hvordan dere håndterer det (presence periodic + pruning, invite resend/prune)
- protokollens header + payloads + hvorfor lengde-prefiks gir robusthet
- trådmodell og mutex-strategi

---

## 14. Forbedringsforslag

- interface-spesifikk broadcast, konfigurerbare porter
- deduplisering/sekvensnummer for chat, ACK for control-meldinger
- autentisering (HMAC), evt kryptering
- flere grupperom samtidig (flere multicast sockets)
- rydde opp i eller ferdigstille ubrukte abstraheringer (`ChatApplication`, `CliController`, `PollMultiplexer`, `SocketAddress`)


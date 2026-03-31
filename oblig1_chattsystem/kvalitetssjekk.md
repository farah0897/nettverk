# Full kvalitetssjekk mot oppgaveteksten (krav-for-krav)

Dette dokumentet oppsummerer en full gjennomgang av prosjektet mot oppgaveteksten, med:

- **Krav**
- **Hvor det er implementert**
- **Om det er fullført**
- **Eventuelle svakheter/mangler**
- **Konkrete forslag til kodeendring** der noe mangler eller kan forbedres

Dato: 2026-03-25

---

## 1) Publisere egen informasjon (brukeroppdagelse / presence via broadcast)

- **Krav**: Ved oppstart sende UDP broadcast med brukernavn og IP-adresse  
  - **Implementert i**:
    - `DiscoveryService::send_presence_broadcast()` i `src/services/discovery_service.cpp`
    - Oppstart kaller `send_presence_broadcast()` i `src/main.cpp`
  - **Status**: Fullført  
  - **Svakheter/mangler**:
    - Bruker `INADDR_BROADCAST` (limited broadcast). På enkelte nett kan per-interface broadcast-adresse være nødvendig.

- **Krav**: Lytte etter annonseringer fra andre brukere  
  - **Implementert i**:
    - SECP parsing/routing i `run_receive_multiplex_loop(...)` i `src/app/receive_multiplex_loop.cpp`
    - `DiscoveryService::on_presence(...)` i `src/services/discovery_service.cpp`
  - **Status**: Fullført  
  - **Svakheter/mangler**:
    - Presence-pakker droppes hvis “announced IPv4” ikke matcher avsender-IP (bevisst validering; kan gi “manglende oppdagelse” ved flere NIC eller uvanlige nettoppsett).

- **Krav**: Vise liste over aktive brukere i CLI  
  - **Implementert i**: `print_active_users()` i `src/app/text_menu.cpp`
  - **Status**: Fullført  

- **Krav**: Periodisk heartbeat/broadcast hvert 5–10 sekund  
  - **Implementert i**:
    - `kDiscoveryHeartbeatInterval{5000}` i `include/chat/services/discovery_service.hpp`
    - Heartbeat-tråd i `src/main.cpp`
  - **Status**: Fullført  

---

## 2) Oppdatering av aktiv bruker-liste (lastSeen + pruning)

- **Krav**: Lagre `username`, `ip`, `lastSeen`  
  - **Implementert i**: `UserDirectory` i `include/chat/state/user_directory.hpp` og `src/state/user_directory.cpp`
  - **Status**: Fullført  

- **Krav**: Automatisk oppdatering av listen ved mottak  
  - **Implementert i**:
    - `UserDirectory::upsert(...)`
    - Kalles fra `DiscoveryService::on_datagram(...)`
  - **Status**: Fullført  

- **Krav**: Fjerne brukere som ikke er sett på en stund (15–20s)  
  - **Implementert i**:
    - `kDiscoveryPeerStaleAge{18}` i `include/chat/services/discovery_service.hpp`
    - `UserDirectory::prune_stale(...)`
    - Kalles fra heartbeat og discovery-recv
  - **Status**: Fullført  
  - **Svakheter/mangler**:
    - Ingen kritiske. Løsningen er robust siden pruning skjer periodisk.

---

## 3) Åpent globalt rom via UDP broadcast (USN Chat)

- **Krav**: Alle klienter kan sende og motta meldinger i fellesrommet  
  - **Implementert i**:
    - `LobbyService::send_chat()` og `LobbyService::on_lobby_chat()` i `src/services/lobby_service.cpp`
    - Menyvalg 2 (USN Chat) i `src/app/text_menu.cpp`
  - **Status**: Fullført  

- **Krav**: Meldingen inneholder avsendernavn og tekst  
  - **Implementert i**: RFC/SECP-format `CHAT|USN Chat|username|message\n` (se `include/chat/protocol/secp.hpp` + `src/protocol/secp.cpp`)
  - **Status**: Fullført  

- **Krav**: Ikke tolke alle broadcastpakker som chatmeldinger  
  - **Implementert i**: `run_receive_multiplex_loop(...)` ruter kun `CHAT` til USN Chat når `ROOM == "USN Chat"`
  - **Status**: Fullført  

- **Krav**: Valider lengde, håndter tom input, robust parsing  
  - **Implementert i**:
    - CLI validering i `prompt_lobby_message()` i `src/app/text_menu.cpp`
    - `LobbyService::send_chat()` validerer tomt + maksgrenser
    - `run_receive_multiplex_loop(...)` dropper malformed/ukjente SECP-meldinger uten crash
  - **Status**: Fullført  

---

## 4) Opprettelse og annonsering av åpne grupperom (broadcast advert)

- **Krav**: Bruker kan opprette grupperom med navn, multicast-IP og port  
  - **Implementert i**: `GroupRoomCoordinator::create_room()` i `src/services/group_room_coordinator.cpp`
  - **Status**: Fullført  
  - **Detalj**:
    - RFC-forenkling: åpne rom bruker multicast `239.0.0.1:50000` (TTL=1), filtrert på `ROOM`.

- **Krav**: Opprettede grupperom annonseres via broadcast hvert 5–10 sekund  
  - **Implementert i**:
    - `kGroupAdvertBroadcastInterval{6}` i `include/chat/services/group_room_coordinator.hpp`
    - `groups.broadcast_owned_advert()` kalles i egen tråd i `src/main.cpp`
  - **Status**: Fullført  

- **Krav**: Andre brukere kan se tilgjengelige grupperom  
  - **Implementert i**:
    - Mottak/lagring: `GroupRoomCoordinator::on_advert_datagram()` i `src/services/group_room_coordinator.cpp`
    - Listing: `GroupRoomCoordinator::list_advertised_rooms()` + `print_advertised_rooms()` i `src/app/text_menu.cpp`
  - **Status**: Fullført  

- **Krav**: Robust feilhåndtering (invalid IP/port/format)  
  - **Implementert i**: `on_advert_datagram()` validerer type, parse, multicastområde og port-konflikter
  - **Status**: Fullført  

---

## 5) Multicast join/leave og mottak/sending av gruppemeldinger

- **Krav**: Join multicast-gruppen ved innmelding  
  - **Implementert i**:
    - `GroupRoomCoordinator::join_room()` → `bind_multicast_channel(...)`
    - `UdpSocket::multicast_add_membership(...)` (`IP_ADD_MEMBERSHIP`)
  - **Status**: Fullført  

- **Krav**: Forlate multicast-gruppen  
  - **Implementert i**:
    - `GroupRoomCoordinator::leave_room()` → `leave_multicast_membership_unlocked()`
    - `UdpSocket::multicast_drop_membership(...)` (`IP_DROP_MEMBERSHIP`)
  - **Status**: Fullført  
  - **Svakheter/mangler**:
    - Ingen vesentlige. Feil ved `IP_DROP_MEMBERSHIP` logges nå med errno.

- **Krav**: Gruppemeldinger sendes via UDP multicast  
  - **Implementert i**: `GroupRoomCoordinator::send_group_chat()` i `src/services/group_room_coordinator.cpp`
  - **Status**: Fullført  

- **Krav**: Gruppemeldinger mottas via UDP multicast  
  - **Implementert i**:
    - `GroupRoomCoordinator::try_recv_multicast()` + `on_multicast_datagram()`
    - `run_receive_multiplex_loop()` poller multicast-fd
  - **Status**: Fullført  

- **Krav**: Ikke la samme bruker joine samme rom flere ganger  
  - **Implementert i**: `GroupRoomCoordinator::join_room()` sjekker om aktivt rom allerede er valgt
  - **Status**: Fullført  

- **Merk / begrensning**:
  - Klienten støtter **kun ett aktivt grupperom om gangen** (forenkling; ofte OK i skoleoppgave).

---

## 6) 1-1 rom via UDP unicast (invitasjon + accept/reject)

- **Krav**: Invitasjon sendes via UDP unicast til valgt aktiv bruker  
  - **Implementert i**: `DirectMessageService::invite_user()` i `src/services/direct_message_service.cpp`
  - **Status**: Fullført  

- **Krav**: Den inviterte kan akseptere eller avslå  
  - **Implementert i**:
    - `DirectMessageService::accept_invite()` / `decline_invite()`
    - Menyvalg 9 i `src/app/text_menu.cpp`
  - **Status**: Fullført  

- **Krav**: Kun invitert bruker skal kunne delta  
  - **Implementert i**: `DirectMessageService::handle_invite()` sjekker `to_username == self_username_`
  - **Status**: Fullført  

---

## 7) Privat chat via UDP unicast (kun aktive sesjoner)

- **Krav**: Private meldinger behandles kun dersom det finnes en gyldig etablert samtale  
  - **Implementert i**:
    - Send: `DirectMessageService::send_private_chat()` krever `Active`
    - Mottak: `handle_chat()` krever `Active` + endpoint-match
  - **Status**: Fullført  

- **Krav**: Ignorér private meldinger fra ukjente/ikke-godkjente avsendere  
  - **Implementert i**: `same_endpoint(...)` mot etablert `peer_addr`
  - **Status**: Fullført  

---

## 8) Ugyldig input (CLI)

- **Krav**: Robust håndtering av tom input og ugyldige valg  
  - **Implementert i**: `src/app/text_menu.cpp` (validering, trim, avbryt ved tom linje)
  - **Status**: Fullført  

- **Krav**: Maks meldingslengde og “short strings” valideres  
  - **Implementert i**:
    - `kMax*` grenser fra `include/chat/protocol/limits.hpp`
    - CLI checks (melding, romnavn, room_id/session_id)
    - Services gjør egne checks (defensivt)
  - **Status**: Fullført  

---

## 9) Nettverksfeil / robust feilhåndtering

- **Krav**: Sjekk returverdier og logg meningsfulle feilmeldinger  
  - **Implementert i**:
    - `UdpSocket` setter `last_errno()` ved feil (se `include/chat/net/udp_socket.hpp` + `src/net/udp_socket.cpp`)
    - `Logger` logger socket-feil og parsingfeil
    - `receive_multiplex_loop` logger `poll()`/`recv`-feil
  - **Status**: Fullført  

- **Krav**: Håndter at nettverkskall kan feile uten at programmet krasjer  
  - **Implementert i**: SECP parsing/routing dropper ved parse-feil; recv-loop fortsetter ved recoverable feil
  - **Status**: Fullført  

---

## 10) Avslutning av program / ren shutdown

- **Krav**: Tråder stoppes rent ved avslutning  
  - **Implementert i**:
    - `InterruptibleSleep` + `request_stop()` i `src/main.cpp`
    - `running` atomic stopper loop, sockets lukkes, tråder joines
  - **Status**: Fullført  

- **Krav**: Program håndterer stdin EOF  
  - **Implementert i**: `run_text_menu_loop()` i `src/app/text_menu.cpp` (avslutter ved `getline` EOF)
  - **Status**: Fullført  

---

## 11) Periodisk republisering (5–10 sekund)

- **Krav**: “Invitasjoner til åpne rom” republiseres periodisk  
  - **Implementert i**: `GroupAdvert` broadcast hvert ~6s (`kGroupAdvertBroadcastInterval`)  
  - **Status**: Fullført  

- **Mulig tolkning**: 1-1 invitasjoner bør også republiseres til de blir besvart/utløper  
  - **Implementert i**:
    - `DirectMessageService::resend_outgoing_pending(...)` i `src/services/direct_message_service.cpp`
    - Kalles periodisk fra heartbeat i `src/main.cpp`
  - **Status**: Fullført  
  - **Detalj**:
    - Resend-interval: ~6s
    - Utløp (max_age): ~45s (invitasjoner fjernes og det gis statusmelding)

---

## Oppsummering

- **Krav 1–4** (presence, USN Chat, grupperom, 1-1): **Implementert**
- **Periodisk republisering**: **Presence + romannonser ja**, **1-1 invites ja** (utgående `DirectInvite` resend’es periodisk til svar/utløp)
- **Robusthet/stabilitet**: God (defensiv parsing, logging, `poll()`, clean shutdown)
- **Buffer overflow prevention**: Ivaretatt med maksgrenser, trygge buffere, og validering i både CLI og services


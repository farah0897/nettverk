# Rapportgrunnlag oblig 2

Tekstutkast til rapport. Juster person og figurer selv. Filnavn er relative til prosjektrot.

---

## 1. Innledning

Oblig 2 bygger på oblig 1: SECP over UDP 50000 (RFC USNChat01 i kurset). Nytt: TCP 50001 med klartekst-SECP (garantert rom), TCP 50002 med ChaCha20+HMAC etter PSK i UDP INVITE (sikkert rom).

Rapporten dekker teori, protokoll (RFC vs. denne koden), arkitektur, implementasjon, feil, sikkerhet, test og Wireshark. Nøkkel i klartekst på UDP og andre begrensninger er med vilje enkle.

Les kap. 3-4 sammen med `include/chat/protocol/` og `src/services/`.

---

## 2. Relevant teori

### 2.1 UDP og TCP

UDP uten tilkobling; brukt til SECP på 50000 (broadcast, unicast, INVITE til sikker TCP). TCP gir strøm og rekkefølge; 50001 er garantert rom (klartekst-linjer), 50002 sikkert rom (krypto), adskilt portmessig.

### 2.2 SECP som tekstprotokoll

Én logisk linje: `TYPE|ROOM|USERNAME|PAYLOAD` + `\n`. Validering i `secp.cpp`. Sjekk kurs-RFC for detaljer; denne koden kan avvike noe.

### 2.3 Symmetrisk kryptering og integritet

PSK 32 byte. Encrypt-then-MAC: ChaCha20 + HMAC-SHA256 (`secure_message_crypto`). Ingen sikker nøkkelutveksling (PSK sendes på UDP i øvingen).

### 2.4 Samtidighet

Tråder: `run_receive_multiplex_loop`, heartbeat, annonsering, meny. Mutex i tjenester; `cout_mutex` for utskrift (`sync_output.cpp`).

---

## 3. Protokollkapittel (RFC USNChat01 og faktisk implementasjon)

### 3.1 Hva RFC USNChat01 typisk beskriver (overordnet)

I kursopplegget knyttes **RFC USNChat01** til SECP-linjer, bruk av **UDP port 50000**, og konsepter som **PRESENCE**, **CHAT**, **ROOM_ANNOUNCE**, **INVITE**, åpne og lukkede rom, samt **TCP for garanterte rom**. Den presise teksten i PDF/Word-utgaven av RFC-en må siteres i den endelige rapporten - **denne kodebasen er normerende for hva som faktisk sendes på ledningen**.

### 3.2 Porter (implementasjon)

| Transport | Port | Bruk i koden |
|-----------|------|----------------|
| UDP | **50000** | All SECP på UDP (`kUdpPort` i `include/chat/protocol/ports.hpp`) |
| TCP | **50001** | Garantert åpent rom (`kTcpPort`) |
| TCP | **50002** | Sikkert rom (`kTcpSecurePort`) |

**Merk:** RFC-en i kursmateriellet kan formulere portvalg annerledes; her er portene **hardkodet** som konstanter og samsvarer med menytekst og dokumentasjon i repoet.

### 3.3 SECP-linjeformat (implementasjon)

- Bygging og parsing: `build_secp_line` / `parse_secp_line` i `src/protocol/secp.cpp`.
- Makslengder: `kMaxPacketBytes` (1024) for hele linjen, egne grenser for brukernavn, romnavn og payload (`include/chat/protocol/limits.hpp`).

**RFC vs kode:** Hvis RFC-en krever spesifikke payload-strenger for visse typer, må hver TYPE sjekkes enkeltvis. Her er eksempler på **faktisk bruk**:

- **PRESENCE:** `PRESENCE|-|username|<IPv4>` (broadcast) - `DiscoveryService`.
- **Lobby:** `CHAT|USN Chat|username|tekst` - `LobbyService`.
- **Multicast-gruppe (oblig 1-stil):** `ROOM_ANNOUNCE|rom|eier|OPEN` (UDP); gruppechat som `CHAT|rom|...` over **multicast** til `239.0.0.1:50000` - se `GroupRoomCoordinator` (forenklet felles multicast-gruppe i `limits.hpp`).
- **Garantert TCP-rom (oblig 2):** `ROOM_ANNOUNCE|rom|vert|OPEN;tcp=50001` på **UDP** for annonsering; **TCP 50001** bærer `CHAT|rom|bruker|tekst\n` som ren tekst.
- **Lukket UDP-invitasjon (oblig 1):** `INVITE` med payload som starter med `CLOSED;to=...` (uten `secure_tcp=1`) - `DirectMessageService`.
- **Sikker TCP-invitasjon (oblig 2):** `INVITE` med payload som inneholder `secure_tcp=1`, `sess=`, `key=` (heks), osv. - `SecureRoomService`; dette er en **implementasjonsutvidelse** utover en minimal «kun UDP»-beskrivelse.

### 3.4 TCP-nyttelast: to varianter

| Modus | Framing | Innhold |
|--------|---------|-----------|
| Garantert 50001 | Linje med `\n` | Full SECP-tekst i klartekst (`TcpLineReader`, `kMaxPacketBytes`) |
| Sikkert 50002 | 2 byte lengde (big-endian) + blob | `seal_message`-utdata: 12 B nonce + ciphertext + 32 B HMAC; inni ciphertext ligger UTF-8 som tolkes som SECP-linje etter dekryptering |

**Skill RFC / kode:** En ren SECP/RFC-beskrivelse omtaler ofte **linjeorientert** protokoll. **Sikker TCP** i denne koden er **ikke** «ren SECP på ledningen» - den er **binær innkapsling** av kryptert SECP-innhold.

---

## 4. Design og arkitektur

### 4.1 Lagdeling

- **Protokoll:** `secp.hpp` / `secp.cpp`, `tcp_line_reader.hpp`, samt payload-hjelpere (f.eks. `secure_invite.hpp`).
- **Transport:** `UdpSocket`, `TcpServer`, `TcpClient`, `TcpConnection` under `src/net/`.
- **Tjenester:** Én klasse per domene - `DiscoveryService`, `LobbyService`, `GroupRoomCoordinator`, `DirectMessageService`, `GuaranteedOpenRoomService`, `SecureRoomService` (`src/services/`).
- **Orkestrering:** `main.cpp` oppretter objekter, starter **mottakstråd** med `run_receive_multiplex_loop`, **heartbeat** og **periodisk annonsering** for grupperom og garantert TCP-vert, og kjører `run_text_menu_loop` på hovedtråden.

### 4.2 Mottaksløkken

`run_receive_multiplex_loop` (`src/app/receive_multiplex_loop.cpp`) poller:

1. UDP-socket og ev. **multicast-socket** (egen `UdpSocket` i `GroupRoomCoordinator`, ikke samme fd som broadcast-UDP).
2. TCP-fd-er fra `GuaranteedOpenRoomService::append_poll_entries` og `SecureRoomService::append_poll_entries`.

Innkommende UDP/multicast behandles som én SECP-linje per datagram (innen `kMaxPacketBytes`). **Routing:** `switch` på `SecpType`; `INVITE` med `is_secure_tcp_invite_payload` går til **sikkert rom**, ellers til **direkte melding**. `ROOM_ANNOUNCE` sendes både til multicast-gruppelogikk og til **garantert TCP-register**.

### 4.3 Meny

CLI er samlet i `src/app/text_menu.cpp`: oblig 1-relaterte valg og oblig 2-valg (opprette/liste/join garantert TCP, sikker invitasjon/svar, sende melding, status, forlate moduser, avslutt). Dette er **bruksgrensesnitt**, ikke en del av RFC-en.

---

## 5. Implementasjon av åpent garantert rom

### 5.1 Vert (host)

`GuaranteedOpenRoomService::start_hosting` (`src/services/guaranteed_open_room_service.cpp`) binder **TCP-server** på `kTcpPort` (50001), forlater eventuell multicast-medlemskap via `GroupRoomCoordinator::leave_room()`, og kaller `broadcast_owned_advert_unlocked()` som sender SECP `ROOM_ANNOUNCE` på **UDP broadcast** med payload **`OPEN;tcp=50001`** (port som tall, jf. `parse_open_tcp_payload`).

### 5.2 Klient

Klienter samler annonser i `tcp_registry_` fra `on_room_announce`. `join_as_client` slår opp **vertens IPv4 og TCP-port** og åpner **TCP-klient** med `TcpClient::connect_ipv4`. Meldinger sendes med `send_chat`, som bygger `CHAT|room|self|tekst\n` og sender med `send_all` til vert eller til klientens ene TCP-forbindelse.

### 5.3 Vert: aksept og relay

Verten aksepterer inntil et maksimalt antall klienter, bruker `TcpLineReader` per klient, parser `parse_secp_line`, godtar kun **`Chat`** i **aktivt romnavn**, og **relayer** linjen til andre tilkoblede klienter. **Autorisasjon:** Det finnes **ingen** kobling mellom TCP-klient og brukerkatalog - «åpent» rom betyr i praksis at **hvem som helst som kan nå IP:50001** kan delta (sikkerhetsmessig begrensning, se kap. 8).

### 5.4 RFC vs implementasjon

RFC USNChat01 omtaler garantert levering over TCP; **denne koden** realiserer det som **newline-SECP over én TCP-strøm per klient**. Eventuelle krav i RFC om eksplisitt «join»-melding på TCP er **ikke** nødvendigvis implementert - tilkobling + gyldig `CHAT` er nok i praksis.

---

## 6. Implementasjon av sikkert rom

### 6.1 Invitasjon (UDP)

`SecureRoomService::send_secure_invite` trekker **tilfeldig 32-byte PSK** (`crypto::fill_random`), genererer **sesjons-ID** (`make_session_id`, kort heks-streng), bygger INVITE-payload med `CLOSED;to=...`, `secure_tcp=1`, `port=50002`, `sess=...`, `key=<64 hex tegn>`, og sender SECP-linjen som **UDP** til motpartens adresse fra `UserDirectory`. Verten starter **TCP-lytting** på `kTcpSecurePort`.

**Begrensning (ærlig):** PSK sendes **i klartekst i UDP-payload**. Det er dokumentert i krypto-headeren som **pedagogisk**; i produksjon må nøkkel aldri sendes slik.

### 6.2 Mottak av INVITE

`on_udp_invite` validerer blant annet: riktig mottaker (`to_user`), samsvar mellom SECP `ROOM`-felt og `sess=` i payload, at UDP-avsenderens IP matcher katalogen for **inviter**, og at `parse_secure_invite_payload` lykkes - inkludert **obligatorisk `key=`** (heks). Se `src/services/secure_room_service.cpp`.

### 6.3 TCP og krypto

- **Invitee:** `accept_secure_invite` kobler TCP til vert, bygger første SECP `CHAT` med fast håndtrykk-payload `kSecureTcpHandshakePayload`, **krypterer** med `seal_message`, sender **2-byte lengde + blob**.
- **Vert:** Etter `accept` venter `HostHandshakePending` til første ramme er dekryptert med `open_message`, SECP er gyldig `CHAT`, og `getpeername` samsvarer med **inviterts** IP i katalogen - deretter `HostActive`.
- **Chat:** `send_secure_chat` bygger SECP `CHAT`, krypterer, sender samme rammeformat.

**RFC vs implementasjon:** RFC USNChat01 beskriver sannsynligvis ikke **ChaCha20+HMAC** og ikke `secure_tcp=1`; dette er **oblig 2-spesifikk utvidelse** i denne kodebasen.

---

## 7. Feilhåndtering

### 7.1 UDP

- Datagram større enn `kMaxPacketBytes`: **droppes**, logges (`receive_multiplex_loop`).
- `parse_secp_line` feiler: melding **ignoreres**.
- Unntak i type-spesifikke handlere: fanget i `try/catch`, logges, løkken **fortsetter**.

### 7.2 TCP garantert

- `TcpLineReader`: for lang linje uten `\n`, eller for stor pending-buffer → **Error** / lukking av klient.
- Send-feil ved relay: klient **fjernes** fra listen.

### 7.3 TCP sikkert

- Ugyldig MAC eller ugyldig håndtrykk: **avvisning** av klient eller **leave** av sesjon, avhengig av fase (`reject_unauthorized_peer_unlocked`, `leave_unlocked`).
- `poll`-feil: logg, kort pause, fortsett (UDP-løkken).

### 7.4 Begrensninger

Ikke alle feil gir brukervennlig melding i CLI - noe logges kun via `Logger`. Dette er typisk for bakgrunnstråd, men bør nevnes under testing.

---

## 8. Sikkerhetsvurdering

Kort oppsummering (utdypes gjerne med eget kapittel eller vedlegg):

- **Styrker:** Encrypt-then-MAC, tilfeldig nonce per melding, MAC før dekryptering, noe konstant-tids MAC-sammenligning, IP-sjekk mot katalog for sikker INVITE og for vert ved håndtrykk.
- **Svakheter:** PSK i klartekst på UDP; ingen forward secrecy; ingen replay-beskyttelse på applikasjonslag; brukernavn er ikke kryptografisk bundet til identitet; garantert TCP er åpent for alle som når porten.

Se utdypning i `documentasjon/SIKKERHETSVURDERING_OBLIG2.md` (kan sammenfattes eller innlemmes).

---

## 9. Testing

### 9.1 Strategi

- **Manuell testing** via `text_menu`-valg (oblig 1- og oblig 2-flyter).
- **Flere terminaler** / **flere maskiner** på LAN.
- **Regresjon:** Verifiser at oblig 1 (multicast, lobby, privat UDP) fortsatt fungerer etter oblig 2-endringer.

### 9.2 Testplan

Strukturert plan med test-ID-er, forventet resultat og dokumentasjon finnes i `documentasjon/TESTPLAN_OBLIG2.md` - egnet å referere til eller lime inn som vedlegg.

### 9.3 Kvalitetsrevisjon

Intern gjennomgang av kode mot krav er dokumentert i `documentasjon/KVALITETSREVISJON_OBLIG2.md` (kan kort oppsummeres i rapporten).

---

## 10. Wireshark-analyse

### 10.1 Filtre

Bruk visningsfilter som `udp.port == 50000`, `tcp.port == 50001`, `tcp.port == 50002` for å skille signaling, garantert rom og sikkert rom.

### 10.2 Forventede observasjoner

- **UDP 50000:** Lesbar SECP (PRESENCE, ROOM_ANNOUNCE, INVITE med `key=` for sikker modus).
- **TCP 50001:** «Follow TCP Stream» viser **klartekst** `CHAT|...`.
- **TCP 50002:** Strømmen viser **binære** rammer; **ikke** lesbar chat-tekst uten dekryptering.

### 10.3 OSI

Knytt **applikasjonslag** (SECP-innhold) til **UDP/TCP** (transport) og **IPv4** (nettverk) i skjermbilder.

**Detaljert studentguide:** `documentasjon/TESTPLAN_OBLIG2.md` (seksjon om Wireshark).

---

## 11. Konklusjon

Oblig 2 er implementert ved å **bevare SECP over UDP** for discovery og signaling, legge til **TCP 50001** for **åpent garantert rom** med linjeorientert SECP i klartekst, og **TCP 50002** for **sikkert rom** med **innkapslet kryptografi** og nøkkelutveksling over UDP som er **enkelt og sårbart**, men **pedagogisk forklart** i kode og kommentarer. Arkitekturen med **én mottaksløkke** som multiplexer UDP og TCP gir en ryddig struktur, men krever disiplinert **feilhåndtering** og forståelse av **trådsikker utskrift**.

**Begrensning:** Løsningen er **ikke** produksjonsklar med hensyn til autentisering, nøkkelhåndtering og tilgangskontroll på garantert TCP. **RFC USNChat01** bør siteres presist der rapporten hevder samsvar - denne teksten beskriver **implementasjonen**; avvik (utvidet INVITE, felles multicast-gruppe, faste porter) må listes eksplisitt i den endelige innleveringen.

**Mulig videre arbeid:** TLS, sterk nøkkelutveksling, AEAD-standard, oppdatert dokumentasjon i repo (`TESTING_GUIDE.md`, `CODE_WALKTHROUGH.md`) slik at den matcher menyer og TCP.

---

## Lim inn i rapportmal - sjekkliste

- [ ] Bytt ut «vi»/«dette prosjektet» med ditt valg av person (jeg/vi/gruppe).
- [ ] Sett inn figurhenvisninger (arkitekturdiagram, Wireshark-skjermbilder).
- [ ] Lim inn **eksakt** RFC-sitat og referanse fra kursmateriellet der protokollen beskrives formelt.
- [ ] Bekreft gruppenavn, studentnummer og eventuell KI-erklæring etter studiets mal.
- [ ] Kryssjekk at portnumre og menypunkter samsvarer med **din** innleverte `text_menu`-versjon.

---

*Fil opprettet som rapportgrunnlag - oppdater ved endringer i kode eller krav.*

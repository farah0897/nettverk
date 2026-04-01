# Kvalitetsrevisjon: oblig 2 og RFC USNChat01 / SECP

**Revisjonsomfang:** Hele kodebasen (`src/`, `include/`, `documentasjon/`) mot oblig 2-krav og intern SECP-implementasjon (`TYPE|ROOM|USERNAME|PAYLOAD\n`, porter i `ports.hpp`).  
**Merk:** Offisiell RFC USNChat01 ligger utenfor repo; vurderingen bruker kode + prosjektets egen dokumentasjon som referanse.

---

## Sammendrag

Oppsummert: UDP 50000 for SECP, egen multicast-socket, TCP 50001 (garantert, klartekst-linjer) og 50002 (lengde + ChaCha20/HMAC). Ugyldige/oversized UDP-droppes; TCP-linjer begrenset av `kMaxPacketBytes`. Hovedproblem: utdatert dokumentasjon (TCP/meny) i eldre `.md`-filer. Mulig navnekollisjon mellom multicast-romnavn og privat CHAT-rom. `parse_secure_invite_payload` krever nå `key=` (fikset). Token-lignende streng i `CODE_WALKTHROUGH.md` er fjernet; roter GitHub-PAT hvis det var ekte.

**Endelig score: 86 %** (se avsnitt nederst; justert etter `key=`-fiks).

---

## Krav-for-krav tabell

| # | Krav / kontrollpunkt | Status | Kommentar |
|---|----------------------|--------|-----------|
| **1** | Oblig 1-funksjoner (PRESENCE, lobby, multicast-gruppe, lukket UDP) | **OK** | `run_receive_multiplex_loop` ruter `Presence`, `CHAT` (lobby + gruppe + direkte), `ROOM_ANNOUNCE`, `INVITE` (ikke-sikker → `DirectMessageService`). `GroupRoomCoordinator` bruker egen multicast-socket på `239.0.0.1:50000`. |
| **2** | Åpent garantert rom (annonse + TCP-chat) | **OK** | `GuaranteedOpenRoomService`: `ROOM_ANNOUNCE` med `OPEN;tcp=50001` på UDP, `listen_on(50001)`, klient `join_as_client`, `CHAT` via `TcpLineReader` + `parse_secp_line`. |
| **3** | Sikkert rom (INVITE, TCP, krypto) | **OK m/ anmerkning** | `SecureRoomService`: UDP INVITE med `secure_tcp=1`, TCP 50002, `seal_message`/`open_message` på nyttelast. PSK i klartekst på UDP er **bevisst pedagogisk svakhet**. |
| **4** | TCP server/klient robust | **OK** | `SO_REUSEADDR`, ikke-blokkerende sockets, poll, `send_all`, klientgrense (garantert), ramme-lengde (sikker). `key=` er obligatorisk i parse. |
| **5** | Kryptering brukt i sikkert rom | **OK** | `send_secure_chat`, `accept_secure_invite` (første melding), og mottak i `process_tcp_poll_events` kaller `crypto::seal_message` / `open_message`. |
| **6** | Invitasjoner riktig UDP-type | **OK** | `SecpType::Invite` på UDP 50000; routing: `is_secure_tcp_invite_payload` → `SecureRoomService`, ellers `DirectMessageService::on_invite`. |
| **7** | TCP-format og framing | **OK** | Garantert: én SECP-linje med `\n` per melding (`TcpLineReader`). Sikker: 2-byte big-endian lengde + sealed blob (`read_frame_unlocked`). `key=` er påkrevd ved parsing av sikker INVITE (etter fiks). |
| **8** | Buffer overflow-forebygging | **OK** | UDP: `kMaxPacketBytes+1` buffer, dropp hvis `n > kMaxPacketBytes`. TCP linje: `kMaxPacketBytes` / `kMaxPending`. Sikker TCP: `flen <= kMaxTcpFrame` (4096). |
| **9** | Feilhåndtering | **Delvis OK** | Try/catch rundt UDP- og TCP-handlere, logging, lukking ved ugyldig MAC/handshake. Noen stier returnerer stille `false` uten brukerfeedback (OK for bakgrunnstråd). |
| **10** | Dokumentasjon vs kode | **Svak** | `RAPPORT_OBLIG1.md`, deler av `TESTING_GUIDE.md` og `CODE_WALKTHROUGH.md` hevder TCP ubrukt eller feil meny; **må oppdateres**. |

---

## Avviksliste

| ID | Alvor | Beskrivelse | Referanse |
|----|-------|-------------|-----------|
| A1 | **Høy** | Dokumentasjon i `RAPPORT_OBLIG1.md` sier TCP 50001 «ikke brukt» - **motsier** nåværende `main.cpp` og `GuaranteedOpenRoomService`. | `documentasjon/RAPPORT_OBLIG1.md` §5 tabell |
| A2 | **Høy** | `TESTING_GUIDE.md` forventer `TCP 50001 (ikke i bruk)` og meny **1-12** - **utdatert** (nå TCP 50001/50002 og utvidet meny). | `documentasjon/TESTING_GUIDE.md` ~143-145 |
| A3 | **Medium** | `CODE_WALKTHROUGH.md` sier «TCP … ikke implementert» og at multicast mottas via «samme UDP-socket» - **feil**: garantert TCP er implementert; multicast bruker **egen** `UdpSocket` (`mcast_sock_`). | Linje ~70-71, ~88 |
| A4 | **Medium** *(mitigert i kode etter revisjon)* | Tidligere: `parse_secure_invite_payload` kunne returnere `true` uten vellykket `key=` → **null-PSK**. Nå kreves `key_ok` (vellykket `from_hex`). | `src/services/secure_room_service.cpp` |
| A5 | **Lav** | Mulig **navnekollisjon**: `DirectMessageService` indekserer sesjoner på `msg.room` (romnavn), og `GroupRoomCoordinator` filtrerer gruppechat på `msg.room`. Samme streng som både gruppe- og privat-romnavn kan gi forvirring (sjelden i praksis). | `direct_message_service.cpp`, `group_room_coordinator.cpp` |
| A6 | **Lav** | `CHAT`-handler kaller sekvensielt `lobby`, `groups`, `direct` - korrekt filtrering per tjeneste, men **forståelsesmessig** tung å lese; ingen funksjonell feil funnet. | `receive_multiplex_loop.cpp` 118-124 |
| A7 | **Operativ** | Tidligere utilsiktet innliming av token-lignende streng i `CODE_WALKTHROUGH.md` - **fjernet** i revisjon. Ved ekte lekkasje: roter GitHub PAT. | `CODE_WALKTHROUGH.md` |

---

## Konkrete forslag til kodefikser

1. **`parse_secure_invite_payload`** - **utført:** `key_ok` kreves sammen med `sess` før aksept.

2. **Dokumentasjon**  
   - Oppdater `TESTING_GUIDE.md` (porter, menypunkter, forventet oppstartstekst).  
   - Oppdater `CODE_WALKTHROUGH.md` §1 og §6: beskriv `GuaranteedOpenRoomService`, `SecureRoomService`, to TCP-porter; rett multicast-mottak til **egen socket**.  
   - Legg til notis i `RAPPORT_OBLIG1.md` eller skriv `RAPPORT_OBLIG2.md` som erstatter utdaterte påstander om TCP.

3. **Navnekollisjon (valgfritt)**  
   - Prefiks for private sesjons-ID-er (f.eks. `priv_` + tilfeldig) eller adskilte felt i `DirectMessageService` vs gruppenavn - kun om dere vil hardgøre modellen.

4. **Tester**  
   - Legg inn én enhetstest eller manuell testliste som verifiserer avvist `INVITE` uten `key=` (etter fiks over).

---

## Endelig score: **86 %**

| Begrunnelse | Vekt |
|-------------|------|
| Kjernefunksjonalitet oblig 1 + 2 og SECP-routing er **implementert og i stor grad korrekt** | +43 |
| Krypto **brukes** riktig i sikkert rom; TCP-framing **skiller** 50001 og 50002; obligatorisk `key=` i parse | +20 |
| Buffer- og lengdegrenser er **systematisk** brukt | +10 |
| Dokumentasjonsdrift og utdaterte «TCP ikke i bruk»-påstander | −8 |
| Mindre arkitekturforvirring (multicast-socket vs tekst) og kollisjonsrisiko | −4 |
| Tidligere dokumentlekkasje/token (mitigert ved sletting) | −5 (operativ oppfølging ved ekte nøkkel) |

**Justér score opp til ~90-92 %** når `TESTING_GUIDE.md`, `CODE_WALKTHROUGH.md` og ev. `RAPPORT_OBLIG1.md` er synkronisert med kode.

---

*Dokument generert som kvalitetsrevisjon; oppdater ved større refaktorering.*

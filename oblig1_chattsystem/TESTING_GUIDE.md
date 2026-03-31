# TESTING_GUIDE.md — svært omfattende testguide (studentnivå)

Dette dokumentet er laget for at du skal kunne **teste hele prosjektet systematisk**, med **konkrete kommandoer du kan kopiere/lim(e)**, og med forklaring av **hva du forventer å se** i hver test. Målet er at du kan forstå og verifisere løsningen “innenfra” — som om du hadde skrevet den selv.

Prosjektet følger **RFC USNChat01 / SECP**: alle UDP-meldinger sendes som **UTF-8 tekst** på formen

`TYPE|ROOM|USERNAME|PAYLOAD\n`

og **all UDP-trafikk går på port `50000`**.

- **Discovery/presence (broadcast)**: `PRESENCE|-|username|ip`
- **Globalt rom (broadcast)**: `CHAT|USN Chat|username|message`
- **Åpne rom (annonser)**: `ROOM_ANNOUNCE|room-name|owner|OPEN`
- **Åpne rom (chat)**: `CHAT|room-name|username|message` via multicast til `239.0.0.1:50000` (TTL=1), filtrert på `ROOM`
- **Lukkede rom (invitasjon + chat)**: `INVITE|room-name|owner|CLOSED;to=invitedUser` og `CHAT|room-name|...` via unicast

Kilde for disse faktaene:

- `CMakeLists.txt` bygger binæren `chatapp`
- `src/main.cpp` printer porter ved oppstart og starter tråder
- `src/app/text_menu.cpp` definerer alle menyvalg og inputvalidering

---

## Forutsetninger (før du tester)

### Maskin og nett

Du kan teste på:

- **Én maskin** med flere terminaler (enkelt for utvikling), eller
- **To+ maskiner på samme LAN** (mest realistisk for broadcast og multicast)

Viktige nettverksnotater:

- UDP broadcast kan være begrenset av nettverket (f.eks. VM-bridge/NAT, WiFi-isolasjon).
- Multicast kan være blokkert i enkelte nett (IGMP/Router-policy). På samme maskin fungerer det ofte, men ikke alltid, avhengig av OS/nettstack.

### Pakker du trenger

På Ubuntu/Debian-lignende Linux:

```bash
sudo apt update
sudo apt install -y build-essential cmake
```

Valgfritt (nyttig for nettverksdebug):

```bash
sudo apt install -y iproute2 net-tools tcpdump
```

For **pakkefangst med GUI og OSI-visning** (se Testpakke 8):

```bash
sudo apt install -y wireshark
```

> På noen systemer heter pakken `wireshark-qt`. Under installasjon kan du bli spurt om å la ikke-root brukere fange pakker — da må brukeren være i gruppen `wireshark`, eller du kjører Wireshark med `sudo wireshark` (enkelt på lab, mindre ideelt på delt PC).

---

## Bygging (build) — “start her”

Kjør disse kommandoene i prosjektroten (`oblig1_chattsystem`).

### Test B1: Konfigurer build med CMake

**Kommando:**

```bash
cmake -S . -B build
```

**Forventet:**

- CMake genererer `build/` med Makefiles/Ninja-filer (avhengig av default generator).
- Ingen compile-feil ennå (kun config).

**Hvis det feiler:**

- Sjekk at `cmake` er installert.
- Sjekk at du står i riktig mappe.

### Test B2: Kompiler programmet

**Kommando:**

```bash
cmake --build build -j
```

**Forventet:**

- `build/chatapp` blir laget.
- Du får ingen linkerfeil.

### Test B3: Verifiser at binæren finnes og kan starte

**Kommando:**

```bash
./build/chatapp
```

**Forventet:**

- Programmet avslutter med feilkode 1 og skriver:
  - `Bruk: ... <brukernavn>`

Dette bekrefter at `argc`-sjekken i `src/main.cpp` fungerer.

---

## “Standardoppsett” for manuelle tester

De fleste testene under antar at du har 2–3 terminalvinduer.

### Terminal A (Farah)

```bash
./build/chatapp Farah
```

### Terminal B (Ole)

```bash
./build/chatapp Ole
```

### Terminal C (Charlie) (valgfritt, men nyttig)

```bash
./build/chatapp Charlie
```

**Forventet ved oppstart (alle):**

- Linje som viser at du er logget inn og lokal IP:
  - `Logget inn som "Farah" med IP X.Y.Z.W`
- Linje som viser porter:
  - `Porter: UDP 50000 (SECP), TCP 50001 (ikke i bruk).`
- Du får menyen med valg 1–12.

**Viktig å forstå mens du tester:**

- Programmet har en mottakstråd; derfor kan nettmeldinger dukke opp **midt i menyen** (det står også i `main.cpp`).

---

## Testpakke 1: Inputvalidering og “grunnleggende robusthet”

Disse testene verifiserer at programmet tåler vanlig feilbruk uten å krasje.

### Test R1: Tomt brukernavn

**Kommando:**

```bash
./build/chatapp "   "
```

**Forventet:**

- Programmet avslutter med:
  - `Brukernavn kan ikke være tomt.`

**Hva det tester:**

- `trim_in_place(username)` + tom-sjekk i `src/main.cpp`.

### Test R2: Ugyldig menyvalg (ikke tall)

**Steg i programmet:**

- Når du ser `Velg (1–12):`, skriv:
  - `hei`

**Forventet:**

- `Ugyldig valg. Skriv heltall 1–12.`
- Programmet fortsetter (ingen crash).

**Hva det tester:**

- `parse_menu_choice` (strtol + range check) i `src/app/text_menu.cpp`.

### Test R3: Ugyldig menyvalg (utenfor range)

**Steg:**

- Skriv `0` eller `13`.

**Forventet:**

- Samme feilmelding som over.

### Test R4: Avbryt meldinger med tom linje

**Steg:**

- Velg `2` (Send melding til fellesrom)
- Når du får prompt `>`, trykk Enter uten tekst.

**Forventet:**

- `(Ingen melding sendt.)`

**Hva det tester:**

- Avbruddsmønsteret “tom linje = avbryt” i menylogikken.

---

## Testpakke 2: Discovery/presence (UDP broadcast 50000)

Målet er å verifisere at klientene oppdager hverandre og at katalogen oppdateres.

### Test D1: Se aktive brukere (etter 5–10 sekunder)

**Steg (i Farah-terminalen):**

- Vent ~5–10 sekunder etter at Ole er startet.
- Velg `1` (Vis aktive brukere).

**Forventet:**

- Farah ser Ole:
  - `Ole @ <ipv4> — sist sett for <n> s siden`

**Gjenta i Ole-terminalen** og forvent å se Farah.

**Hva det tester:**

- `DiscoveryService` sender “presence” og mottar andres.
- `UserDirectory::upsert` + `snapshot_peers`.
- Periodisk heartbeat (`kDiscoveryHeartbeatInterval`) holder “last seen” fersk.

### Test D2: Stale pruning (bruker forsvinner)

**Steg:**

- Lukk Ole ved å velge `12` (Avslutt).
- Vent litt (du kan sjekke liste flere ganger med `1`).

**Forventet:**

- Ole forsvinner fra listen etter at `prune_stale_peers()` har kjørt nok ganger og “max age” er passert.

**Hva det tester:**

- At katalogen ikke vokser ukontrollert og at “online”-status er basert på “last seen”.

---

## Testpakke 3: USN Chat (globalt rom via UDP broadcast 50000)

Målet er å verifisere broadcast-chat i fellesrommet.

### Test L1: Farah sender USN Chat-melding, Ole mottar

**Steg (Farah):**

- Velg `2`
- Skriv:
  - `Hei fra Farah`

**Forventet (Farah):**

- Farah printer sin egen melding lokalt:
  - `[USN Chat] Farah: Hei fra Farah`

**Forventet (Ole):**

- Ole får den via nett:
  - `[USN Chat] Farah: Hei fra Farah`

**Hva det tester:**

- `LobbyService::send_chat` encoder `LobbyChatPayload` og sender broadcast.
- `LobbyService::on_datagram` decoder og trigges callback.
- I `main.cpp` er callback satt til å ignorere egne meldinger (`if (sender == username) return;`).

### Test L2: Maks-lengde på USN Chat-melding (lokal validering)

**Steg:**

- Velg `2`
- Lim inn en veldig lang tekst (lengre enn maks).

**Forventet:**

- `Meldingen er for lang.`

**Hva det tester:**

- Menyen sjekker `msg.size() > kMaxChatMessageBytes` før sending.

---

## Testpakke 4: Grupperom (annonser via broadcast + chat via multicast)

Dette er todelt:

1. Rommet annonseres til alle via broadcast (50000)
2. Chatten i rommet går via multicast når du har joinet

### Test G0: Forstå menyflyten for grupperom

- `3` oppretter rom (du “eier” rommet og blir aktiv i det).
- `4` viser annonserte rom (fra broadcast).
- `5` joiner et rom ved rom-ID.
- `6` sender melding til aktivt rom (multicast).
- `7` forlater rommet (drop membership og nullstiller aktivt rom).

### Test G1: Farah oppretter rom, Ole ser det i liste

**Steg (Farah):**

- Velg `3`
- Skriv romnavn, f.eks.:
  - `Gruppe1`

**Forventet (Farah):**

- `Grupperom opprettet. Ditt rom-ID: <id>`

**Steg (Ole):**

- Vent noen sekunder (annonser sendes periodisk).
- Velg `4` (Vis tilgjengelige grupperom).

**Forventet (Ole):**

- Ser en linje som:
  - `ID: <id> — «Gruppe1» @ 239.255.42.X:42YYY`

**Hva det tester:**

- `GroupRoomCoordinator::broadcast_owned_advert()` sender `ROOM_ANNOUNCE` på UDP 50000.
- Registry på mottakersiden oppdateres og listes via `list_advertised_rooms()`.

### Test G2: Ole joiner rom og mottar multicast-chat

**Steg (Ole):**

- Velg `5`
- Lim inn rom-ID fra `4`

**Forventet (Ole):**

- `Du er nå med i rom <id>.`

**Steg (Farah):**

- Velg `6`
- Skriv:
  - `Velkommen til Gruppe1`

**Forventet (Farah):**

- `[Gruppe <id>] Farah: Velkommen til Gruppe1`

**Forventet (Ole):**

- Ole mottar:
  - `[Gruppe <id>] Farah: Velkommen til Gruppe1`

**Hva det tester:**

- `join_room` fører til multicast membership (`IP_ADD_MEMBERSHIP`).
- Multicast socket blir inkludert i `poll()` via `multicast_receive_fd()`.

### Test G3: Ole forlater rom, slutter å motta multicast

**Steg (Ole):**

- Velg `7` (Forlat grupperom).

**Forventet (Ole):**

- `Du har forlatt grupperommet.`

**Steg (Farah):**

- Send ny gruppemelding (valg `6`).

**Forventet (Ole):**

- Ole skal **ikke** få meldingen (fordi membership er droppet).

---

## Testpakke 5: Lukket rom (INVITE + CHAT) via UDP unicast 50000

Privat chat er “sessions” med `session_id`. Flyt:

- Invitasjon sendes (pending hos mottaker)
- Mottaker aksepterer eller avslår
- Ved aksept blir det en aktiv session
- Chat sendes i sessionen

### Test P1: Farah inviterer Ole (Ole får pending invite)

**Steg (Farah):**

- Velg `8`
- Skriv:
  - `Ole`

**Forventet (Farah):**

- `Invitasjon sendt. Session-ID: <sid>`

**Forventet (Ole):**

- Ole får en statusmelding i konsollen (prefiks `[Privat] ...`) som indikerer at invitasjon er mottatt.
- (Hvis du ikke ser den umiddelbart: gå til test P2, den viser pending-listen eksplisitt.)

**Hva det tester:**

- `DirectMessageService::invite_user` finner IP via `UserDirectory` og sender unicast.
- Mottaker lagrer den som “pending incoming”.

### Test P2: Ole aksepterer invitasjon

**Steg (Ole):**

- Velg `9` (Svar på invitasjon)
- Du får liste `Pending invitasjoner (...)`
- Kopier session-id
- Svar:
  - `a`

**Forventet:**

- Ole får en statusmelding om at session er aktiv / akseptert.
- Farah får en statusmelding om at Ole aksepterte.

**Hva det tester:**

- Pending → Active state transition.
- Endpoint-match og validering rundt accept.

### Test P3: Send privat melding (begge veier)

**Steg (Farah):**

- Velg `10` (Send privat melding)
- Du får liste over aktive sessions
- Skriv session-id
- Skriv melding:
  - `Hei Ole, dette er privat`

**Forventet (Farah):**

- Lokal echo:
  - `[Privat <sid>] Farah: Hei Ole, dette er privat`

**Forventet (Ole):**

- Mottar:
  - `[Privat <sid>] Farah: Hei Ole, dette er privat`

**Gjenta fra Ole til Farah** og forvent samme mønster.

### Test P4: Avslå invitasjon (reject + reason)

**Oppsett:**

- Start en ny session: Farah velger `8` og inviterer Ole igjen.

**Steg (Ole):**

- Velg `9`
- Velg riktig session-id
- Svar:
  - `d`
- Skriv reason, f.eks.:
  - `Ikke nå`

**Forventet:**

- Ole får status om at den er avslått.
- Farah får status om at den ble avslått + reason (hvis implementert i status-tekst).

### Test P5: Resend/robusthet (kun hvis du kan simulere pakketap)

Denne testen er avansert og valgfri. Prosjektet har resend-logikk for utgående pending invites (for å tåle UDP-tap).

**Idé:**

- Hvis en invite “forsvinner”, vil Farah prøve å sende den på nytt i en periode.

**En enkel måte å simulere pakketap på** (Linux, krever `tc` og root) er å legge inn tap på utgående UDP, men det kan påvirke all nettverkstrafikk. Hvis du ikke er komfortabel med dette, hopp over.

---

## Testpakke 6: Samtidighet og “meldinger midt i menyen”

Målet er å kunne forklare at programmet er trådet (receiver thread + heartbeat + group advert).

### Test T1: Mottak mens du står i menyen

**Steg:**

- La Ole sende flere USN Chat-meldinger mens Farah bare står og ser på menyen uten å taste.

**Forventet:**

- Farah får meldinger printet selv om hun ikke “gjør noe” i menyen.

**Hva det tester:**

- `run_receive_multiplex_loop` kjører i egen tråd og kan skrive til stdout med `cout_mutex`.

---

## Testpakke 7: Nettverksverifisering (diagnostikk)

Dette er ikke “funksjonstester”, men verktøy du kan bruke hvis noe ikke virker.

### Test N1: Se at portene faktisk er lyttende (UDP)

Mens en klient kjører, åpne en ny terminal og kjør:

```bash
ss -lunp | grep 50000
```

**Forventet:**

- Du ser én UDP socket bundet til 50000 (prosessen kan hete `chatapp`).

### Test N2: Sniffe UDP 50000 på kommandolinjen (avansert)

På en maskin i samme nett (krever root):

```bash
sudo tcpdump -ni any udp port 50000
```

**Forventet:**

- Når noen sender USN Chat-melding, ser du UDP-pakker på port 50000.

---

## Testpakke 8: Wireshark — dokumentere meldinger og OSI-lag

Målet er å **vise faktisk nettverkstrafikk** fra chatklienten, **dokumentere innholdet** (SECP-linjer) og å **mappe feltene du ser i Wireshark til OSI-modellen** — typisk krav i rapporter om nettverk.

### Forberedelse

1. Installer Wireshark (se avsnittet *Pakker du trenger* over).
2. Velg **riktig grensesnitt**:
   - **To klienter på samme maskin:** fang på `lo` (loopback). Mye broadcast/multicast oppfører seg annerledes enn på LAN; du kan likevel ofte se **unicast** og noe lokal trafikk — test og noter hva du faktisk fanger.
   - **To maskiner på samme LAN:** fang på det aktive Ethernet- eller WiFi-grensesnittet (f.eks. `eth0`, `enp0s3`, `wlp…`).

### Test W1: Start fangst med filter på SECP-porten

**Steg:**

1. Start Wireshark.
2. Velg grensesnitt og dobbeltklikk for å starte fangst, **eller** bruk fangstfilter:
   - **Capture filter** (libpcap, før du starter): `udp port 50000`
3. Start én eller to `chatapp`-instanser og utfør en enkel handling, f.eks.:
   - **USN Chat:** valg `2` og send en kort melding (Testpakke 3).
   - **Presence:** vent noen sekunder etter oppstart (Testpakke 2 sender heartbeat).

**Forventet:**

- Du ser **UDP-datagrammer** med destinasjonsport **50000** (og ofte kildeport **50000**).
- I pakkepanelet: velg en pakke → i **Packet Details** (midten) ekspander lagene nedenfor.

### Test W2: Hva du ser i Wireshark vs. OSI-lag (dokumentasjon i rapport)

Wireshark viser protokoller som **kapslinger** av hverandre. Slik kan du forklare det i en oppgave (forenklet modell — OSI er pedagogisk; TCP/IP brukes ofte parallelt):

| OSI (pedagogisk) | Hva du typisk klikker på i Wireshark | Hva det betyr for dette prosjektet |
|------------------|----------------------------------------|-------------------------------------|
| **7 Applikasjon** | `Data` under **UDP** (rå nyttelast) | Sekvensen av bytes som er **UTF-8 tekst**: `TYPE\|ROOM\|USERNAME\|PAYLOAD\n` (SECP). Høyreklikk UDP → *Follow* → *UDP Stream* for lesbar sammenheng. |
| **4 Transport** | **UDP** | Kilde-/destinasjonsport **50000**, lengde, checksum. Her “leverer” OS UDP-nyttelasten til applikasjonen. |
| **3 Nettverk** | **Internet Protocol Version 4** | Kilde- og destinasjons-**IP** (unicast, broadcast `255.255.255.255` eller subnett-broadcast, eller multicast-adresse f.eks. **239.0.0.1** for grupperom). TTL, lengde. |
| **2 Datalink** | **Ethernet II** (eller WiFi-frame) | **MAC-adresser** (kilde/dest). EtherType `0x0800` = IPv4. |
| **1 Fysisk** | Ikke som eget lag i .pcap-filen | Mediet (kabel/WiFi) — i rapport: kort nevnt som underlag for bitene på ledningen/luften. |

**Konkrete korrelasjoner mot prosjektet:**

- **Broadcast (presence, USN Chat, `ROOM_ANNOUNCE`):** IPv4 **Destination** kan være broadcast-adressen for subnettet eller `255.255.255.255` (avhengig av OS/stack). UDP **50000**. Nyttelast: f.eks. `CHAT|USN Chat|Farah|Hei\n`.
- **Multicast (grupperom-chat):** IPv4 **Destination** **239.0.0.1**, UDP **50000**. Nyttelast: `CHAT|<romnavn>|<bruker>|<tekst>\n`.
- **Unicast (invite/privat chat):** IPv4 destination = motpartens **unicast-IP**, UDP **50000**. Nyttelast: `INVITE|...` eller `CHAT|...`.

### Test W3: Display filter og eksport (rapportvedlegg)

**Display filter** (etter at pakker er fanget inn) — eksempler:

- `udp.port == 50000` — all SECP-relatert UDP på porten.
- `ip.dst == 239.0.0.1` — multicast til prosjektets gruppeadresse.
- `udp contains "CHAT"` — tekstlig søk i nyttelast (nyttig for å plukke ut chat-linjer; ikke en full protokollvalidator).

**Dokumentasjon:**

- Ta **skjermdump** av én representativ pakke med **Packet Details** utvidet (Ethernet → IP → UDP → Data).
- Lagre fangst: **File → Save As…** (`*.pcapng`) og referer til filen i rapporten (vedlegg eller figurtekst).

### Test W4: Sammenlign med tcpdump (valgfritt)

Samme trafikk som Test N2, men åpnet i Wireshark:

```bash
sudo tcpdump -ni <grensesnitt> -w chat_secp.pcap udp port 50000
```

Avslutt med Ctrl+C, åpne `chat_secp.pcap` i Wireshark. Da kan du skrive at du har **reproduserbar** lagring av pakker utenom GUI.

**Vanlige fallgruver:**

- **Ingen pakker på loopback** for broadcast: test på ekte LAN eller beskriv begrensningen i rapporten.
- **Wireshark uten rettigheter:** bruk `sudo wireshark` eller medlemskap i `wireshark`-gruppen.
- **Feil grensesnitt:** du fanger “tomt” selv om appen sender — bytt til `lo` vs. LAN-grensesnitt.

---

## Vanlige problemer + hva det betyr (kort feilsøk)

- **Du ser ikke andre brukere i valg 1**
  - Broadcast/presence går ikke gjennom (nettverk, VM/NAT, firewall), eller du testet for tidlig.
  - Vent 5–10 sek og prøv igjen. Sjekk `ss -lunp`.

- **USN Chat-meldinger kommer ikke frem**
  - Broadcast kan være filtrert. Test med to klienter på samme maskin først.

- **Grupperom vises ikke i valg 4**
  - Annonsen sendes periodisk; vent noen sekunder.
  - Hvis du er på ulike subnett, kan broadcast/multicast være blokkert.

- **Multicast-chat fungerer ikke**
  - Nettverket/OS kan blokkere multicast. Test på samme maskin først.

- **Privat invite feiler med “ukjent bruker”**
  - 1-1 bruker `UserDirectory`; du må først ha oppdaget brukeren via discovery (valg 1 kan verifisere det).

---

## “Sjekkliste” (hurtigtest på 10 minutter)

Hvis du bare vil verifisere alt raskt:

- Bygg: `cmake -S . -B build && cmake --build build -j`
- Start Farah og Ole
- Farah: `1` → ser Ole
- Farah: `2` send USN Chat → Ole mottar
- Farah: `3` opprett gruppe → Ole: `4` ser rom → Ole: `5` join → Farah: `6` send gruppe → Ole mottar
- Farah: `8` inviter Ole → Ole: `9` aksepter → Farah: `10` send privat → Ole mottar
- Begge: `12` avslutt rent
- (Valgfritt rapport:) Testpakke 8 — Wireshark med `udp port 50000`, skjermdump av lag (Ethernet → IP → UDP → data) og ev. `.pcapng`-vedlegg


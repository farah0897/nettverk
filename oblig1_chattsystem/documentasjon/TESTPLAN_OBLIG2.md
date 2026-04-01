# Testplan oblig 2

Denne planen dekker lokal testing, flere terminaler, LAN med andre studenter, garantert åpent TCP-rom (port **50001**), sikkert rom (**50002**), invitasjoner, disconnects, ugyldig input, kryptering og regresjon på oblig 1 (UDP **50000**, multicast, lobby, privat chat).

For hver test finnes: **testnavn**, **mål**, **steg**, **kommandoer**, **forventet resultat**, **mulige feil**, **dokumentasjon i rapporten**.

**Grunnforutsetning:** Stå i prosjektmappen (der `CMakeLists.txt` ligger). Første gang eller etter endringer i kode:

```bash
cmake -S . -B build
cmake --build build
```

Kjør klienten som `./build/chatapp <brukernavn>` (bytt ut brukernavn). På Windows/WSL bruk samme sti til `build`-mappen.

---

## Rapportmal (gjenta per gjennomført test)

| Felt | Innhold |
|------|---------|
| ID + navn | f.eks. `NET-01` |
| Miljø | OS, antall maskiner, testbrukernavn |
| Kommandoer | kort: f.eks. `./build/chatapp Farah` + relevante menyvalg (kopier fra testens **Kommandoer**-rad) |
| Utført | dato |
| Resultat | bestått / delvis / feilet |
| Bevis | skjermdump, logg, PCAP-beskrivelse (ingen PSK/hemmeligheter) |
| Avvik | hva som gikk galt og antatt årsak |

---

## A. Lokal testing på én maskin

### LOC-01 Én prosess, meny og porter

| | |
|--|--|
| **Mål** | Verifisere at klienten starter, lytter på forventede porter, og at meny ikke henger. |
| **Steg** | Bygg og kjør `chatapp`; logg inn med brukernavn; åpne meny flere ganger; velg status (meny **20**) uten å ha opprettet rom. |
| **Kommandoer** | `cmake --build build` · `./build/chatapp Farah` - i meny: tast `20` + Enter. Valgfritt i annen terminal: `ss -tulpn \| grep -E '50000\|50001\|50002'`. |
| **Forventet resultat** | Programmet svarer; status viser ingen aktiv TCP-sesjon; ingen crash. |
| **Mulige feil** | Port allerede i bruk; stdin lukket → avslutt. |
| **Rapport** | Skjermbilde av første meny + status; evt. `ss -tulpn \| grep -E '50000\|50001\|50002'` før/etter start. |

### LOC-02 Vert og klient på samme maskin (to instanser)

| | |
|--|--|
| **Mål** | Garantert åpent rom vert↔klient lokalt (loopback). |
| **Steg** | Terminal 1: vert (meny **12**), romnavn f.eks. `test-lokal`. Terminal 2: annen bruker, **13** for liste, **14** join med samme navn. **18** send melding begge veier. |
| **Kommandoer** | T1: `./build/chatapp Vert` → meny **12** (rom `test-lokal`). T2: `./build/chatapp Klient` → **13**, **14** (`test-lokal`), **18** (chat). T1: **18**. Avslutt med **23** eller Ctrl+C. |
| **Forventet resultat** | Begge ser chat; vert ser klient tilkoblet; ingen duplikatport-feil. |
| **Mulige feil** | Klient finner ikke annonse (broadcast/timing); join feiler. |
| **Rapport** | Tidsstempel + kort logg (hvem sendte hva); om annonse kom fra samme host. |

---

## B. Flere terminaler

### MT-01 Tre terminaler: lobby + to chattere

| | |
|--|--|
| **Mål** | Flere prosesser uten at stdout blir uleselig (trådsikker utskrift). |
| **Steg** | T1: bruker A, meny **2** lobby. T2: bruker B, **2**. T3: A og B veksler på multicast (**3-6**) eller garantert TCP (**12-14**, **18**). |
| **Kommandoer** | T1: `./build/chatapp Farah` → **2**. T2: `./build/chatapp Bob` → **2**. T3 (samme som T1 eller T2): følg meny **3-6** eller **12-14**/**18** som beskrevet i steg. |
| **Forventet resultat** | Meldinger er lesbare; eventuelt noe interleaving er OK hvis dokumentert. |
| **Mulige feil** | Deadlock ved input; manglende flush. |
| **Rapport** | Kort beskrivelse + utklipp som viser at begge fortsatt kan bruke meny etter nettverksaktivitet. |

### MT-02 Parallell: garantert TCP og sikkert rom

| | |
|--|--|
| **Mål** | To uavhengige funksjoner samtidig (50001 vs 50002). |
| **Steg** | T1/T2: garantert rom som i LOC-02. T3/T4: annet brukerpar, sikkert rom (**15-17**, **19**). |
| **Kommandoer** | T1: `./build/chatapp VertG` → **12**. T2: `./build/chatapp KlientG` → **13**/**14**/**18**. T3: `./build/chatapp VertS` → **15** (inviter T4). T4: `./build/chatapp KlientS` → **1** (vent katalog) → **17** (aksept) → **19**. |
| **Forventet resultat** | Ingen krysskobling av meldinger mellom modusene. |
| **Mulige feil** | Feil port i konfigurasjon; global tilstand blandes. |
| **Rapport** | Tabell: terminal → modus → observert oppførsel. |

---

## C. Testing med andre studenter på nettverk

### NET-01 Kryss-PC: garantert rom

| | |
|--|--|
| **Mål** | UDP-annonsering og TCP mellom forskjellige IP-er. |
| **Steg** | Avtal VLAN/subnett. Vert på PC-A (**12**). PC-B: **13**, verifiser at A’s rom vises; **14** join; **18** chat. |
| **Kommandoer** | På PC-A (fra klonet repo): `cmake --build build && ./build/chatapp VertLan` → **12**. På PC-B: `./build/chatapp KlientLan` → **13**/**14**/**18** (samme prosedyre som LOC-02). Kjør fra hver maskins kopi av prosjektet. |
| **Forventet resultat** | Rom synlig; TCP til riktig IP:50001; chat fungerer. |
| **Mulige feil** | Brannmur; ikke samme broadcast-domene; NAT (sjelden på lab-LAN). |
| **Rapport** | IP-adresser (anonymiser ved behov), skjermdump av liste og én meldingslinje. |

### NET-02 Kryss-PC: sikkert rom

| | |
|--|--|
| **Mål** | Full sikker flyt over LAN. |
| **Steg** | A **15** inviterer B (brukernavn fra katalog). B **17** aksepterer med session-id. Begge **19**. |
| **Kommandoer** | PC-A: `./build/chatapp Farah` → **15** (inviter `Bob`). PC-B: `./build/chatapp Bob` → **1** (bekreft Farah) → **17** (session-id, `a`) → **19**. PC-A: **19**. Valgfritt: **20** på begge. |
| **Forventet resultat** | Dekryptert chat i UI; fase riktig i **20**. |
| **Mulige feil** | Bruker ikke i katalog; TCP blokkert til 50002. |
| **Rapport** | Rekkefølge INVITE → ACCEPT → CHAT; ikke lim inn PSK i klartekst. |

---

## D. Åpent garantert rom

### GAR-01 Vert annonserer og flere ser listen

| | |
|--|--|
| **Mål** | Annonserte TCP-rom stemmer med det som vises i meny **13**. |
| **Steg** | Vert **12**; to klienter kjører **13** innen rimelig tid. |
| **Kommandoer** | T1: `./build/chatapp Vert` → **12**. T2: `./build/chatapp Klient1` → **13**. T3: `./build/chatapp Klient2` → **13**. |
| **Forventet resultat** | Rom med vert, IP og port vises konsistent. |
| **Mulige feil** | Utdatert annonse; duplikater etter omstart. |
| **Rapport** | Før/etter-utskrift av meny **13**. |

### GAR-02 Vert forlater / stopper hosting

| | |
|--|--|
| **Mål** | **22** (forlat åpent garantert TCP-rom / vert) oppfører seg forutsigbart. |
| **Steg** | Klient koblet; vert velger **22**. Prøv **18** fra klient. |
| **Kommandoer** | Start som LOC-02 (vert **12**, klient **14**). På vert-terminal: **22**. På klient: **18** (forvent feil). |
| **Forventet resultat** | Tilkobling brutt; sending feiler kontrollert eller tydelig melding om inaktiv sesjon. |
| **Mulige feil** | Hengende klient; crash i stedet for feilmelding. |
| **Rapport** | Hva programmet skrev etter disconnect. |

---

## E. Sikkert rom

### SEC-01 Faser og status (meny 20)

| | |
|--|--|
| **Mål** | `SecureRoomPhase` / status er forståelig underveis. |
| **Steg** | Gå idle → invitasjon sendt → (motpart pending) → aktiv chat; åpne **20** underveis. |
| **Kommandoer** | To instanser: `./build/chatapp Vert` + `./build/chatapp Klient`. Følg **15**/**17**/**19** som i NET-02; mellom hvert steg: tast **20** + Enter på begge. |
| **Forventet resultat** | Status tekst matcher tilstand (vert vs. invitert). |
| **Mulige feil** | Fase «står fast» etter feil. |
| **Rapport** | Liste med fase før/etter hvert steg. |

### SEC-02 Avslå invitasjon

| | |
|--|--|
| **Mål** | `decline` rydder tilstand. |
| **Steg** | A **15**; B **17** velger **d** for session-id. A prøver **19**. |
| **Kommandoer** | A: `./build/chatapp Vert` → **15** (målbruker `Bob`). B: `./build/chatapp Bob` → **17** → skriv session-id → `d`. A: **19** (skal feile / ikke aktiv chat). |
| **Forventet resultat** | Ingen aktiv sikker chat; A kan ikke sende til avslått sesjon på en meningsfull måte. |
| **Mulige feil** | Pending state gjenstår; vert henger i lytte-modus. |
| **Rapport** | Observasjon + evt. logglinje. |

---

## F. Invitasjoner

### INV-01 UDP privat (oblig 1) vs. sikker INVITE (oblig 2)

| | |
|--|--|
| **Mål** | Skille mellom oblig 1 (**8-9**) og sikker (**15-17**). |
| **Steg** | Utfør begge flyter med samme par brukernavn i klar rekkefølge; noter menypunkt. |
| **Kommandoer** | **Først oblig 1:** A: `./build/chatapp Farah` → **8** (inviter `Bob`). B: `./build/chatapp Bob` → **9**. **Deretter sikker (ny sesjon / avslutt gammel):** A: **15** mot `Bob`; B: **17**/**19**. Bruk Wireshark med `udp.port == 50000 && frame contains "INVITE"` for å sammenligne. |
| **Forventet resultat** | Riktig protokoll for hver (UDP DM vs. sikker TCP-strøm). |
| **Mulige feil** | Forveksling av session-id mellom tjenester. |
| **Rapport** | Tabell: menypunkt → protokoll → resultat. |

### INV-02 Flere ventende sikre invitasjoner

| | |
|--|--|
| **Mål** | Liste i **17** håndterer flere pending (dersom designet tillater det). |
| **Steg** | To verter inviterer samme bruker (hvis mulig); B åpner **17**. |
| **Kommandoer** | T1: `./build/chatapp Vert1` → **15** (`Bob`). T2: `./build/chatapp Vert2` → **15** (`Bob`). T3: `./build/chatapp Bob` → **17** (se liste). |
| **Forventet resultat** | Liste viser begge; aksept kun for valgt session-id. |
| **Mulige feil** | Kun siste synlig; overskriving. |
| **Rapport** | Skjermdump av listen (anonymisert). |

---

## G. Disconnects

### DISC-01 Klient lukker under garantert TCP-chat

| | |
|--|--|
| **Mål** | Vert tolererer bortfall av klient. |
| **Steg** | Klient `Ctrl+C` eller avslutt prosess under aktiv **18**; vert sender melding etterpå. |
| **Kommandoer** | T1: `./build/chatapp Vert` → **12**. T2: `./build/chatapp Klient` → **14**. Når koblet: T2: **18**, deretter **Ctrl+C** på T2. T1: **18** igjen. |
| **Forventet resultat** | Vert crasher ikke; evt. melding om at peer er borte. |
| **Mulige feil** | Deadlock; vert kan ikke ta ny klient uten omstart. |
| **Rapport** | Oppførsel etter disconnect; om **22**/ny **12** trengs. |

### DISC-02 Vert lukker under sikker sesjon

| | |
|--|--|
| **Mål** | Klient oppdager lukket TCP. |
| **Steg** | Aktiv **19**; vert avslutter (**21** eller avslutt prosess). Klient prøver **19**. |
| **Kommandoer** | Etabler sikker sesjon (NET-02-flyt). På vert: **21** *eller* `Ctrl+C` på `./build/chatapp Vert`. På klient: **19**. |
| **Forventet resultat** | Feilmelding eller ren disconnect; fase tilbake til idle. |
| **Mulige feil** | Uendelig venting; heng i `send`. |
| **Rapport** | Hva som skjedde på klient-side. |

### DISC-03 Nettverksbrudd (valgfritt)

| | |
|--|--|
| **Mål** | Reell disconnect uten kontrollert avslutning. |
| **Steg** | Aktiv sesjon; deaktiver interface kortvarig på én side. |
| **Kommandoer** | Start `./build/chatapp …` på to maskiner som i NET-02. På Linux (eksempel): `sudo ip link set <iface> down` / `up` - **advarsel:** kutter all trafikk på grensesnittet; bruk test-VM eller eget grensesnitt. Alternativt: koble ut kabel kortvarig. |
| **Forventet resultat** | Timeout eller broken pipe; programmet kan brukes videre etter gjenoppretting (evt. ny sesjon). |
| **Mulige feil** | Må restarte alt. |
| **Rapport** | Kort notat; full repro ikke nødvendig hvis ustabilt. |

---

## H. Ugyldig input

### INP-01 Menyvalg utenfor område og ikke-numerisk

| | |
|--|--|
| **Mål** | Robust parsing av meny (1-23). |
| **Steg** | Skriv `abc`, `0`, `99`, tom linje, mellomrom + gyldig tall. |
| **Kommandoer** | `./build/chatapp Test` - ved meny, tast inn `abc`, Enter; `0`, Enter; `99`, Enter; Enter (tom); ` 12`, Enter - observer svar. |
| **Forventet resultat** | Tydelig feilmelding; løkke fortsetter uten crash. |
| **Mulige feil** | Uendelig løkke; feil tall aksepteres. |
| **Rapport** | Tabell: input → programsvar. |

### INP-02 Romnavn/brukernavn: `|`, for lang streng, tom streng

| | |
|--|--|
| **Mål** | Validering ved opprettelse/join/invitasjon. |
| **Steg** | Ved **12**, **14**, **15**: gi ugyldige strenger der det passer. |
| **Kommandoer** | `./build/chatapp Test` → **12** (rom `a|b`), **14** (ugyldig), **15** (brukernavn med `|`). |
| **Forventet resultat** | Avvisning med forklaring; ingen korrupt tilstand. |
| **Mulige feil** | Sender likevel på nettverket. |
| **Rapport** | Eksempler på avviste strenger (uten sensitive data). |

### INP-03 Session-id ved sikker INVITE (meny 17)

| | |
|--|--|
| **Mål** | Kun gyldig heks der det kreves. |
| **Steg** | Ugyldig tegn, for lang id, tom der id kreves. |
| **Kommandoer** | `./build/chatapp Bob` med ventende invitasjon (etter **15** fra annen prosess) → **17** → skriv `gggggg` (ugyldig heks), **ZZZZZZ** (ugyldig), Enter tom på session-id. |
| **Forventet resultat** | Avvisning før meningsløst TCP-forsøk der det er meningen. |
| **Mulige feil** | Aksepterer og feiler senere med uklar melding. |
| **Rapport** | Kort punktliste. |

---

## I. Kryptering

### CRY-01 Wireshark / tcpdump på sikker port (50002)

| | |
|--|--|
| **Mål** | Nyttelast på **50002** er ikke lesbar som klartekst-chat. |
| **Steg** | Fangst på interface under **19**; send kjent streng f.eks. `TESTPLAN_OBLIG2`. |
| **Kommandoer** | Terminal 1: `wireshark` (eller `sudo wireshark`) - velg riktig interface, start fangst. Terminal 2/3: `./build/chatapp …` - etabler sikker sesjon, **19**, send `TESTPLAN_OBLIG2`. CLI: `sudo tcpdump -i <iface> -w oblig2.pcap 'tcp port 50002'` (valgfritt lagre uten GUI). |
| **Forventet resultat** | I PCAP: ingen full klartekst av den kjente strengen i relevant TCP payload. |
| **Mulige feil** | Tekst synlig i klartekst → sjekk at krypto brukes; feil port fanget. |
| **Rapport** | Wireshark-filter `tcp.port == 50002`; søk etter streng gir 0 treff i applikasjonsdata. **Ikke** vedlegg PSK. |

### CRY-02 Sammenlign garantert TCP (50001) med sikker (50002)

| | |
|--|--|
| **Mål** | Åpent rom har lesbar protokolllinje; sikker ikke samme klartekst i payload. |
| **Steg** | Samme melding på **18** vs **19** med fangst. |
| **Kommandoer** | Kjør én LOC-02-test (50001) og én sikker sesjon (50002) med samme tekst i chat; i Wireshark: `tcp.port == 50001 or tcp.port == 50002` - **Follow TCP Stream** på hver port. |
| **Forventet resultat** | Pedagogisk forskjell synlig i analyse. |
| **Mulige feil** | Feil port i sammenligning. |
| **Rapport** | Én setning + evt. kort hex-utdrag (maskert). |

---

## J. Oblig 1 - regresjon

### O1-01 Multicast: 3-7

| | |
|--|--|
| **Mål** | Meny **3-7** fungerer som etter oblig 1. |
| **Steg** | To brukere på LAN; opprett, list, join, chat, **7** forlat multicast. |
| **Kommandoer** | T1: `./build/chatapp Farah` → **3** (rom `gruppe1`) → **6**. T2: `./build/chatapp Bob` → **4**/**5** (`gruppe1`) → **6**. T1 eller T2: **7**. |
| **Forventet resultat** | Gruppechat som før; **7** forlater kun UDP-multicast (jf. nåværende meny). |
| **Mulige feil** | Multicast ikke rutet på tvers av VLAN. |
| **Rapport** | Sjekkliste med avhuking. |

### O1-02 Lobby og katalog (1-2)

| | |
|--|--|
| **Mål** | Etter TCP-tester fungerer fortsatt discovery og lobby. |
| **Steg** | Etter GAR/SEC-tester: **1**, **2**. |
| **Kommandoer** | `./build/chatapp Farah` → **1**; **2** + skriv melding. (Kjør `./build/chatapp Bob` på annen maskin/terminal for å se lobby-melding.) |
| **Forventet resultat** | Peers synes; lobby-melding vises. |
| **Mulige feil** | UDP blokkert (sjelden). |
| **Rapport** | Avsnitt «regresjonstesting». |

### O1-03 Privat UDP-rom (8-11)

| | |
|--|--|
| **Mål** | Ingen regresjon fra oblig 2-endringer. |
| **Steg** | Invitasjon, aksept, privat melding, liste aktive. |
| **Kommandoer** | A: `./build/chatapp Farah` → **8** (`Bob`). B: `./build/chatapp Bob` → **9** (aksepter). A: **10** (send chat). **11** på begge. |
| **Forventet resultat** | Som oblig 1-beskrivelse. |
| **Mulige feil** | Session-ID forveksles med sikker rom - unngå ved klar rekkefølge og notater. |
| **Rapport** | «Gjentatt etter oblig 2» med referanse til oblig 1-test. |

---

## Wireshark - praktisk studentguide (oblig 2)

Denne guiden er tilpasset **chatapp**-prosjektet: all SECP-trafikk på **UDP 50000**, garantert åpent rom på **TCP 50001**, sikkert rom på **TCP 50002**. SECP-linjer har formen `TYPE|ROOM|USERNAME|PAYLOAD` avsluttet med linjeskift (`\n`).

### 1. Nyttige filtre i Wireshark

Start med riktig **fangstgrensesnitt** (Ethernet eller Wi‑Fi på lab-LAN). Bruk **fangstfilter** bare om du vil begrense størrelsen *før* lagring (fangstfilter er ikke det samme som visningsfilter).

**Visningsfilter** (skriv i feltet øverst - trykk Enter):

| Hva du vil se | Filtereksempel |
|---------------|----------------|
| All trafikk til/fra chat-UDP | `udp.port == 50000` |
| Garantert åpent TCP-rom | `tcp.port == 50001` |
| Sikkert rom (krypto på TCP) | `tcp.port == 50002` |
| Både UDP-chat og én TCP-port | `udp.port == 50000 or tcp.port == 50001` |
| Kun oppkobling (SYN/FIN/RST) | `tcp.port == 50001 && (tcp.flags.syn == 1 \|\| tcp.flags.fin == 1 \|\| tcp.flags.reset == 1)` |
| Pakker som *inneholder* en tekst | `frame contains "PRESENCE"` eller `frame contains "ROOM_ANNOUNCE"` |
| Mer presist SECP-type (UDP) | `udp.port == 50000 && frame contains "PRESENCE"` |

**Tips:** `tcp.port == 50001` fanger **både** kilde- og destinasjonsport når én av dem er 50001. På samme maskin (localhost) kan du bruke `udp.port == 50000 && ip.addr == 127.0.0.1` for å roe ned støy.

**Unngå:** Filtre som er for brede (`ip.addr == …` uten port) når du skal dokumentere oblig - da blir skjermbildet uleselig.

---

### 2. Skille UDP- og TCP-trafikk

| I Wireshark | Hva du ser |
|-------------|------------|
| **Protokoll-kolonnen** | `UDP` vs `TCP` for hver rad. |
| **Farge** | Innstillinger → **Coloring Rules**: f.eks. grønn for `udp`, blå for `tcp` (valgfritt). |
| **Filter** | `udp` eller `tcp` alene; eller `udp.port == 50000` / `tcp.port == 50001` / `tcp.port == 50002`. |
| **Statistikk** | **Statistics → Protocol Hierarchy** - andel UDP/TCP. |
| **Pakke-detaljer** | Utvid **User Datagram Protocol** (UDP) eller **Transmission Control Protocol** (TCP) i trevisningen. |

**Minneknagg:** I dette prosjektet er **signaling og multicast** nesten alltid **UDP 50000**. **Chat på «garantert åpent rom»** er **TCP 50001** (newline-SECP i klartekst). **Sikkert rom** er **TCP 50002** med nyttelast som *ikke* er lesbar SECP i klartekst.

---

### 3. Hvordan finne konkrete meldingstyper og TCP-faser

#### 3.1 `PRESENCE` (discovery)

- **Transport:** UDP broadcast til port **50000** (fra din maskin med jevne mellomrom).
- **Filter:** `udp.port == 50000 && frame contains "PRESENCE"`
- **I pakke:** Velg en UDP-pakke → **Data** / **UDP payload** → vis som «ASCII» eller «Raw». Du skal se en linje som starter med `PRESENCE|…`.
- **Rapport:** Én pakke uthevet + tekst som forklarer at dette er brukerregistrering i LAN.

#### 3.2 `ROOM_ANNOUNCE`

Det finnes **to vanlige varianter** i kodebasen:

| Variant | Transport | Kjennetegn i payload |
|---------|-----------|----------------------|
| **Multicast (oblig 1)** | UDP (multicast-adresse) | `ROOM_ANNOUNCE|romnavn|eier|OPEN` |
| **Garantert TCP (oblig 2)** | UDP **50000** broadcast | `ROOM_ANNOUNCE|romnavn|vert|OPEN;tcp=50001` (payload `OPEN;tcp=` + port - se `GuaranteedOpenRoomService`) |

- **Filter:** `frame contains "ROOM_ANNOUNCE"` (evt. kombiner med `udp.port == 50000`).
- **Rapport:** Forklar kort om pakken annonserer **UDP-gruppe** vs **TCP-vert** (avhengig av hva du testet).

#### 3.3 `INVITE`

- **Oblig 1 - lukket UDP-rom:** `INVITE|…` unicast UDP til motpart, payload **CLOSED**;**to=**… - skiller seg fra sikker INVITE.
- **Oblig 2 - sikkert rom:** SECP `INVITE` på UDP med `secure_tcp=1` i payload (se `secure_invite.hpp`).

- **Filter:** `udp.port == 50000 && frame contains "INVITE"`  
  For å sammenligne: `frame contains "secure_tcp"` vs `frame contains "CLOSED"`.

#### 3.4 `CHAT`

- **Lobby (USN Chat):** `CHAT` på UDP 50000 til «USN Chat»-rom (se faktisk pakkeinnhold).
- **Multicast-gruppe:** CHAT på multicast-UDP etter at du er med i gruppe.
- **Garantert TCP:** `CHAT|…` som **linjer på TCP-strøm 50001** (se §3.6).
- **Sikkert rom:** Første applikasjonsmelding etter tilkobling er logisk SECP `CHAT|…`, men **inne i kryptert blob** på 50002 - ikke forvent klartekst i Wireshark på denne porten.

#### 3.5 TCP-oppkobling (three-way handshake)

- **Filter:** `tcp.port == 50001 && tcp.flags.syn == 1` (eller `50002` for sikkert rom).
- **Se rekkefølgen:** Klient → **SYN** → vert **SYN, ACK** → klient **ACK**. Etter det følger **PSH, ACK** med data.
- **Verktøy:** Høyreklikk en TCP-pakke → **Follow → TCP Stream** for å se hele samtalen som én strøm.

#### 3.6 TCP-dataoverføring (klartekst vs krypto)

- **50001 - garantert åpent rom:** **Follow TCP Stream** viser lesbare linjer med `CHAT|room|bruker|tekst` (evt. fragmentert over flere pakker - fortsatt ofte lesbart i strømvisningen).
- **50002 - sikkert rom:** Strømmen viser **lengdeprefiks + binær data** (ChaCha20/HMAC). Ikke forvent å lese meldingsteksten i klartekst her.

---

### 4. Dokumentere OSI-lagene (praktisk for rapport)

Wireshark viser ikke «OSI» eksplisitt, men du kan knytte **lag** til **felt** i pakkevisningen:

| OSI (forenklet) | Hva du tar skjermbilde av i Wireshark |
|-----------------|----------------------------------------|
| **7 - Applikasjon** | Innhold i UDP-payload eller TCP «Application Data» (SECP-tekst der det er klartekst). |
| **4 - Transport** | UDP-header (porter 50000) eller TCP-header (50001/50002, sekvensnr, ACK, flagg). |
| **3 - Nettverk** | **IPv4**: kilde-/destinasjons-IP. |
| **2 - Datalink** | **Ethernet II**: MAC-adresser (valgfritt i rapport hvis faget krever det). |

**Rapport-setning du kan bruke:** *«Figur X viser den samme SECP-meldingen på applikasjonslaget (UDP payload), med IPv4-adressering på nettverkslaget og UDP-port 50000 på transportlaget.»*

---

### 5. Hvilke skjermbilder du bør ta til rapporten

| # | Innhold | Hvorfor |
|---|---------|---------|
| 1 | **Fangstliste** med filter `udp.port == 50000` og en `PRESENCE`-pakke valgt | Viser discovery på oblig-porten. |
| 2 | **UDP-payload** (utvidet) for `ROOM_ANNOUNCE` eller `INVITE` relevant for din test | Dokumenterer SECP-format. |
| 3 | **TCP-handshake** på `tcp.port == 50001` (SYN/ACK-sekvens) | Viser oppkobling til garantert rom. |
| 4 | **Follow TCP Stream** for **50001** med synlig `CHAT|…` | Bevis for ukryptert applikasjonsdata på TCP. |
| 5 | **Follow TCP Stream** for **50002** under aktiv meny **19** | Bevis for *ikke*-lesbar nyttelast (krypto). |
| 6 | (Valgfritt) **Statistics → Conversations** (TCP) | Viser antall sesjoner på 50001 og 50002. |

**Personvern:** Beskær eller anonymiser MAC/IP hvis læreren ber om det. **Ikke** ta skjermbilder av UDP-payload som inneholder **hele PSK/nøkkel** i klartekst - beskriv heller at nøkkel *kan* observeres på UDP (pedagogisk svakhet) uten å gjengi den.

---

### 6. Forskjell mellom oblig 1- og oblig 2-trafikk (kort for rapport)

| Område | Oblig 1 (typisk) | Oblig 2 (typisk) |
|--------|------------------|------------------|
| **Hovedtransport** | UDP 50000 (broadcast/multicast/unicast), **ingen** TCP for multicast-gruppe | Samme UDP 50000 for signaling **+** **TCP 50001** (garantert) eller **TCP 50002** (sikkert) |
| **Rom-annonsering** | `ROOM_ANNOUNCE` … **OPEN**, multicast | `ROOM_ANNOUNCE` for TCP-rom, fortsatt på UDP 50000 |
| **Gruppechat** | Multicast UDP etter join | **TCP 50001** med SECP-linjer i klartekst |
| **Lukket 1-1** | UDP `INVITE` med **CLOSED** | **Sikker:** UDP `INVITE` med **secure_tcp=1**, deretter krypto på **50002** |
| **Wireshark «gulltest»** | Ser du `CHAT|…` i multicast/UDP? | På **50001** ser du klartekst; på **50002** ikke |

---

### 7. Hvordan kryptert trafikk (sikkert rom) *bør* se ut sammenlignet med ukryptert

| Observasjon | Garantert TCP (**50001**) | Sikkert rom (**50002**) |
|-------------|---------------------------|-------------------------|
| **Follow TCP Stream** | Lesbar SECP (`CHAT|…`) og vanlig tekst | **Binær** / tilfeldig byte-sekvens; ingen full meldingstekst |
| **Pakkestørrelse** | Ofte liten, linjeorientert | Ofte **4-byte lengde** + blob (avhengig av implementasjon) |
| **Søk i Wireshark** `frame contains "Hei"` på **50001** | Kan treffe hvis du sendte «Hei» | **Skal ikke** treffe i applikasjonsdata etter kryptering |
| **UDP før TCP** | INVITE kan inneholde nøkkel i klartekst (øvingsmessig) | Samme - **ikke** lim inn hele nøkkel i rapport |

**Konklusjon til rapport:** *«På TCP 50001 er applikasjonsnyttelasten lesbar SECP. På TCP 50002 er samme logiske melding innkapslet slik at Wireshark ikke viser klartekst, i tråd med ChaCha20+HMAC-laget i programmet.»*

---

## Kobling krav → tester

| Krav (fra oppgave) | Relevante test-ID-er |
|--------------------|----------------------|
| Lokal én maskin | LOC-01, LOC-02 |
| Flere terminaler | MT-01, MT-02 |
| Andre på nettverk | NET-01, NET-02 |
| Åpent garantert rom | GAR-01, GAR-02, LOC-02, NET-01 |
| Sikkert rom | SEC-01, SEC-02, NET-02 |
| Invitasjoner | INV-01, INV-02 |
| Disconnects | DISC-01-03 |
| Ugyldig input | INP-01-03 |
| Kryptering | CRY-01, CRY-02 |
| Oblig 1 fungerer | O1-01-03 |

---

*Sist oppdatert: kan justeres når oblig 2-krav endres.*

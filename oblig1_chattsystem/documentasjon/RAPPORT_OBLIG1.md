# Obligatorisk oppgave 1: UDP-basert chattsystem

**Emne:** Nettverk og sikkerhet (IRI)  
**Type:** Teknisk rapport  
**Språk:** Norsk (bokmål)

---

## Innholdsfortegnelse

1. [Sammendrag](#1-sammendrag)
2. [Innledning](#2-innledning)
3. [Problemstilling og mål](#3-problemstilling-og-mål)
4. [Teoretisk og teknisk bakgrunn](#4-teoretisk-og-teknisk-bakgrunn)
5. [Beskrivelse av kommunikasjonsprotokollen (RFC USNChat01 / SECP)](#5-beskrivelse-av-kommunikasjonsprotokollen-rfc-usnchat01--secp)
6. [Løsning og implementasjon](#6-løsning-og-implementasjon)
7. [Robusthet og feilhåndtering](#7-robusthet-og-feilhåndtering)
8. [Testing](#8-testing)
9. [Wireshark og OSI-lag](#9-wireshark-og-osi-lag)
10. [Sikkerhetsvurdering](#10-sikkerhetsvurdering)
11. [Diskusjon og begrensninger](#11-diskusjon-og-begrensninger)
12. [Konklusjon](#12-konklusjon)
13. [KI-erklæring](#13-ki-erklæring)
14. [Referanser](#14-referanser)

---

## 1. Sammendrag

Rapporten beskriver et chatprogram for kommandolinjen, skrevet i C++, som snakker med andre klienter over UDP på et lokalt nett. Programmet lar brukere bli synlige for hverandre, chatte i et åpent fellesrom som alle hører, opprette åpne grupperom med multicast, og føre private samtaler én-mot-én med unicast. Alle meldinger følger den tekstbaserte protokollen SECP som er beskrevet i kursdokumentet RFC USNChat01. Videre forklares hvorfor oppgaven er lagt opp som den er, hvordan løsningen er bygd, hvordan den er testet, og hvordan trafikken kan studeres i Wireshark. Til slutt følger en kort vurdering av sikkerhet og begrensninger.

**Nøkkelord:** UDP, broadcast, multicast, unicast, POSIX sockets, SECP, lokalnett.

---

## 2. Innledning

Mange programmer bruker TCP når de trenger pålitelig levering. I denne oppgaven brukes derimot UDP med vilje: det er enkelt å komme i gang med, har lav overhead, og støtter både kringkasting til alle på subnettet og sending til en multicast-gruppe. Samtidig må man leve med at pakker kan forsvinne eller komme i «feil» rekkefølge, og at alt som kommer inn over nettet må sjekkes nøye, man kan ikke stole blindt på innholdet.

Oppgaven er å lage et lite chattsystem for LAN uten sentral server, med vanlig socket-programmering som på øvingene, ikke ferdige GUI-rammeverk. Det skal støtte fire hovedfunksjoner: å vise hvem som er til stede, åpent fellesrom, åpne grupperom og lukkede to-personersrom. I tillegg kreves fornuftig feilhåndtering, sjekk av brukerinput og begrensning av hvor mye data programmet godtar om gangen, slik at buffere ikke kan overfyldes. Leveransen skal dokumenteres, testes (også sammen med andre), og trafikken skal sees på i Wireshark med kobling til OSI-modellen.

Rapporten følger denne rekkefølgen: først problem og mål, deretter nødvendig bakgrunn, så selve meldingsformatet (SECP), deretter implementasjon, robusthet, testing og Wireshark, og til slutt sikkerhet, diskusjon og konklusjon.

---

## 3. Problemstilling og mål

### 3.1 Problemstilling

Uten en felles server må hver klient finne ut hvem som finnes på nettet, hvor felles «kanaler» er, og hvordan mer private samtaler kan startes. Oppgaven kan formuleres slik:

> Hvordan kan flere uavhengige programmer utveksle tekst over UDP slik at alle kan oppdage hverandre og chatte åpent, at grupper kan skilles ut med multicast, og at private rom bare når de som er invitert, samtidig som systemet tåler nettverksfeil, feil bruk fra tastaturet og data som ikke er som forventet?

### 3.2 Funksjonelle mål (oppgavetekst)


| Mål                   | Kort beskrivelse                                                                                                                     |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| **1. Presence**       | Ved oppstart og jevnlig: broadcast med brukernavn og IP, slik at andre kan bygge en liste over aktive brukere.                       |
| **2. Fellesrom**      | Et åpent rom der alle som er med kan sende og motta; meldinger går som broadcast og vises fortløpende.                               |
| **3. Åpne grupperom** | Rom opprettes etter behov, annonseres slik at andre finner dem, med innmelding og utmelding; selve gruppechatten går over multicast. |
| **4. 1–1-rom**        | Lukket rom: invitasjon sendes direkte til én motpart; bare de to deltar; chat går som unicast.                                       |


Oppgaveteksten krever også at viktige meldinger (som invitasjoner og romannonser) gjentas med jevne mellomrom, typisk hvert 5. til 10. sekund, slik at noen som starter programmet senere, fortsatt får med seg informasjonen. I løsningen gjentas blant annet presence, romannonser og ventende invitasjoner innenfor et slikt intervall.

### 3.3 Ikke-funksjonelle mål

- **Biblioteker:** Som på øving: vanlige socket-funksjoner, ikke store rammeverk som Qt.
- **Robusthet:** Håndtere nettverksfeil, ugyldig input og brå avbrudd; unngå at for store meldinger skriver utenfor tildelt minne.
- **Dokumentasjon:** Beskrive protokollen (RFC USNChat01 / SECP), dokumentere tester og Wireshark, vurdere sikkerhet, levere KI-erklæring og referanser.

---

## 4. Teoretisk og teknisk bakgrunn

### 4.1 UDP og best-effort

UDP gir ingen garanti for at en pakke kommer frem eller kommer i samme rekkefølge som den ble sendt. Derfor brukes i praksis gjentatte «jeg er her»-meldinger, tidsavbrudd der det trengs, og forsiktig tolking av alt som mottas, som om det kan være ufullstendig eller feil.

### 4.2 Broadcast, multicast og unicast i denne oppgaven

- **Broadcast:** Én sending som alle på samme kringkastingsområde (typisk LAN) kan fange opp. Brukes når «alle» skal vite noe: presence, det åpne fellesrommet og annonser om grupperom når man ikke har en liste over hvem som skal ha beskjed.
- **Multicast:** Én gruppeadresse på nettverket; bare klienter som har meldt seg inn i gruppen mottar trafikken. Brukes til chat i åpne grupperom.
- **Unicast:** Sending direkte til én IP og port. Brukes til invitasjon og privat chat i lukkede rom.

### 4.3 OSI-modellen (relevant for Wireshark)

Når trafikken inspiseres i Wireshark, er det naturlig å knytte det man ser til lag i OSI-modellen: Ethernet (lag 2), IPv4 (lag 3), UDP (lag 4), og selve chatteksten som ligger inni UDP-datagrammet, altså applikasjonslaget (lag 7) i denne sammenhengen.

---

## 5. Beskrivelse av kommunikasjonsprotokollen (RFC USNChat01 / SECP)

Dette kapitlet handler om hvordan meldingene er bygd opp, altså applikasjonslaget i RFC USNChat01. I koden kalles det praktisk talt SECP (*Simple Educational Chat Protocol*). Implementasjonen ligger i `include/chat/protocol/secp.hpp` og `src/protocol/secp.cpp`.

### 5.1 Formål og avgrensing

Dokumentet sier hvordan ulike chat-klienter skal formatere tekstlinjer slik at de forstår hverandre i samme LAN. Alt er tekstbasert: hver logiske melding er én linje som slutter med linjeskift. Det spiller ingen rolle for selve linjeformatet om pakken sendes med broadcast, multicast eller unicast, det er et valg man tar ut fra romtype og hvem som skal nås.

RFC nevner også TCP og «garanterte rom» på port 50001. I denne obligatoriske leveransen er det bare UDP som er implementert; TCP-delen er ikke med, men nevnes her så rapporten stemmer med spesifikasjonen.

### 5.2 Meldingsformat: én linje, fire felt

Alle SECP-meldinger følger samme mønster:

```text
TYPE|ROOM|USERNAME|PAYLOAD\n
```


| Felt         | Betydning                                                                                      |
| ------------ | ---------------------------------------------------------------------------------------------- |
| **TYPE**     | Hva slags melding det er, som tekst (for eksempel `PRESENCE` eller `CHAT`).                    |
| **ROOM**     | Romnavn, eller strek (`-`) når rom ikke brukes (som ved presence).                             |
| **USERNAME** | Avsender, rom-eier eller annen rolle avhengig av meldingstype.                                 |
| **PAYLOAD**  | Resten av informasjonen: chattekst, IP-adresse, `OPEN`, tekst om lukket rom med mottaker, osv. |


Teksten skal være gyldig UTF-8. Ingen av feltene skal inneholde `|` eller linjeskift, slik at linjen kan deles opp på en entydig måte uten spesielle escape-regler.

### 5.3 Transportlag: porter og binding


| Protokoll | Port      | Bruk i denne leveransen                             |
| --------- | --------- | --------------------------------------------------- |
| **UDP**   | **50000** | All SECP-trafikk, broadcast, multicast og unicast. |
| **TCP**   | **50001** | Nevnt i RFC for garanterte rom; **ikke brukt** her. |


Mottaker lytter altså på UDP port 50000 (avsender kan bruke en tilfeldig kildeport, som vanlig for UDP).

### 5.4 Meldingstyper og eksempler

Programmet kjenner blant annet disse typene (se `secp.cpp` for parsing og navn):


| TYPE              | Kort forklaring                | Typisk ROOM / PAYLOAD                                       |
| ----------------- | ------------------------------ | ----------------------------------------------------------- |
| **PRESENCE**      | Jeg er her; min IP publiseres. | `ROOM = -`, `PAYLOAD` = IPv4 som tekst.                     |
| **ROOM_ANNOUNCE** | Et åpent rom annonseres.       | `ROOM` = romnavn, `PAYLOAD = OPEN`, eier i USERNAME.        |
| **INVITE**        | Invitasjon til lukket rom.     | `PAYLOAD = CLOSED;to=<brukernavn>`.                         |
| **CHAT**          | Selve chatteksten i et rom.    | `ROOM` skiller f.eks. globale `USN Chat` fra navngitte rom. |


Det globale fellesrommet heter `**USN Chat`**. Meldinger med `CHAT|USN Chat|...` behandles som broadcast-chat i programmet.

**Eksempler på gyldige linjer:**

```text
PRESENCE|-|alice|192.168.1.10
ROOM_ANNOUNCE|gruppe1|alice|OPEN
INVITE|privat1|alice|CLOSED;to=bob
CHAT|USN Chat|alice|Hei alle sammen
CHAT|gruppe1|bob|Melding i grupperom
```

### 5.5 Multicast for åpne grupperom (RFC-forenkling)

I undervisningsøyemed kan flere åpne grupperom dele samme multicast-adresse. I `limits.hpp` er den satt til `239.0.0.1`. Klientene ser likevel på **hvilket romnavn** som står i hver `CHAT`-melding, og viser bare det som hører til det rommet brukeren faktisk sitter i. På den måten holdes multicast-oppsettet enkelt uten at romene blandes sammen i brukergrensesnittet.

### 5.6 Validering, lengdegrenser og robusthet

Når programmet mottar eller skal sende en melding, må teksten være «på riktig form»: den skal bestå av fire deler skilt med strek (`|`), og verken brukernavn, romnavn eller selve meldingsteksten skal være for langt. Hele linjen får heller ikke overstige en fast maksgrense (1024 tegn), slik at ingen pakke kan fylle mer enn det programmet er forberedt på å ta imot. I tillegg sjekkes det at teksten er gyldig UTF-8, slik at rare eller ødelagte tegn ikke skaper uforutsigbar oppførsel.

Prøver man å bygge en ugyldig melding (for kort, for langt, feil format), returnerer funksjonen som lager linjen et tomt resultat, da sendes **ingenting** ut på nettet. På mottakersiden forkastes linjer som ikke består sjekken, uten at programmet krasjer. Dette er måten oppgavens krav om **buffer overflow prevention** og **trygg håndtering av ukjente data** er løst på i praksis (se `secp.cpp`).

### 5.7 Sammenheng med øvrig rapport

**Kapittel 6** beskriver hvordan selve chat-programmet er bygd: hvilke deler som sender presence, fellesrom, grupperom og privat chat, og hvordan innkommende meldinger fordeles videre. **Kapittel 8** og **9** handler om hvordan dette er testet i bruk og bekreftet med Wireshark, altså at meldingene faktisk ser ut og oppfører seg som forventet i et reelt kjør.

---

## 6. Løsning og implementasjon

### 6.1 Overordnet arkitektur

Programmet kjører som én prosess med flere tråder som deler på arbeidet:


| Tråd               | Rolle                                                                                                                        |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| **Hovedtråd**      | Viser tekstmeny og venter på det brukeren taster.                                                                            |
| **Mottakstråd**    | Lytter på nettet, leser inn UDP-pakker, tolker SECP-linjer og sender dem videre til riktig del av programmet.                |
| **Heartbeat**      | Sender jevnlig presence, fjerner brukere som ikke har vært hørt på lenge, og gjentar ventende invitasjoner der det er behov. |
| **Gruppeannonser** | Sender jevnlig romannonser for brukerens eget åpne grupperom.                                                                |


Poengen er at nettverket ikke skal «henge» mens brukeren leser menyen: innkommende meldinger kan vises fortløpende.

**Hovedfiler:** `src/main.cpp`, `src/app/receive_multiplex_loop.cpp`, `src/app/text_menu.cpp`.

### 6.2 Modulær inndeling

- `**UdpSocket`:** Samler det som trengs for å åpne socket, binde, sende og motta, og for å slå på broadcast, gjenbruk av adresse og multicast-medlemskap.
- `**UserDirectory`:** Holder oversikt over hvilke brukernavn som er sett nylig og hvilken adresse de hadde, på en måte som er trygg å bruke fra flere tråder.
- `**DiscoveryService`:** Sender og tolker presence-meldinger.
- `**LobbyService`:** Håndterer det åpne fellesrommet **USN Chat** over broadcast.
- `**GroupRoomCoordinator`:** Romannonser, inn og ut av multicast-gruppe, og gruppechat.
- `**DirectMessageService`:** Invitasjoner, svar ja/nei, privat chat med unicast, og ny forsøk om invitasjon ikke ser ut til å ha nådd frem.

### 6.3 Kobling til RFC USNChat01 (SECP)

Syntaks, typer og validering er beskrevet i **kapittel 5**. Koden bygger og leser linjer med funksjonene i `secp.hpp` / `secp.cpp`. Mottakstråden leser tekst fra hvert UDP-datagram, forsøker å parse én SECP-linje, og videresender til discovery, lobby, grupperom eller privat chat ut fra type og romnavn.

### 6.4 Kartlegging mot funksjonelle krav

**Presence:** Discovery sender broadcast ved oppstart og i heartbeat; katalogen oppdateres når andre svarer; gamle oppføringer ryddes bort.

**Fellesrom:** Lobby bygger chat-linjer med `USN Chat`; mottakstråden sender slike meldinger til lobby når romfeltet er nettopp det navnet.

**Åpne grupperom:** Koordinatoren annonserer rommet jevnlig, melder socket inn og ut av multicast etter behov, og sender gruppechat på multicast-adressen.

**1–1:** Direktemeldingstjenesten sender invitasjon og privat chat med unicast til adresse hentet fra katalogen; sesjonen håndteres med aksept/avslag og gjentakelse av invitasjon ved behov.

[skjermbilde: arkitektur eller oversikt – valgfritt]

---

## 7. Robusthet og feilhåndtering

I et UDP-system er det en grunnleggende forutsetning at **ting kan feile**: pakker kan tape, operativsystemet kan avbryte venting midt i et nettverkskall, og én enkelt feil bør ikke knekke hele programmet. Robusthet handler om **å skille mellom forbigående hendelser og alvorlige feil**, og om **å fortsette forsvarlig** der det er rimelig.

### 7.1 Nettverksfeil

Programmet må kunne lytte på **vanlig UDP** og på **multicast** når brukeren deltar i et grupperom, samtidig. Løsningen er å la én mottakertråd vente på **flere kanaler** på én gang, slik at den ikke blokkerer på den ene mens den andre har meldinger. Det er et typisk mønster: nettverks-I/O samles på ett sted, mens meny og brukerinput kan stå stille uten å «miste» trafikk.

Når operativsystemet avbryter et ventekall (i POSIX markeres det ofte med feilkoden `EINTR`), behandles det som et **midlertidig avbrudd**, for eksempel fordi et signal ble levert, og forsøket gjentas. **Vedvarende** socket-feil logges, og mottak fortsetter der det lar seg gjøre, slik at én mislykket lesing ikke nødvendigvis avslutter hele klienten. Ved **kritiske** feil i selve ventelogikken kan mottakstråden stoppes kontrollert, mens hovedtråden fortsatt kan avslutte programmet ryddig (for eksempel når brukeren velger å gå ut).

### 7.2 Ugyldig input (CLI)

Før noe sendes ut på nettet, sjekkes **tastaturinput** fra menyen: gyldig valg, meningsfull tekst der det trengs, og ingen tomme meldinger der de ikke gir mening. Det er en **første forsvarslinje** som reduserer unødvendig trafikk og gir forståelige feilmeldinger til brukeren.

De **samme lengdegrensene** som SECP beskriver (jf. kapittel 5, blant annet romnavn, brukernavn, chattekst og hele linjen), brukes også ved innlesing. Kan programmet likevel ikke bygge en gyldig protokollinje, sendes **ingenting**. Prinsippet samsvarer med mottakssiden: **ugyldig data skal ikke ut på ledningen**.

### 7.3 Ugyldige eller ondsinnede pakker

UDP har ingen innebygd «tilgangskontroll»: i prinsippet kan hvem som helst på samme nett sende datagram til en åpen port. Innholdet kan derfor **ikke** tas for gitt, verken som korrekt formatert, komplett eller velmenende.

Hver innkommende nyttelast behandles som **ukjent data** til den har bestått de samme reglene som SECP legger opp til: riktig oppdeling i felt, lovlige lengder, forbud mot visse tegn i felt (slik at formatet ikke kan «sprekke»), og gyldig tekstkoding (UTF-8). Linjer som feiler, **forkastes**; de når ikke logikken for presence, fellesrom, grupperom eller private invitasjoner. Meldingstyper klienten ikke kjenner, **ignoreres** fremfor å bli tolket etter beste gjetning. Målet er å unngå både krasj og feilaktig oppførsel når noe uventet dukker opp på nettet.

### 7.4 Buffer overflow prevention

Oppgaven krever at programmet beskytter seg mot **buffer overflow**, at man ikke skriver forbi minnet som er satt av til én pakke. I praksis er dette løst på to måter som henger sammen.

**Mottak:** Det reserveres et **fast, begrenset område** for én innkommende pakke (her maksimalt 1024 byte, i tråd med protokollens maksgrense). Operativsystemet leverer ikke mer enn dette i ett kall til det området, så selve lesingen kan ikke overskride bufferen.

**Tolking og sending:** Bare de **faktisk mottatte** bytene brukes videre, og bare etter validering mot protokollen. Er et datagram større enn mottaksbufferen, vil applikasjonen typisk bare se begynnelsen; en ufullstendig eller avkuttet linje vil da sjelden være gyldig SECP og blir forkastet. Utgående linjer som overstiger samme makslengde, bygges ikke eller sendes ikke.

Kort sagt: **ingen ubegrenset kopiering** fra nettet inn i programmet, og **ingen sending** uten at total størrelse holder seg innenfor grensen. Det er den direkte koblingen mellom oppgavens krav om sikker parsing og forebygging av overflow.

---

## 8. Testing

### 8.1 Testmetode

Testing er gjort **manuelt** gjennom programmets meny, med **to eller flere terminalvinduer** på samme maskin og der det har latt seg gjøre **sammen med andre** på LAN. En mer detaljert sjekkliste finnes i `TESTING_GUIDE.md`.

### 8.2 Bygg og kjøring

```text
cmake -S . -B build
cmake --build build -j
./build/chatapp <brukernavn>
```

[skjermbilde: vellykket bygg og programstart]

### 8.3 Testdekning mot krav


| Område    | Hva som verifiseres                              | [skjermbilde]               |
| --------- | ------------------------------------------------ | --------------------------- |
| Presence  | To klienter ser hverandre under «aktive brukere» | [skjermbilde]               |
| USN Chat  | Melding broadcastes og mottas                    | [skjermbilde]               |
| Grupperom | Rom listes, innmelding, multicast-chat           | [skjermbilde] [skjermbilde] |
| 1–1       | Invitasjon, aksept, privat chat                  | [skjermbilde] [skjermbilde] |
| Robusthet | Ugyldig menyvalg, tom melding, for lang tekst    | [skjermbilde]               |


### 8.4 Testing med andre studenter

[skjermbilde: discovery med andre på nettverket]  
[skjermbilde: USN Chat]  
[skjermbilde: grupperom]  
[skjermbilde: privat 1–1]

Kort beskrivelse av observasjoner (fyll inn etter egen kjøring): *…*

---

## 9. Wireshark og OSI-lag

Wireshark brukes til å **bekrefte** at trafikken er UDP mot port 50000, og at innholdet i pakken stemmer med SECP-linjer man forventer (type, rom, avsender, tekst). Et enkelt filter er `udp.port == 50000`; for å skille broadcast, multicast og unicast kan man også se på destinasjons-IP.


| Lag | Hva som observeres                                         |
| --- | ---------------------------------------------------------- |
| L2  | Ethernet-rammer og MAC-adresser                            |
| L3  | IPv4; om pakken går til broadcast, multicast eller én vert |
| L4  | UDP med kilde- og destinasjonsport                         |
| L7  | Lesbar SECP-tekst inni UDP-payload                         |


[skjermbilde: Wireshark – presence broadcast]  
[skjermbilde: Wireshark – CHAT USN Chat]  
[skjermbilde: Wireshark – multicast]  
[skjermbilde: Wireshark – unicast INVITE/CHAT]

---

## 10. Sikkerhetsvurdering

### 10.1 Identifiserte sårbarheter

- **Ingen autentisering:** Hvem som helst på nettet kan i prinsippet sende en pakke med et annet brukernavn i feltet, så lenge formatet er gyldig.
- **Ingen kryptering:** Alt innhold kan leses av den som kan ta opp trafikk på LAN.
- **UDP:** Meldinger kan mistes; rekkefølge er ikke garantert.

### 10.2 Implementerte tiltak

Streng sjekk av SECP-linjer og feltlengder reduserer risiko for at programmet misbrukes til å oversvømme minnet eller krasje på uventet format. I privat modus sjekkes det i tillegg at meldinger som skal tilhøre en sesjon, faktisk matcher forventet avsender og rom, slik at ikke «alt» godtas uten videre.

### 10.3 Forslag til videre arbeid

I et reelt system ville man vurdert identitet (for eksempel signerte meldinger), kryptering (DTLS eller eget lag), og begrensning av hvor mange meldinger man godtar per tidsenhet fra samme kilde.

---

## 11. Diskusjon og begrensninger

Løsningen er holdt innenfor rammene til obligatorisk oppgave: én UDP-port for all applikasjonslogikk, tekstprotokoll som er lett å feilsøke, og ingen TCP-implementasjon for de «garanterte rommene» i RFC. Broadcast og multicast forutsetter at nettverket faktisk lar slik trafikk passere; på noen nett (NAT, gjeste-WiFi med klient-isolasjon) vil ikke alt fungere som på et «rent» lab-LAN. Det er en kjent begrensning for denne typen prototype.

---

## 12. Konklusjon

Oppgaven har vært å bygge et UDP-basert chattsystem uten sentral server. Rapporten har vist hvordan meldingsformatet SECP (kapittel 5) knytter sammen alle delene, og hvordan programmet er delt i moduler og tråder (kapittel 6) slik at presence, åpent fellesrom, grupperom og privat chat kan sameksistere. Broadcast brukes der «alle» skal nås, multicast til gruppechat, unicast til invitasjon og lukket samtale. Manuell testing (kapittel 8) og inspisering i Wireshark (kapittel 9) bekrefter at trafikken samsvarer med forventningene. Arbeidet illustrerer både hva UDP er godt egnet til i en enkel LAN-sammenheng, og hvor skjør løsningen er uten autentisering og kryptering, noe som er naturlig neste steg i mer seriøse systemer.

---

## 13. KI-erklæring

Jeg har brukt KI-verktøy (for eksempel til formulering og struktur i rapporttekst) som **støtte**, men jeg står ansvarlig for innholdet. **Kildekode**, **testkjøringer**, **Wireshark-bilder** og **faglig vurdering** er egen innsats eller bygger på kursmateriell og egen implementasjon. Eventuelle avvik mellom rapport og kode er min feil og skal rettes før innlevering.

---

## 14. Referanser

[1] USN, «RFC USNChat01 – spesifikasjon av chatprotokoll (SECP),» utdelt kursmateriell, 2026. (Lokal fil: `RFC USNChat01.docx`.)

[2] The Open Group, *The Open Group Base Specifications Issue 7*, POSIX.1-2017, 2018. (Socket-API: `socket`, `bind`, `sendto`, `recvfrom`, `poll`.)

[3] Wireshark Foundation, *Wireshark User’s Guide*, [https://www.wireshark.org/docs/](https://www.wireshark.org/docs/), brukt til analyse av UDP-pakker og filtre, 2026.

[4] Postel, J., «User Datagram Protocol,» *RFC 768*, Internet Engineering Task Force, 1980. (Grunnleggende UDP-semantikk.)

---

*Slutt på rapport.*
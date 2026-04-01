# Sikkerhetsvurdering oblig 2

Utdrag fra koden (porter 50000 / 50001 / 50002). Tilpass til veileders krav.

---

## 1. Identifiserte sårbarheter

### 1.1 Hemmelig nøkkel (PSK) i klartekst på UDP

`SecureRoomService::send_secure_invite` bygger en INVITE der **hele den symmetriske nøkkelen** sendes som heks i payload (`key=…`) over **UDP broadcast/unicast** på port 50000. Enhver som kan avlytte samme L2-segment (eller har speiling av trafikk) kan **lese PSK** og deretter dekryptere tilsvarende TCP-trafikk på 50002. Dette er eksplisitt dokumentert i `secure_message_crypto.hpp` som pedagogisk begrensning.

### 1.2 Ingen kryptografisk identitetsbinding

Brukernavn i SECP er **selvpåstått**. `UserDirectory` knytter brukernavn til **sist observerte kilde-IP** fra PRESENCE/oppdateringer, ikke til signerte påstander. Det finnes ingen sertifikater, ingen offentlige nøkler og ingen bevis på at «Alice» kontrollerer en gitt nøkkel utover at trafikk kommer fra en IP som katalogen assosierer med «Alice».

### 1.3 Svak entropi i sesjons-ID

`make_session_id()` bruker `std::mt19937` med `random_device` som seed og trekker **6 heksadesimale tegn** (teoretisk 24 bit). Det gir lite rom for kollisjoner i et klasserom, men er **ikke** på nivå med kryptografisk sesjonsidentifikatorer og er sårbart for forutsigbarhet dersom PRNG-tilstand lekkes eller seed er svak.

### 1.4 Åpent garantert TCP-rom (50001) uten tilgangskontroll på transportlag

Vert **aksepterer TCP-tilkoblinger** opp til en maksgrense (`accept_new_clients_unlocked`) uten å verifisere at klientens IP samsvarer med en bestemt bruker i katalogen. Chat er **klartekst-SECP** på linje. Hvem som helst som kan nå vertens IP:50001 kan delta i rommet og lese/relayere meldinger (MITM på samme LAN, eller direkte tilkobling).

### 1.5 Replay av krypterte rammer

`seal_message` / `open_message` beskytter **konfidensialitet og integritet per melding**, men det finnes **ingen sekvensnummer, tidsstempel eller nonce-registrering** på mottakers side. En angriper som har fanget en gyldig blob kan **sende den på nytt** innenfor samme TCP-sesjon (begrenset av at nøkkelen må være den samme).

### 1.6 Delte trusselmodeller: LAN-angriper

På et delt LAN kan en angriper kombinere **avlytting** (PSK fra INVITE), **tilkobling til åpent TCP-rom** (50001), og i noen scenarioer **IP-spoofing eller ARP-påvirkning** (utenfor programkoden) for å forstyrre eller etterligne endepunkter. Uten TLS eller gjensidig autentisering er **transporten** ikke beskyttet mot aktive angrep på nettverksveien.

### 1.7 Ressurs- og protokollgrenser

Lengdebegrensninger (`kMaxTcpFrame`, SECP-lengder) reduserer risiko for enkle buffer-scenarioer, men **nekting av tjeneste** (mange tilkoblinger til 50001/50002, store ugyldige rammer som tømmer CPU) er ikke fullt adressert i en produksjonsforstand.

---

## 2. Implementerte sikkerhetstiltak

### 2.1 Kryptografisk innkapsling på sikkert rom (TCP 50002)

- **Encrypt-then-MAC:** ChaCha20 på klartekst, deretter HMAC-SHA256 over `nonce ‖ ciphertext` (`secure_message_crypto.cpp`).
- **Tilfeldig 12-byte nonce per melding** (`fill_random`), som reduserer risiko for keystream-gjenbruk med samme PSK.
- **Separat MAC-nøkkel** avledet fra PSK (`derive_mac_key`) slik at samme 32-byte ikke brukes direkte til både strømkiffer og MAC.
- **MAC verifiseres før dekryptering** ved feil avvises meldingen uten å «leake» padding-informasjon (kommentar i kode).
- **Konstant-tidsaktig MAC-sammenligning** over 32 byte (`const_time_equal_32`) - bedre enn naiv `memcmp` for tag-sammenligning i undervisningskontekst.

### 2.2 Logisk tilgangskontroll rundt sikker INVITE og TCP-håndtrykk

- **UDP INVITE (sikker):** `on_udp_invite` krever blant annet at `ROOM`-felt i SECP matcher `sess=` i payload; at `to_user` er lokal bruker; at avsender-**IP** for UDP-pakken matcher `UserDirectory::find_address(inviter)` (`secure_room_service.cpp`).
- **Vert etter TCP-aksept:** Før `HostActive` settes, må første dekrypterte SECP `CHAT` være korrekt håndtrykk; `getpeername` på TCP-forbindelsen må matche **forventet IP** for **invitert** brukernavn i katalogen (`find_address(invited_username_)`).
- **Feil MAC eller feil håndtrykk:** Uautorisert klient kobles fra (`reject_unauthorized_peer_unlocked`); vert kan fortsette å lytte etter ny lovlig klient der designet tillater det.

### 2.3 Discovery (PRESENCE)

- `DiscoveryService::on_presence` avviser oppføringer der **payload-IP ikke samsvarer med faktisk kilde-IP** i UDP-datagrammet (unntak for tom/`-`), som reduserer enkel **fjern** spoofing av andres brukernavn uten å kontrollere samme adresse.

### 2.4 Generelle tiltak

- **Inndatavalidering** i protokoll (SECP-bygging returnerer tom streng ved ugyldige felt), **maks lengder** på brukernavn, romnavn og chat.
- **Mutex** rundt delt tilstand i tjenester (unngår datakappløp i referanseimplementasjonen; pedagogisk korrekt mønster).

---

## 3. Svakheter ved autentisering og autorisasjon

| Aspekt | Observasjon i implementasjonen |
|--------|--------------------------------|
| **Hva «Alice» betyr** | Identitet = **brukernavnstreng** + **sist kjente IP**. Ingen signatur som knytter navn til nøkkel. |
| **Tillit til katalog** | Sikker INVITE godtas delvis fordi inviters IP matcher katalogen - men katalogen er **bygget fra ubekreftede PRESENCE**-meldinger på samme UDP-kanal. |
| **Vert på sikkert rom** | Etter at PSK er avlyttet, kan en angriper **teoretisk** prøve å etablere TCP før den legitime klienten (begrenset av at vert forventer riktig håndtrykk og riktig peer-IP - men PSK-lekkasjen er det kritiske bruddet). |
| **Invitee** | Må eksplisitt akseptere session-id; det gir **menneskelig** kontroll, men ikke kryptografisk sterk autorisasjon. |
| **Garantert TCP-rom** | **Ingen** autorisasjon av klient utover «første som kobler til» og SECP-validering av linjer - i tråd med «åpent» rom, men **ikke** egnet for konfidensialitet. |

**Oppsummert:** Systemet implementerer **begrenset tilgangskontroll** for sikker TCP basert på **IP + katalog + delt hemmelighet**, ikke på **sterk identitet**. Det tilsvarer et **styrt læringsmiljø**, ikke en **zero-trust**-arkitektur.

---

## 4. Svakheter ved valgt krypteringsmetode

| Punkt | Forklaring |
|-------|------------|
| **PSK distribusjon** | Symmetrisk nøkkel sendes i **klartekst på UDP**. Alle konfidensialitetsgevinster på TCP 50002 forutsetter at PSK **ikke** er kompromittert - den er det ved design i denne øvingen. |
| **Ingen forward secrecy** | Lekkasje av én PSK gir dekryptering av **hele** samtalen med den nøkkelen. Ingen ephemeral Diffie-Hellman eller lignende. |
| **ChaCha20 + separat HMAC** | Fornuftig pedagogisk (synlig «encrypt» og «MAC»). Moderne produkter bruker ofte **AEAD** (f.eks. ChaCha20-Poly1305) for å unngå feil i sammensetning; her er EtM eksplisitt implementert - bra for forståelse, men **ikke** det samme som en gjennomgående kryptografisk standard-API. |
| **Ingen replay-beskyttelse** | Se pkt. 1.5. |
| **Ingen binding til transport** | Krypto laget er **koblet til PSK og applikasjonspayload**, ikke til TLS sessions. |
| **Sidekanaler** | Kommentarer i `secure_message_crypto.hpp` angir at implementasjonen **ikke** er hardenet mot avanserte timing-/sidekanalangrep. |

---

## 5. Svakheter ved UDP discovery og invitasjoner

- **Broadcast/multicast:** PRESENCE og mange SECP-meldinger er synlige for **hele broadcast-domenet**. Det gir **ingen konfidensialitet** på metadata (hvem som er online, romnavn i annonser).
- **ROOM_ANNOUNCE** for garantert TCP avslører **romnavn, vert og TCP-port** i klartekst på UDP - forventet for annonsering, men **ikke** personvernvennlig.
- **INVITE (sikker):** Inneholder **session-parametre og nøkkel** i samme pakke som kan sniffes; det er den alvorligste UDP-sårbarheten i oblig 2-løsningen.
- **INVITE (lukket UDP-rom, oblig 1):** Egen flyt med `CLOSED` uten `secure_tcp=1`; fortsatt avhengig av **UDP-kilde-IP** og katalog - samme tillitsmodell som over, uten TCP-krypto-laget.

---

## 6. Forslag til forbedringer

1. **Nøkkelutveksling:** Erstatt PSK i UDP med **Diffie-Hellman** (evt. Curve25519) signert med **langsiktige nøkler** eller verifisert via **TLS** / **Signal-lignende** protokoll; eller minst **TLS 1.3** på dedikert kanal for etablering.
2. **Autentisering:** Introdusere **X.509** eller **SPKI**-lignende identiteter, eller **TOFU** (trust on first use) med fingerprint vist til brukeren før aksept.
3. **AEAD og standardbibliotek:** Bruk **ChaCha20-Poly1305** fra et revidert bibliotek (f.eks. libsodium) for å redusere implementasjonsrisiko.
4. **Replay:** Legg inn **monotont sekvensnummer** eller **tidsvindu** i autentisert payload før MAC.
5. **Sesjons-ID:** Trekk **128+ bit** fra **CSPRNG** (`getrandom` / `std::random_device` brukt korrekt, eller `RAND_bytes`).
6. **Garantert TCP:** Dersom rommet skal være «kun inviterte», kreve **token** eller **TLS client auth**; for «åpent» rom: dokumenter eksplisitt at **alle med nettverkstilgang kan lese og skrive**.
7. **Rate limiting og tilkoblingsgrenser:** På server for å dempe enkle DoS-forsøk.

---

## 7. Pedagogisk løsning versus produksjonssikker løsning

| Tema | Pedagogisk løsning (denne koden) | Produksjonssikker retning |
|------|----------------------------------|---------------------------|
| **Formål** | Vise TCP-roller, SECP, **Encrypt-then-MAC**, poll-basert I/O, og **begrensninger** ved PSK over UDP | Beskytte brukere mot avlytting, spoofing og MITM i åpne nett |
| **Nøkkel** | Tilfeldig PSK, men **sendt i klartekst** i INVITE | Aldri hemmelighet i klartekst; bruk **TLS** eller **E2E** med autentisert utveksling |
| **Identitet** | Brukernavn + IP i katalog | **Sertifikater**, **OAuth**, **mutual TLS**, eller **E2E**-identitetsnøkler |
| **Åpent TCP-rom** | Klartekst, åpen tilkobling | Evt. **TLS** selv for «åpent» rom hvis integritet ønskes; ellers **åpen bar**-modell med full åpenhet |
| **Sikker TCP-rom** | ChaCha20 + HMAC, IP-sjekk, håndtrykk | **AEAD**, **replay-beskyttelse**, **forward secrecy**, **hardened** crypto library |
| **Discovery** | UDP broadcast, synlig metadata | **Private discovery**, **encrypted presence**, eller sentral infrastruktur med autentisering |
| **Rapporttekst** | Dokumenter **eksplisitt** hva som er demonstrasjon og hva som ville vært annerledes i drift | - |

**Konklusjon for rapporten:** Implementasjonen viser **fornuftige byggeklosser** (EtM, nonce, MAC før dekryptering, IP-sjekker på kritiske stier) innenfor et **bevisst begrenset trusselbilde** (typisk pålitelig LAN, ærlige studenter). Den er **ikke** ment som sikker mot aktive angripere med nettverkstilgang eller mot **global** motpart. Skill tydelig mellom **hva koden faktisk beskytter mot** (passiv avlytting av TCP-nyttelast *etter* at PSK er hemmelig holdt) og **hva den ikke beskytter mot** (PSK-lekkasje på UDP, åpent 50001, svak identitet, replay).

---

*Referanser i kodebase: `include/chat/crypto/secure_message_crypto.hpp`, `src/crypto/secure_message_crypto.cpp`, `src/services/secure_room_service.cpp`, `src/services/discovery_service.cpp`, `src/services/guaranteed_open_room_service.cpp`, `include/chat/services/secure_room_service.hpp` (kommentarer om identitet og IP).*

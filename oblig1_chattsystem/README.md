# Chatapp (USN SECP / oblig 1+2)

CLI-klient i C++. UDP 50000 (SECP), TCP 50001 (garantert rom), TCP 50002 (sikkert rom med ChaCha20+HMAC).

## Bygg

```bash
cmake -S . -B build
cmake --build build
```

## Kjør

```bash
./build/chatapp <brukernavn>
```

Krever lokal IPv4. Se `documentasjon/` for testplan, rapportutkast og kodegjennomgang.

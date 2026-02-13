# Edirol-FA66-Firewire-SDR-I-Q-Streamer

I/Q-Streamer für die FireWire Edirol FA-66 Soundkarte

---

## Quickstart

1. Edirol FA-66 anschließen
2. Power-Schalter hinten auf **FireWire Netz** stellen
3. `fa66_ch34_buffered --nobuffer` aufrufen
4. gqrx starten:
   ```bash
   /Applications/Gqrx.app/Contents/MacOS/gqrx
   ```

---

## Verwendung

```bash
# Mit Buffer (Standard, stabil)
./fa66_ch34_buffered

# Oder explizit:
./fa66_ch34_buffered --buffer

# Ohne Buffer (niedrige Latenz, könnte Dropouts haben)
./fa66_ch34_buffered --nobuffer

# Hilfe anzeigen
./fa66_ch34_buffered --help
```

---

## Kompilieren

```bash
clang -o fa66_iq_server fa66_iq_server.c -framework CoreAudio -framework AudioToolbox
```

## Starten

```bash
./fa66_iq_server
```

Dann kann in einem zweiten Terminal-Fenster gqrx gestartet werden:

```bash
cd /Applications/Gqrx.app/Contents/MacOS
./gqrx
```

---

## GQRX konfigurieren

Im **"Configure I/O devices"** Dialog:

| Einstellung    | Wert                                              |
| -------------- | ------------------------------------------------- |
| **Device**     | `RTL-SDR Spectrum Server` oder `Other`            |
| **Device string** | `rtl_tcp=127.0.0.1:1234`                      |
| **Input rate** | `48000`                                           |
| **Decimation** | `None`                                            |

Dann auf **OK** klicken.

Im ersten Terminal solltest du dann sehen:

```
GQRX connected! Streaming channels 3+4...
```

## GQRX Settings

<img width="343" height="484" alt="image" src="https://github.com/user-attachments/assets/9ccdcc76-b64e-473c-ac77-db1fc365d995" />

## GQRX App

<img width="1089" height="716" alt="image" src="https://github.com/user-attachments/assets/811e07c0-8c25-4a95-bb90-20c5411fe5fa" />

# Edirol-FA66-Firewire-SDR-I-Q-Streamer

**********************************
* 
* I/Q - Streamer für die FireWire Edirol FA-66 Soundkarte
*
**********************************

(1) Edirol FA-66 anschliessen
(2) Power Schalter hinten auf FireWire Netz
(3) fa66_ch34_buffered --nobuffer aufrufen
(4) gqrx in /Applications/Gqrx.app/Contents/MacOS/./gqrx starten 

# Mit Buffer (Standard, stabil)
./fa66_ch34_buffered

# Oder explizit:
./fa66_ch34_buffered --buffer

# Ohne Buffer (niedrige Latenz, könnte Dropouts haben)
./fa66_ch34_buffered --nobuffer

# Hilfe anzeigen
./fa66_ch34_buffered --help

************
*
*  compilieren mit:
*
*  clang -o fa66_iq_server fa66_iq_server.c -framework CoreAudio -framework AudioToolbox
*
* *
*
*  starten mit 
*
*  ./fa66_iq_server
*
*  Dann kann in einem 2. Fenster gqrx gestartet werden
*
************

cd /Applications/Gqrx.app/Contents/MacOS
./gqrx
```

## **Im GQRX "Configure I/O devices" Dialog:**

**Device:** Wähle aus dem Dropdown:
- `RTL-SDR Spectrum Server` oder
- `Other` 

**Device string:**
```
rtl_tcp=127.0.0.1:1234
```

**Input rate:**
```
48000
```

**Decimation:**
```
None
```

**Dann auf "OK" klicken.**

---

Im ersten Terminal solltest du dann sehen:
```
GQRX connected! Streaming channels 3+4...

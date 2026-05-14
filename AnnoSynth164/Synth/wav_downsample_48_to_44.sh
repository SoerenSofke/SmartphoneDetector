#!/usr/bin/env bash
#
# downsample_48_to_44.sh
# -----------------------
# Konvertiert alle .wav-Dateien in einem Ordner von 48 kHz auf 44,1 kHz
# mit SoX (very-high-quality, linear-phase Sinc-Resampler).
# Originaldateien werden überschrieben (atomar via Temp-Datei).
#
# Nutzung:  ./downsample_48_to_44.sh /pfad/zum/ordner
#
# Voraussetzungen: sox  (brew install sox  /  sudo apt install sox)

set -uo pipefail

# --- Argumente prüfen ---------------------------------------------------------
if [ $# -lt 1 ]; then
    echo "Nutzung: $0 <verzeichnis>" >&2
    exit 1
fi

DIR="$1"

if [ ! -d "$DIR" ]; then
    echo "Fehler: '$DIR' ist kein Verzeichnis." >&2
    exit 1
fi

if ! command -v sox >/dev/null 2>&1; then
    echo "Fehler: sox ist nicht installiert." >&2
    echo "  macOS:  brew install sox" >&2
    echo "  Linux:  sudo apt install sox" >&2
    exit 1
fi

# --- Zähler -------------------------------------------------------------------
processed=0
skipped=0
failed=0

# --- Verarbeitung -------------------------------------------------------------
# nullglob: leerer Glob ergibt keine Iteration (statt literal "*.wav")
# nocaseglob: matcht auch .WAV, .Wav etc.
shopt -s nullglob nocaseglob

for file in "$DIR"/*.wav; do

    # Sample-Rate ermitteln
    rate=$(sox --i -r "$file" 2>/dev/null)
    if [ -z "$rate" ]; then
        echo "⚠️   Überspringe (nicht lesbar):  $(basename "$file")"
        failed=$((failed + 1))
        continue
    fi

    # Nur 48-kHz-Dateien anfassen
    if [ "$rate" != "48000" ]; then
        echo "⏭️   Überspringe (${rate} Hz):     $(basename "$file")"
        skipped=$((skipped + 1))
        continue
    fi

    # Bit-Tiefe ermitteln (für Dithering-Entscheidung)
    bits=$(sox --i -b "$file" 2>/dev/null)
    bits=${bits:-24}   # Fallback, falls leer

    # Temp-Datei im selben Verzeichnis (gleiches Filesystem -> atomares mv).
    # WICHTIG: Endung .wav muss am Ende stehen, sonst rät SoX am PID-Suffix
    # ("no handler for file extension '12345'").
    tmpfile="${file%.[wW][aA][vV]}.tmp.$$.wav"

    # Resampling:
    #   gain -1            = 1 dB Headroom gegen Inter-Sample-Peaks beim
    #                        Sinc-Resampling (verhindert Clipping zuverlässig,
    #                        Pegelverlust ist unhörbar)
    #   rate -v -L 44100   = very-high-quality, linear-phase Sinc-Resampler
    #   dither -s          = Triangular-Shaped Dither (nur sinnvoll bei <=16 Bit,
    #                        weil dort nach dem Resampling requantisiert wird)
    if [ "$bits" -le 16 ]; then
        sox "$file" "$tmpfile" gain -1 rate -v -L 44100 dither -s
    else
        sox "$file" "$tmpfile" gain -1 rate -v -L 44100
    fi
    sox_status=$?

    # Erfolg prüfen, dann atomar ersetzen
    if [ $sox_status -eq 0 ] && [ -s "$tmpfile" ]; then
        mv "$tmpfile" "$file"
        echo "✅  Konvertiert (${bits}-bit):    $(basename "$file")"
        processed=$((processed + 1))
    else
        rm -f "$tmpfile"
        echo "❌  Fehlgeschlagen:               $(basename "$file")"
        failed=$((failed + 1))
    fi

done

shopt -u nullglob nocaseglob

# --- Zusammenfassung ----------------------------------------------------------
echo ""
echo "─────────────────────────────────────────────"
echo "Fertig:  $processed konvertiert, $skipped übersprungen, $failed fehlgeschlagen"

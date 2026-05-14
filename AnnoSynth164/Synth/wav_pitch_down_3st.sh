#!/usr/bin/env bash
#
# pitch_down_3st.sh
# -----------------
# Senkt alle .wav-Dateien in einem Ordner um 3 Halbtöne ab.
# Originaldateien werden überschrieben (atomar via Temp-Datei).
#
# Verwendet SoX mit `pitch` (Phase-Vocoder): Tonhöhe ändert sich,
# Länge bleibt gleich.
#
# Nutzung:  ./pitch_down_3st.sh <verzeichnis>
#
# Voraussetzung: sox  (brew install sox  /  sudo apt install sox)

set -uo pipefail

# --- Konfiguration ------------------------------------------------------------
SEMITONES=-3                # negativ = runter, positiv = rauf
CENTS=$(( SEMITONES * 100 )) # SoX rechnet in Cents (1 Halbton = 100 Cents)

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
failed=0

# --- Verarbeitung -------------------------------------------------------------
shopt -s nullglob nocaseglob

for file in "$DIR"/*.wav; do

    # Bit-Tiefe für Dithering-Entscheidung
    bits=$(sox --i -b "$file" 2>/dev/null)
    bits=${bits:-24}

    # Temp-Datei mit .wav-Endung am Ende (sonst rät SoX am PID-Suffix)
    tmpfile="${file%.[wW][aA][vV]}.tmp.$$.wav"

    # Pitch-Shift:
    #   gain -1            = 1 dB Headroom gegen Inter-Sample-Peaks
    #   pitch -q $CENTS    = Pitch-Shift in Cents, -q = höhere Qualität
    #   dither -s          = Dither nur bei <=16 Bit nötig
    if [ "$bits" -le 16 ]; then
        sox "$file" "$tmpfile" gain -1 pitch -q "$CENTS" dither -s
    else
        sox "$file" "$tmpfile" gain -1 pitch -q "$CENTS"
    fi
    sox_status=$?

    # Erfolg prüfen, dann atomar ersetzen
    if [ $sox_status -eq 0 ] && [ -s "$tmpfile" ]; then
        mv "$tmpfile" "$file"
        echo "✅  Pitched ${SEMITONES} st (${bits}-bit):  $(basename "$file")"
        processed=$((processed + 1))
    else
        rm -f "$tmpfile"
        echo "❌  Fehlgeschlagen:                $(basename "$file")"
        failed=$((failed + 1))
    fi

done

shopt -u nullglob nocaseglob

# --- Zusammenfassung ----------------------------------------------------------
echo ""
echo "─────────────────────────────────────────────"
echo "Fertig:  $processed verarbeitet, $failed fehlgeschlagen"

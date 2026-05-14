#!/usr/bin/env bash
#
# run_pdr.sh
# ----------
# Führt `python3 pdr.h <wav-datei> 1` für alle .wav-Dateien
# in einem Ordner aus.
#
# Nutzung:  ./run_pdr.sh <verzeichnis>
#
# Hinweis:  pdr.h muss im aktuellen Arbeitsverzeichnis liegen
#           (von dem aus das Skript aufgerufen wird).

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

if [ ! -f "pdr.h" ]; then
    echo "Warnung: 'pdr.h' ist im aktuellen Verzeichnis nicht zu finden." >&2
    echo "         (Skript läuft trotzdem weiter, falls du das absichtlich willst.)" >&2
fi

# --- Zähler -------------------------------------------------------------------
processed=0
failed=0

# --- Verarbeitung -------------------------------------------------------------
shopt -s nullglob nocaseglob

for file in "$DIR"/*.wav; do
    echo "▶  $(basename "$file")"
    if python3 pdr.h "$file" 1; then
        processed=$((processed + 1))
    else
        echo "❌  Fehler bei: $(basename "$file")"
        failed=$((failed + 1))
    fi
done

shopt -u nullglob nocaseglob

# --- Zusammenfassung ----------------------------------------------------------
echo ""
echo "─────────────────────────────────────────────"
echo "Fertig:  $processed verarbeitet, $failed fehlgeschlagen"

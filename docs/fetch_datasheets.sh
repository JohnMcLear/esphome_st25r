#!/usr/bin/env bash
# fetch_datasheets.sh — Download all ST25R product datasheets from STMicroelectronics
#
# Run this script from the repo root or the docs/ directory to populate docs/
# with the official datasheets for every ST25R NFC transceiver in the family.
#
# ST25R product family split by TX output power class:
#
#   1.6 W devices  — ST25R3911B, ST25R3916, ST25R3916B, ST25R3917B, ST25R3919
#   2.2 W devices  — ST25R3918, ST25R3918B, ST25R3920
#
# Usage:
#   cd docs && bash fetch_datasheets.sh
#   # or from repo root:
#   bash docs/fetch_datasheets.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_URL="https://www.st.com/resource/en/datasheet"

# ST25R product family — (filename → description)
# Ordered: 1.6 W class first, then 2.2 W class.
declare -A PRODUCTS=(
    # ── 1.6 W output power ──────────────────────────────────────────────────
    ["st25r3911b.pdf"]="ST25R3911B  High-performance NFC/HF card reader, 1.6 W (DS8679)"
    ["st25r3916.pdf"]="ST25R3916   High-performance multiprotocol NFC transceiver, 1.6 W (DS12484)"
    ["st25r3916b.pdf"]="ST25R3916B  Enhanced ST25R3916, improved sensitivity, 1.6 W (DS13406)"
    ["st25r3917b.pdf"]="ST25R3917B  ST25R3916B reduced-feature variant, 1.6 W (no NFC-V / card emul.)"
    ["st25r3919.pdf"]="ST25R3919   Automotive-grade ST25R3916, 1.6 W (AEC-Q100)"
    # ── 2.2 W output power ──────────────────────────────────────────────────
    ["st25r3918.pdf"]="ST25R3918   High-performance multiprotocol NFC transceiver, 2.2 W"
    ["st25r3918b.pdf"]="ST25R3918B  Enhanced ST25R3918, improved sensitivity, 2.2 W"
    ["st25r3920.pdf"]="ST25R3920   Automotive-grade ST25R3918, 2.2 W (AEC-Q100)"
)

# Explicit order so output is grouped by power class
ORDERED=(
    # 1.6 W
    st25r3911b.pdf
    st25r3916.pdf
    st25r3916b.pdf
    st25r3917b.pdf
    st25r3919.pdf
    # 2.2 W
    st25r3918.pdf
    st25r3918b.pdf
    st25r3920.pdf
)

echo "Downloading ST25R product datasheets to: $SCRIPT_DIR"
echo ""

SUCCESS=0
SKIPPED=0
FAILED=0

CURRENT_CLASS=""
for FILENAME in "${ORDERED[@]}"; do
    # Print section header when power class changes
    case "$FILENAME" in
        st25r391[1-7]*.pdf|st25r3919.pdf)
            if [[ "$CURRENT_CLASS" != "1.6W" ]]; then
                echo "  ── 1.6 W output power ──"
                CURRENT_CLASS="1.6W"
            fi ;;
        st25r3918*.pdf|st25r3920.pdf)
            if [[ "$CURRENT_CLASS" != "2.2W" ]]; then
                echo ""
                echo "  ── 2.2 W output power ──"
                CURRENT_CLASS="2.2W"
            fi ;;
    esac

    DEST="$SCRIPT_DIR/$FILENAME"
    URL="$BASE_URL/$FILENAME"
    DESC="${PRODUCTS[$FILENAME]}"

    if [[ -f "$DEST" ]]; then
        printf "  [skip]  %-22s  (already present)\n" "$FILENAME"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    printf "  [fetch] %-22s  %s\n" "$FILENAME" "$DESC"
    if curl -fsSL --connect-timeout 15 --max-time 60 \
            -H "User-Agent: Mozilla/5.0" \
            -o "$DEST" "$URL"; then
        # Verify it is actually a PDF
        if [[ "$(head -c 4 "$DEST" 2>/dev/null)" == "%PDF" ]]; then
            SIZE=$(du -k "$DEST" | cut -f1)
            echo "             -> OK  (${SIZE} kB)"
            SUCCESS=$((SUCCESS + 1))
        else
            echo "             -> WARNING: downloaded file does not look like a PDF, removing"
            rm -f "$DEST"
            FAILED=$((FAILED + 1))
        fi
    else
        echo "             -> FAILED  (curl error $?)"
        rm -f "$DEST"
        FAILED=$((FAILED + 1))
    fi
done

echo ""
echo "Done.  success=$SUCCESS  skipped=$SKIPPED  failed=$FAILED"

if [[ $FAILED -gt 0 ]]; then
    echo ""
    echo "Some datasheets could not be downloaded.  ST occasionally requires"
    echo "account login for certain documents.  In that case, download them"
    echo "manually from https://www.st.com and place them in docs/."
    exit 1
fi

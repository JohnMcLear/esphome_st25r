#!/usr/bin/env bash
# fetch_datasheets.sh — Download all ST25R product datasheets and eval-board data briefs
#
# Downloads from STMicroelectronics into this docs/ directory:
#
#   IC datasheets  → docs/                  (https://www.st.com/resource/en/datasheet/)
#   Board briefs   → docs/boards/           (https://www.st.com/resource/en/data_brief/)
#
# Complete ST25R family covered:
#
#   Legacy / first-generation
#     ST25R95
#   Second-generation (ST25R39xx), 1.6 W
#     ST25R3911B, ST25R3916, ST25R3916B, ST25R3917B, ST25R3919
#   Second-generation (ST25R39xx), 2.2 W
#     ST25R3918, ST25R3918B, ST25R3920
#   Third-generation (ST25Rxxx), low-power reader
#     ST25R100, ST25R200
#   Third-generation (ST25Rxxx), high-performance reader
#     ST25R300, ST25RN300, ST25R500
#
#   X-NUCLEO eval boards
#     NFC03A1 (ST25R95)  NFC05A1 (ST25R3911B)  NFC06A1 (ST25R3916)
#     NFC08A1 (ST25R3916B/ST25R3918)            NFC09A1 (ST25R3918)
#     NFC12A1 (ST25R500/ST25R300)
#
# Usage:
#   cd docs && bash fetch_datasheets.sh
#   # or from repo root:
#   bash docs/fetch_datasheets.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DS_BASE="https://www.st.com/resource/en/datasheet"
DB_BASE="https://www.st.com/resource/en/data_brief"

# ── IC datasheets ─────────────────────────────────────────────────────────────
# Each entry: filename → human description
# Output directory: docs/   (SCRIPT_DIR)

declare -A IC_DATASHEETS

# Legacy
IC_DATASHEETS["st25r95.pdf"]="ST25R95     ISO14443/15693 NFC reader (legacy, first-gen)"

# ST25R39xx — 1.6 W
IC_DATASHEETS["st25r3911b.pdf"]="ST25R3911B  Multiprotocol NFC transceiver, 1.6 W"
IC_DATASHEETS["st25r3916.pdf"]="ST25R3916   High-performance multiprotocol NFC transceiver, 1.6 W (DS12484)"
IC_DATASHEETS["st25r3916b.pdf"]="ST25R3916B  Enhanced ST25R3916, improved sensitivity, 1.6 W"
IC_DATASHEETS["st25r3917b.pdf"]="ST25R3917B  Reduced-feature ST25R3916B, 1.6 W (no NFC-V / card emul.)"
IC_DATASHEETS["st25r3919.pdf"]="ST25R3919   Automotive-grade ST25R3916, 1.6 W (AEC-Q100)"

# ST25R39xx — 2.2 W
IC_DATASHEETS["st25r3918.pdf"]="ST25R3918   High-performance multiprotocol NFC transceiver, 2.2 W"
IC_DATASHEETS["st25r3918b.pdf"]="ST25R3918B  Enhanced ST25R3918, improved sensitivity, 2.2 W"
IC_DATASHEETS["st25r3920.pdf"]="ST25R3920   Automotive-grade ST25R3918, 2.2 W (AEC-Q100)"

# ST25Rxxx third-generation — low-power reader
IC_DATASHEETS["st25r100.pdf"]="ST25R100    Low-power NFC reader (shares driver with ST25R200)"
IC_DATASHEETS["st25r200.pdf"]="ST25R200    Low-power multiprotocol NFC reader"

# ST25Rxxx third-generation — high-performance reader
IC_DATASHEETS["st25r300.pdf"]="ST25R300    High-performance NFC reader (shares driver with ST25R500)"
IC_DATASHEETS["st25rn300.pdf"]="ST25RN300   NFC-only variant of ST25R300"
IC_DATASHEETS["st25r500.pdf"]="ST25R500    High-performance multiprotocol NFC reader with DPO/CR"

# Explicit ordering (groups by generation / power class)
IC_ORDER=(
    # Legacy
    st25r95.pdf
    # ST25R39xx 1.6 W
    st25r3911b.pdf
    st25r3916.pdf
    st25r3916b.pdf
    st25r3917b.pdf
    st25r3919.pdf
    # ST25R39xx 2.2 W
    st25r3918.pdf
    st25r3918b.pdf
    st25r3920.pdf
    # ST25Rxxx low-power
    st25r100.pdf
    st25r200.pdf
    # ST25Rxxx high-performance
    st25r300.pdf
    st25rn300.pdf
    st25r500.pdf
)

# ── Eval-board data briefs ────────────────────────────────────────────────────
# Each entry: filename → human description  (chip in parentheses)
# Output directory: docs/boards/

declare -A BOARD_BRIEFS

BOARD_BRIEFS["x-nucleo-nfc03a1.pdf"]="X-NUCLEO-NFC03A1  ST25R95 NFC reader expansion board"
BOARD_BRIEFS["x-nucleo-nfc05a1.pdf"]="X-NUCLEO-NFC05A1  ST25R3911B NFC reader expansion board"
BOARD_BRIEFS["x-nucleo-nfc06a1.pdf"]="X-NUCLEO-NFC06A1  ST25R3916 NFC reader expansion board"
BOARD_BRIEFS["x-nucleo-nfc08a1.pdf"]="X-NUCLEO-NFC08A1  ST25R3916B / ST25R3918 NFC reader expansion board"
BOARD_BRIEFS["x-nucleo-nfc09a1.pdf"]="X-NUCLEO-NFC09A1  ST25R3918 2.2 W NFC reader expansion board"
BOARD_BRIEFS["x-nucleo-nfc12a1.pdf"]="X-NUCLEO-NFC12A1  ST25R500/ST25R300 NFC reader expansion board"

BOARD_ORDER=(
    x-nucleo-nfc03a1.pdf
    x-nucleo-nfc05a1.pdf
    x-nucleo-nfc06a1.pdf
    x-nucleo-nfc08a1.pdf
    x-nucleo-nfc09a1.pdf
    x-nucleo-nfc12a1.pdf
)

# ── Download helper ───────────────────────────────────────────────────────────

SUCCESS=0; SKIPPED=0; FAILED=0

fetch_one() {
    local dest="$1" url="$2" desc="$3"
    if [[ -f "$dest" ]]; then
        printf "  [skip]  %-30s  (already present)\n" "$(basename "$dest")"
        SKIPPED=$((SKIPPED + 1))
        return
    fi
    printf "  [fetch] %-30s  %s\n" "$(basename "$dest")" "$desc"
    if curl -fsSL --connect-timeout 15 --max-time 60 \
            -H "User-Agent: Mozilla/5.0" \
            -o "$dest" "$url"; then
        if [[ "$(head -c 4 "$dest" 2>/dev/null)" == "%PDF" ]]; then
            SIZE=$(du -k "$dest" | cut -f1)
            echo "                -> OK  (${SIZE} kB)"
            SUCCESS=$((SUCCESS + 1))
        else
            echo "                -> WARNING: not a valid PDF, removing"
            rm -f "$dest"
            FAILED=$((FAILED + 1))
        fi
    else
        echo "                -> FAILED  (curl error $?)"
        rm -f "$dest"
        FAILED=$((FAILED + 1))
    fi
}

# ── Main ──────────────────────────────────────────────────────────────────────

BOARDS_DIR="$SCRIPT_DIR/boards"
mkdir -p "$BOARDS_DIR"

echo "ST25R datasheet fetcher"
echo "Destination: $SCRIPT_DIR"
echo ""

# ── IC datasheets ─────────────────────────────────────────────────────────────
PREV_GROUP=""
for F in "${IC_ORDER[@]}"; do
    # Print group header when generation/class changes
    case "$F" in
        st25r95.pdf)                      GROUP="Legacy (ST25R95)" ;;
        st25r391[1-7]*.pdf|st25r3919.pdf) GROUP="ST25R39xx — 1.6 W" ;;
        st25r3918*.pdf|st25r3920.pdf)     GROUP="ST25R39xx — 2.2 W" ;;
        st25r100.pdf|st25r200.pdf)        GROUP="ST25Rxxx — low-power reader" ;;
        st25r300.pdf|st25rn300.pdf|st25r500.pdf) GROUP="ST25Rxxx — high-performance reader" ;;
        *)                                GROUP="Other" ;;
    esac
    if [[ "$GROUP" != "$PREV_GROUP" ]]; then
        echo "  ── $GROUP ──"
        PREV_GROUP="$GROUP"
    fi
    fetch_one "$SCRIPT_DIR/$F" "$DS_BASE/$F" "${IC_DATASHEETS[$F]}"
done

echo ""
echo "  ── X-NUCLEO eval board data briefs (→ docs/boards/) ──"
for F in "${BOARD_ORDER[@]}"; do
    fetch_one "$BOARDS_DIR/$F" "$DB_BASE/$F" "${BOARD_BRIEFS[$F]}"
done

echo ""
echo "Done.  success=$SUCCESS  skipped=$SKIPPED  failed=$FAILED"

if [[ $FAILED -gt 0 ]]; then
    echo ""
    echo "Some files could not be downloaded.  ST occasionally requires account"
    echo "login for certain documents.  Download them manually from"
    echo "https://www.st.com and place them in the appropriate directory."
    exit 1
fi

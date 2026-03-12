#!/usr/bin/env bash
# create-upstream-prs.sh
#
# Creates draft PRs to esphome/esphome and esphome/esphome-docs
# from JohnMcLear's forks with the ST25R component and documentation.
#
# Prerequisites:
#   - gh CLI installed and authenticated as JohnMcLear
#   - git configured with JohnMcLear credentials
#   - Run from the root of JohnMcLear/esphome_st25r repo
#
# Usage:
#   ./create-upstream-prs.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$SCRIPT_DIR"

echo "=== ST25R Upstream PR Creator ==="
echo ""

# Check prerequisites
if ! command -v gh &>/dev/null; then
    echo "ERROR: gh CLI not found. Install from https://cli.github.com/"
    exit 1
fi
if ! gh auth status &>/dev/null; then
    echo "ERROR: gh CLI not authenticated. Run: gh auth login"
    exit 1
fi

BRANCH_DATE=$(date +%Y%m%d)
ESPHOME_BRANCH="add-st25r3916-component-${BRANCH_DATE}"
DOCS_BRANCH="add-st25r3916-docs-${BRANCH_DATE}"

# ============================================================
# PART 1: esphome/esphome component PR
# ============================================================
echo "--- Part 1: Creating esphome/esphome PR ---"
echo ""

ESPHOME_FORK_DIR=$(mktemp -d)
echo "Cloning JohnMcLear/esphome into $ESPHOME_FORK_DIR ..."
git clone --depth=1 https://github.com/JohnMcLear/esphome.git "$ESPHOME_FORK_DIR"
cd "$ESPHOME_FORK_DIR"

echo "Fetching upstream dev branch..."
git remote add upstream https://github.com/esphome/esphome.git
git fetch --depth=1 upstream dev
git checkout -b "$ESPHOME_BRANCH" upstream/dev

echo "Copying component files..."
cp -r "$REPO_ROOT/esphome-pr/esphome/components/st25r" "esphome/components/"
cp -r "$REPO_ROOT/esphome-pr/esphome/components/st25r_spi" "esphome/components/"
cp -r "$REPO_ROOT/esphome-pr/esphome/components/st25r_i2c" "esphome/components/"

echo "Copying test files..."
cp -r "$REPO_ROOT/esphome-pr/tests/components/st25r_spi" "tests/components/"
cp -r "$REPO_ROOT/esphome-pr/tests/components/st25r_i2c" "tests/components/"

echo "Updating CODEOWNERS..."
# Insert st25r entries after pn7160_spi line (alphabetical order)
ST25R_CODEOWNERS="esphome/components/st25r/* @JohnMcLear\nesphome/components/st25r_i2c/* @JohnMcLear\nesphome/components/st25r_spi/* @JohnMcLear"
sed -i "/esphome\/components\/pn7160_spi/a \\$ST25R_CODEOWNERS" CODEOWNERS || \
    echo "" >> CODEOWNERS && echo -e "$ST25R_CODEOWNERS" >> CODEOWNERS

echo "Committing changes..."
git add esphome/components/st25r esphome/components/st25r_spi esphome/components/st25r_i2c
git add tests/components/st25r_spi tests/components/st25r_i2c
git add CODEOWNERS
git commit -m "Add ST25R NFC/RFID component (ST25R3916 family)"

echo "Pushing branch to JohnMcLear/esphome..."
git push origin "$ESPHOME_BRANCH"

echo "Creating draft PR to esphome/esphome..."
gh pr create \
    --repo "esphome/esphome" \
    --draft \
    --title "[WIP] Add ST25R3916/B NFC/RFID component" \
    --base "dev" \
    --head "JohnMcLear:$ESPHOME_BRANCH" \
    --body "$(cat <<'PR_BODY'
# What does this implement/fix?

Adds a new NFC/RFID component for the STMicroelectronics ST25R39xx family of NFC reader ICs.

Supported chips: ST25R3916, ST25R3916B, ST25R3917, ST25R3917B, ST25R3919, ST25R3920.

The component provides:
- SPI transport (`st25r_spi`)
- I²C transport (`st25r_i2c`)
- Binary sensor platform for UID-based tag detection
- `on_tag` and `on_tag_removed` triggers
- Health status binary sensor
- RF field strength sensor
- Configurable RF power and antenna tuning

**Pull request in esphome-docs with documentation:**
- esphome/esphome-docs#XXXX ← (update when docs PR is created)

## Types of changes

- [ ] Bugfix (non-breaking change which fixes an issue)
- [x] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Code quality improvements to existing code or addition of tests
- [ ] Other

## Test Environment

- [x] ESP32
- [x] ESP32 IDF
- [ ] ESP8266
- [ ] RP2040
- [ ] BK72xx
- [ ] RTL87xx

## Example entry for `config.yaml`:

```yaml
# SPI example
spi:
  clk_pin: GPIO18
  miso_pin: GPIO19
  mosi_pin: GPIO23

st25r_spi:
  cs_pin: GPIO5
  irq_pin: GPIO4
  update_interval: 1s

binary_sensor:
  - platform: st25r
    uid: 74-10-37-94
    name: "ST25R NFC Tag"
```

## Checklist:
- [ ] The code change is tested and works locally.
- [x] Tests have been added to verify that the new code works (under `tests/` folder).

**⚠️ WIP Notes:**
- The `update()` function currently uses blocking `delay()` calls that need to be converted to a proper non-blocking state machine.
- I²C transport has been implemented but needs hardware verification.
- Mifare Classic and ISO14443B support are not yet implemented.
PR_BODY
)"

echo ""
echo "✓ Component PR created!"
cd "$REPO_ROOT"
rm -rf "$ESPHOME_FORK_DIR"

# ============================================================
# PART 2: esphome/esphome-docs documentation PR
# ============================================================
echo ""
echo "--- Part 2: Creating esphome/esphome-docs PR ---"
echo ""

DOCS_FORK_DIR=$(mktemp -d)
echo "Cloning JohnMcLear/esphome-docs into $DOCS_FORK_DIR ..."
git clone --depth=1 https://github.com/JohnMcLear/esphome-docs.git "$DOCS_FORK_DIR"
cd "$DOCS_FORK_DIR"

echo "Fetching upstream current branch..."
git remote add upstream https://github.com/esphome/esphome-docs.git
git fetch --depth=1 upstream current
git checkout -b "$DOCS_BRANCH" upstream/current

echo "Copying documentation files..."
cp "$REPO_ROOT/esphome-docs-pr/src/content/docs/components/binary_sensor/st25r.mdx" \
   "src/content/docs/components/binary_sensor/"

echo "Updating components/index.mdx..."
# Add st25r entry after rc522 (alphabetical order by chip name)
python3 - <<'PYEOF'
import re

with open('src/content/docs/components/index.mdx', 'r') as f:
    content = f.read()

# Add ST25R entry after RC522 in the NFC/RFID table
old_entry = '  ["RC522", "/components/binary_sensor/rc522/", "rc522.jpg"],'
new_entry = '  ["RC522", "/components/binary_sensor/rc522/", "rc522.jpg"],\n  ["ST25R", "/components/binary_sensor/st25r/", "st25r.jpg"],'

if old_entry in content:
    content = content.replace(old_entry, new_entry)
    print("Updated index.mdx")
else:
    print("WARNING: Could not find RC522 entry to insert after. Please update index.mdx manually.")

with open('src/content/docs/components/index.mdx', 'w') as f:
    f.write(content)
PYEOF

echo "Committing documentation..."
git add src/content/docs/components/binary_sensor/st25r.mdx
git add src/content/docs/components/index.mdx
git commit -m "Add ST25R NFC/RFID component documentation"

echo "Pushing branch to JohnMcLear/esphome-docs..."
git push origin "$DOCS_BRANCH"

echo "Creating draft PR to esphome/esphome-docs..."
gh pr create \
    --repo "esphome/esphome-docs" \
    --draft \
    --title "[WIP] Add ST25R3916/B NFC/RFID component documentation" \
    --base "current" \
    --head "JohnMcLear:$DOCS_BRANCH" \
    --body "$(cat <<'PR_BODY'
## Description

Adds documentation for the new ST25R NFC/RFID component (ST25R3916 family).

**Related pull request in esphome:**
- esphome/esphome#XXXX ← (update with component PR number)

## Checklist

- [ ] I am merging into `next` because this is new documentation that has a matching pull-request in [esphome](https://github.com/esphome/esphome) as linked above.
  or
- [x] I am merging into `current` because this is a fix, change and/or adjustment in the current documentation and is not for a new component or feature.

- [x] Link added in `/src/content/docs/components/index.mdx` when creating new documents for new components or cookbook.

**⚠️ WIP Notes:**
- The component PR in esphome/esphome is still in draft state.
- Documentation will be updated once the component PR is finalized.
PR_BODY
)"

echo ""
echo "✓ Documentation PR created!"
cd "$REPO_ROOT"
rm -rf "$DOCS_FORK_DIR"

echo ""
echo "=== All done! ==="
echo ""
echo "Remember to:"
echo "1. Update the docs PR number in the component PR body"
echo "2. Update the component PR number in the docs PR body"
echo "3. Request a component image from esphomebot: @esphomebot generate image st25r"

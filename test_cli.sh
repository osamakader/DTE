#!/bin/bash

# Device Tree Explorer CLI smoke test
#
# Runs the main dte-cli commands end to end in a temporary directory and
# exits non-zero if any check fails. The full test suite is run with ctest
# (see README); this script is a quick manual check of an installed or
# freshly built binary.
#
# Usage: ./test_cli.sh [path/to/dte-cli]

set -u

CLI="${1:-./build/bin/dte-cli}"

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'
if [ ! -t 1 ] || [ -n "${NO_COLOR:-}" ]; then
    RED='' GREEN='' BLUE='' NC=''
fi

if [ ! -x "$CLI" ]; then
    echo -e "${RED}[FAIL]${NC} CLI executable not found: $CLI (build first: ./build.sh)"
    exit 1
fi
CLI="$(cd "$(dirname "$CLI")" && pwd)/$(basename "$CLI")"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

PASSED=0
FAILED=0

# check NAME EXPECTED_EXIT COMMAND...
check() {
    local name="$1" expected="$2"
    shift 2
    echo -e "${BLUE}[TEST]${NC} $name"
    "$@" >out.txt 2>err.txt
    local rc=$?
    if [ "$rc" -eq "$expected" ]; then
        echo -e "${GREEN}[PASS]${NC} $name"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}[FAIL]${NC} $name (exit $rc, expected $expected)"
        sed 's/^/    /' err.txt
        FAILED=$((FAILED + 1))
    fi
}

# check_output NAME PATTERN FILE: FILE must contain PATTERN
check_output() {
    if grep -q -- "$2" "$3"; then
        echo -e "${GREEN}[PASS]${NC} $1"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}[FAIL]${NC} $1 ('$2' not found in $3)"
        FAILED=$((FAILED + 1))
    fi
}

cat > board.dts << 'EOF'
/dts-v1/;

/ {
    compatible = "test,device";
    model = "Test Device";
    #address-cells = <1>;
    #size-cells = <1>;

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;

        cpu@0 {
            device_type = "cpu";
            compatible = "arm,cortex-a53";
            reg = <0>;
            clock-frequency = <1200000000>;
        };
    };

    memory@80000000 {
        device_type = "memory";
        reg = <0x80000000 0x40000000>;
    };
};
EOF
sed 's/"Test Device"/"Test Device rev B"/' board.dts > changed.dts

check "version" 0 "$CLI" --version
check "help" 0 "$CLI" --help
check "unknown command fails" 1 "$CLI" no-such-command
check "missing file fails" 1 "$CLI" info missing.dtb
check "info" 0 "$CLI" info board.dts
check_output "info counts nodes" "Total nodes: 4" out.txt
check "validate" 0 "$CLI" validate board.dts
check "list" 0 "$CLI" list board.dts
check_output "list shows decimal value correctly" "clock-frequency = <0x47868c00>" out.txt
check "search" 0 "$CLI" search board.dts cpu
check "convert dts -> dtb" 0 "$CLI" convert board.dts board.dtb
check "convert dtb -> dts" 0 "$CLI" convert board.dtb roundtrip.dts
check "diff identical (exit 0)" 0 "$CLI" diff board.dts board.dtb
check "diff changed (exit 1)" 1 "$CLI" diff board.dts changed.dts
check_output "diff shows new value" '+ "Test Device rev B"' out.txt
check "export json" 0 "$CLI" export board.dts json board.json
check_output "json has model" '"model": "Test Device"' board.json
check "export yaml" 0 "$CLI" export board.dts yaml board.yaml
check "export dtb" 0 "$CLI" export board.dts dtb exported.dtb
check "exported dtb equals source" 0 "$CLI" diff board.dts exported.dtb

echo
if [ "$FAILED" -eq 0 ]; then
    echo -e "${GREEN}All $PASSED checks passed${NC}"
    exit 0
fi
echo -e "${RED}$FAILED of $((PASSED + FAILED)) checks failed${NC}"
exit 1

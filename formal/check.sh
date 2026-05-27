#!/usr/bin/env bash
# Verifica las specs TLA+ de Aleph con TLC.
#
# Uso:
#   formal/check.sh                # corre ambas specs
#   formal/check.sh scene_graph    # sólo la base
#   formal/check.sh dpo_rules      # sólo las reglas
#
# Requiere `tlc` (TLA+ toolbox CLI) en PATH. Si no lo tenés:
#   curl -L https://github.com/tlaplus/tlaplus/releases/latest/download/tla2tools.jar -o /tmp/tla2tools.jar
#   echo 'alias tlc="java -cp /tmp/tla2tools.jar tlc2.TLC"' >> ~/.bashrc

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

run_with_cfg() {
    local spec="$1"
    local cfg="$2"
    echo "─── ${spec} (cfg=${cfg}.cfg) ───────────────────────────────────"
    if command -v tlc >/dev/null 2>&1; then
        tlc -workers 4 -deadlock "${spec}.tla" -config "${cfg}.cfg"
    elif [[ -f "${TLA_TOOLS:-/tmp/tla2tools.jar}" ]]; then
        java -cp "${TLA_TOOLS:-/tmp/tla2tools.jar}" tlc2.TLC \
            -workers 4 -deadlock "${spec}.tla" -config "${cfg}.cfg"
    else
        echo "ERROR: tlc no encontrado en PATH y \$TLA_TOOLS no apunta a tla2tools.jar"
        echo "Descargar con:"
        echo "  curl -L https://github.com/tlaplus/tlaplus/releases/latest/download/tla2tools.jar -o /tmp/tla2tools.jar"
        return 1
    fi
}

run_one() {
    local spec="$1"
    case "$spec" in
        sheaf_h0)
            # Base (empty fixture) + 3 fixture configs that pin dim H^0
            # to a hand-computed value.
            run_with_cfg sheaf_h0 sheaf_h0
            run_with_cfg sheaf_h0 sheaf_h0_fixture1
            run_with_cfg sheaf_h0 sheaf_h0_fixture2
            run_with_cfg sheaf_h0 sheaf_h0_fixture3
            ;;
        *)
            run_with_cfg "$spec" "$spec"
            ;;
    esac
}

# TLA+ checks: run them but don't abort the whole script if tlc is missing
# (so the Coq pass below still runs even on machines without tlc).
set +e
if [[ $# -eq 0 ]]; then
    run_one scene_graph
    run_one dpo_rules
    run_one sheaf_h0
else
    for s in "$@"; do
        run_one "$s"
    done
fi
set -e

# Coq proofs (M3d). Skipped silently if coqc not on PATH.
if command -v coqc >/dev/null 2>&1; then
    echo ""
    echo "─── Coq proofs ─────────────────────────────────────────────"
    (cd "$ROOT/coq" && ./build.sh)
    echo "All Coq proofs verified."
else
    echo ""
    echo "(skipping Coq proofs: coqc not on PATH)"
fi

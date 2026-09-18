#!/usr/bin/env bash
set -euo pipefail

# Validate the built plugins with pluginval.
#
#   ./scripts/run_pluginval.sh                 # validate all bundles found in build/
#   ./scripts/run_pluginval.sh path/to/x.vst3  # validate specific bundle(s)
#
# Env:
#   PLUGINVAL_BIN         path to the pluginval binary (default: PATH lookup,
#                         then /Applications/pluginval.app)
#   PLUGINVAL_STRICTNESS  strictness level 1-10 (default: 5)

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

PLUGINVAL="${PLUGINVAL_BIN:-}"
if [[ -z "${PLUGINVAL}" ]]; then
  if command -v pluginval >/dev/null 2>&1; then
    PLUGINVAL="pluginval"
  elif [[ -x "/Applications/pluginval.app/Contents/MacOS/pluginval" ]]; then
    PLUGINVAL="/Applications/pluginval.app/Contents/MacOS/pluginval"
  fi
fi

STRICTNESS="${PLUGINVAL_STRICTNESS:-5}"

if [[ "${PLUGINVAL}" == */* ]]; then
  if [[ ! -x "${PLUGINVAL}" ]]; then
    echo "pluginval is not executable: ${PLUGINVAL}" >&2
    exit 2
  fi
else
  if ! command -v "${PLUGINVAL}" >/dev/null 2>&1; then
    echo "pluginval not found; set PLUGINVAL_BIN or install pluginval" >&2
    exit 2
  fi
fi

if ! [[ "${STRICTNESS}" =~ ^([1-9]|10)$ ]]; then
  echo "PLUGINVAL_STRICTNESS must be an integer from 1 to 10 (got ${STRICTNESS})" >&2
  exit 2
fi

if (( $# > 0 )); then
  BUNDLES=("$@")
else
  BUNDLES=(
    "${PROJECT_DIR}/build/Neve1073_artefacts/Release/VST3/EON 1073 GOD.vst3"
    "${PROJECT_DIR}/build/Neve1073_artefacts/Release/AU/EON 1073 GOD.component"
    "${PROJECT_DIR}/build/Avalon737_artefacts/Release/VST3/EON 737 GOD.vst3"
    "${PROJECT_DIR}/build/Avalon737_artefacts/Release/AU/EON 737 GOD.component"
  )
fi

failed=()
skipped=()
for bundle in "${BUNDLES[@]}"; do
  if [[ ! -d "${bundle}" ]]; then
    echo ">> SKIP (not built): ${bundle}"
    skipped+=("${bundle}")
    continue
  fi
  echo ">> Validating: ${bundle} (strictness ${STRICTNESS})"
  if "${PLUGINVAL}" --strictness-level "${STRICTNESS}" --validate "${bundle}"; then
    echo ">> PASS: ${bundle}"
  else
    echo ">> FAIL: ${bundle}"
    failed+=("${bundle}")
  fi
done

echo
echo "== Summary =="
echo "  passed : $(( ${#BUNDLES[@]} - ${#failed[@]} - ${#skipped[@]} ))"
echo "  failed : ${#failed[@]}"
echo "  skipped: ${#skipped[@]}"
for b in "${failed[@]}"; do echo "    FAIL ${b}"; done

(( ${#failed[@]} == 0 ))

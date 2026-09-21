#!/bin/sh
set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/.." && pwd)

if [ "$#" -gt 1 ]; then
    echo "usage: $0 [source_dir]" >&2
    exit 2
fi

if [ "$#" -eq 1 ]; then
    source_dir=$1
else
    source_dir="$repo_root/../eon_dsp"
fi

if ! source_root=$(git -C "$source_dir" rev-parse --show-toplevel 2>/dev/null); then
    echo "error: source_dir is not a Git worktree: $source_dir" >&2
    exit 1
fi

if ! source_head=$(git -C "$source_root" rev-parse --verify 'HEAD^{commit}'); then
    echo "error: could not resolve source HEAD in $source_root" >&2
    exit 1
fi

required_headers="Adaa.h Measure.h Oversampling.h Rng.h RtGuard.h Solvers.h Stages.h Transformer.h Triode.h Wdf.h Zdf.h"
for header in $required_headers; do
    source_header="$source_root/Dsp/$header"
    if ! git -C "$source_root" ls-files --error-unmatch "Dsp/$header" >/dev/null 2>&1 ||
       [ ! -f "$source_header" ] || [ ! -r "$source_header" ]; then
        echo "error: required tracked source header is missing or unreadable: Dsp/$header" >&2
        exit 1
    fi
    if ! git -C "$source_root" diff --quiet "$source_head" -- "Dsp/$header"; then
        echo "error: source header differs from recorded HEAD $source_head: Dsp/$header" >&2
        exit 1
    fi
done

destination="$repo_root/eon_dsp"
mkdir -p "$destination/Dsp"
stage_dir=$(mktemp -d "$destination/.sync.XXXXXX")

cleanup()
{
    for header in $required_headers REVISION; do
        if [ -f "$stage_dir/$header" ]; then
            rm "$stage_dir/$header"
        fi
    done
    rmdir "$stage_dir" 2>/dev/null || :
}

trap cleanup 0
trap 'exit 1' HUP INT TERM

for header in $required_headers; do
    cp "$source_root/Dsp/$header" "$stage_dir/$header"
done
printf '%s\n' "$source_head" > "$stage_dir/REVISION"

for header in $required_headers; do
    mv "$stage_dir/$header" "$destination/Dsp/$header"
done
mv "$stage_dir/REVISION" "$destination/REVISION"

printf 'Synced eon_dsp headers from %s at %s\n' "$source_root" "$source_head"

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

required_headers="Adaa.h Measure.h Oversampling.h Rate.h Rng.h RtGuard.h Solvers.h Stages.h Transformer.h Triode.h Wdf.h Zdf.h"
for header in $required_headers; do
    object_spec="$source_head:Dsp/$header"
    if ! object_type=$(git -C "$source_root" cat-file -t "$object_spec" 2>/dev/null) ||
       [ "$object_type" != blob ]; then
        echo "error: required source HEAD blob is missing: Dsp/$header" >&2
        exit 1
    fi
done
if ! object_type=$(git -C "$source_root" cat-file -t "$source_head:NOTES.md" 2>/dev/null) ||
   [ "$object_type" != blob ]; then
    echo "error: required source HEAD blob is missing: NOTES.md" >&2
    exit 1
fi

destination="$repo_root/eon_dsp"
transaction_dir=$(mktemp -d "$repo_root/.eon_dsp.sync.XXXXXX")
stage_dir="$transaction_dir/eon_dsp"
backup_dir="$transaction_dir/backup"
restore_failed=0

cleanup()
{
    exit_code=$?
    trap - 0 HUP INT TERM
    trap '' HUP INT TERM

    if [ -d "$backup_dir" ]; then
        if [ ! -e "$destination" ]; then
            if mv "$backup_dir" "$destination"; then
                backup_dir=
            else
                echo "error: could not restore the previous DSP snapshot from $backup_dir" >&2
                restore_failed=1
                exit_code=1
            fi
        elif ! rm -rf "$backup_dir"; then
            echo "error: could not remove the previous DSP snapshot at $backup_dir" >&2
            restore_failed=1
            exit_code=1
        fi
    fi

    if [ "$restore_failed" -eq 0 ] && [ -d "$transaction_dir" ]; then
        if ! rm -rf "$transaction_dir"; then
            echo "error: could not remove sync staging directory $transaction_dir" >&2
            exit_code=1
        fi
    elif [ "$restore_failed" -ne 0 ]; then
        echo "recovery data retained at $transaction_dir" >&2
    fi

    exit "$exit_code"
}

trap cleanup 0
trap 'exit 1' HUP INT TERM

if [ -L "$destination" ]; then
    echo "error: destination must not be a symlink: $destination" >&2
    exit 1
fi
if [ -e "$destination" ] && [ ! -d "$destination" ]; then
    echo "error: destination exists but is not a directory: $destination" >&2
    exit 1
fi

mkdir -p "$stage_dir/Dsp"

# Copy every header the source tree carries, not only the ones named above.
# The list above is a floor — it catches a source tree that is missing
# something the snapshot needs — but a header *added* upstream has to flow
# through on its own. Rate.h arrived in v0.1.7 while the list still ended at
# Zdf.h, and the result was a snapshot whose Stages.h and Transformer.h both
# included a file that had not been copied, so nothing compiled.
git -C "$source_root" ls-tree --name-only "$source_head" Dsp/ \
    | while IFS= read -r path; do
        case "$path" in
            *.h) git -C "$source_root" cat-file blob "$source_head:$path" > "$stage_dir/$path" ;;
        esac
      done
git -C "$source_root" cat-file blob "$source_head:NOTES.md" > "$stage_dir/NOTES.md"
printf '%s\n' "$source_head" > "$stage_dir/REVISION"

if [ -d "$destination" ]; then
    mv "$destination" "$backup_dir"
fi
mv "$stage_dir" "$destination"

printf 'Synced eon_dsp headers from %s at %s\n' "$source_root" "$source_head"

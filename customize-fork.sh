#!/usr/bin/env bash
#
# customize-fork.sh
#
# Why this script exists:
# This fork carries a small set of intentional deviations from upstream RetroArch.
# Every time we merge/rebase upstream changes, those files come back.
# Rather than manually re-deleting them and trying to remember exactly what we did
# last time, this script is the single source of truth for the patch: run it after
# pulling upstream and it reproduces our customizations deterministically.
#
# Usage:
#   ./customize-fork.sh
#
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

GITHUB_DIR=".github"
WORKFLOWS_DIR="${GITHUB_DIR}/workflows"
KEEP_WORKFLOW="Android.yml"

# Remove GitHub's sponsorship/funding prompt — we don't use it in this fork.
funding_file="${GITHUB_DIR}/FUNDING.yml"
if [ -f "$funding_file" ]; then
    echo "Removing ${funding_file}"
    rm -f "$funding_file"
fi

# We only build/ship the Android frontend, so strip every other CI workflow
# and keep just Android.yml.
if [ -d "$WORKFLOWS_DIR" ]; then
    for workflow in "$WORKFLOWS_DIR"/*; do
        filename="$(basename "$workflow")"
        if [ "$filename" != "$KEEP_WORKFLOW" ]; then
            echo "Removing ${workflow}"
            rm -f "$workflow"
        fi
    done
fi

echo "Done. .github now only contains our customizations (Android.yml workflow, no FUNDING.yml)."
 
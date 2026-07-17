#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: CC-BY-4.0
#
# Prepare the docs/ tree for the published site build.
#
# The docs/ Markdown sources use repo-relative links (e.g. `../lib/tests/foo.cpp`,
# `../.devcontainer/Dockerfile`) so they resolve correctly when browsing the
# source on GitHub. Those links don't exist in the published documentation
# tree, so for the site build we rewrite them in-place to absolute GitHub URLs.
#
# This script:
#   1. Generates docs/index.md from README.md (the single source of truth for
#      the landing page), rewriting README's docs/-prefixed and repo-root
#      links so they resolve from inside docs/.
#   2. Rewrites any link in docs/*.md that escapes the docs/ tree (i.e. starts
#      with `../`) to an absolute github.com/.../<ref>/... URL.
#
# Intended to run in CI on a fresh checkout; the in-place edits to docs/*.md
# are not meant to be committed.

set -euo pipefail

# Derive the repo slug from the GitHub Actions context so the script works
# unchanged on forks (e.g. peterbrightwell/mxl during testing). Fall back to
# the upstream slug for ad-hoc local runs.
REPO_SLUG="${GITHUB_REPOSITORY:-dmf-mxl/mxl}"
OWNER="${REPO_SLUG%%/*}"
REPO="${REPO_SLUG##*/}"
# Project Pages base URL for this repo, e.g. https://peterbrightwell.github.io/mxl
SITE_BASE="https://${OWNER}.github.io/${REPO}"
# Use BUILD_REF if the workflow set one (covers dispatch with an input ref),
# else the ref the run was triggered on, else fall back to main.
REF="${BUILD_REF:-${GITHUB_REF_NAME:-main}}"
REPO_URL="https://github.com/${REPO_SLUG}/blob/${REF}"

if [[ ! -f README.md ]]; then
    echo "error: README.md not found (run from repo root)" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# 1. Landing page: README.md -> docs/index.md
# ---------------------------------------------------------------------------
sed -E \
    -e "s#\]\(docs/([^)]+)\)#](\1)#g" \
    -e "s#\]\(\./?LICENSE\.txt\)#](${REPO_URL}/LICENSE.txt)#g" \
    -e "s#\]\(LICENSE\.txt\)#](${REPO_URL}/LICENSE.txt)#g" \
    -e "s#\]\(CONTRIBUTING\.md\)#](${REPO_URL}/CONTRIBUTING.md)#g" \
    -e "s#\]\(SECURITY\.md\)#](${REPO_URL}/SECURITY.md)#g" \
    -e "s#\]\(CODE_OF_CONDUCT\.md\)#](${REPO_URL}/CODE_OF_CONDUCT.md)#g" \
    -e "s#\]\(GOVERNANCE/([^)]+)\)#](${REPO_URL}/GOVERNANCE/\1)#g" \
    -e "s#\]\(examples/([^)]+)\)#](${REPO_URL}/examples/\1)#g" \
    -e "s#https://github.com/${REPO_SLUG}/blob/[0-9a-f]+/docs/([^)\" ]+)#\1#g" \
    README.md > docs/index.md

echo "Generated docs/index.md from README.md"

# ---------------------------------------------------------------------------
# 2. Rewrite docs/*.md links that escape the docs/ tree to absolute GitHub URLs
#
# Matches Markdown link targets of the form `](../<path>)`. The captured path
# may contain further `../` segments (collapsed by the URL itself).
# ---------------------------------------------------------------------------
shopt -s nullglob
for f in docs/*.md; do
    [[ "${f}" == "docs/index.md" ]] && continue
    sed -i.bak -E "s#\]\(\.\./([^)]+)\)#](${REPO_URL}/\1)#g" "${f}"
    rm -f "${f}.bak"
done

echo "Rewrote ../ links in docs/*.md to ${REPO_URL}/..."

# ---------------------------------------------------------------------------
# 3. Template the API Reference page so its links point at the matching
#    Doxygen build (e.g. /mxl/api/v1.1.0/) on the current Pages site, and at
#    the current repo on github.com.
# ---------------------------------------------------------------------------
API_REF_PAGE="docs/API Reference.md"
if [[ -f "${API_REF_PAGE}" ]]; then
    sed -i.bak \
        -e "s#__BUILD_REF__#${REF}#g" \
        -e "s#__SITE_BASE__#${SITE_BASE}#g" \
        -e "s#__REPO_SLUG__#${REPO_SLUG}#g" \
        "${API_REF_PAGE}"
    rm -f "${API_REF_PAGE}.bak"
    echo "Templated ${API_REF_PAGE} for ref=${REF}, site=${SITE_BASE}"
fi

# ---------------------------------------------------------------------------
# 4. Template zensical.toml so the header repo link (repo_url / repo_name)
#    points at the fork or upstream that produced this build. The same
#    __REPO_SLUG__ placeholder is used elsewhere (see step 3).
# ---------------------------------------------------------------------------
if [[ -f zensical.toml ]]; then
    sed -i.bak -e "s#__REPO_SLUG__#${REPO_SLUG}#g" zensical.toml
    rm -f zensical.toml.bak
    echo "Templated zensical.toml for repo=${REPO_SLUG}"
fi

#!/usr/bin/env bash
#
# Download the C/C++ dependency archives listed in dependency/source/manifest.txt.
# Runs on the host (not in the container) and only fetches what is missing, so
# archives that had to be copied in by hand -- private repos, air-gapped
# machines -- are never touched.
#
# Usage:
#   scripts/wheel/fetch_deps.sh [--verify] [--force]
#
#   --verify  re-check the sha256 of archives that are already present
#   --force   re-download everything
#
# Environment:
#   DFTRACER_DEPS_DIR      archive directory (default <repo>/dependency/source)
#   DFTRACER_DEPS_MIRROR   directory or base URL to fetch from instead of the
#                          manifest URL (useful when GitHub is unreachable)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
DEPS_DIR="${DFTRACER_DEPS_DIR:-$PROJECT_DIR/dependency/source}"
MANIFEST="$DEPS_DIR/manifest.txt"
MIRROR="${DFTRACER_DEPS_MIRROR:-}"

VERIFY=0
FORCE=0
for arg in "$@"; do
  case "$arg" in
    --verify) VERIFY=1 ;;
    --force) FORCE=1 ;;
    -h | --help)
      sed -n '2,25p' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *)
      echo "fetch_deps.sh: unknown argument '$arg'" >&2
      exit 2
      ;;
  esac
done

[ -f "$MANIFEST" ] || {
  echo "fetch_deps.sh: manifest not found: $MANIFEST" >&2
  exit 1
}

check_sha() {
  local file="$1" want="$2"
  [ "$want" = "-" ] && return 0
  local got
  got="$(sha256sum "$file" | awk '{print $1}')"
  if [ "$got" != "$want" ]; then
    echo "  sha256 mismatch for $(basename "$file")" >&2
    echo "    expected $want" >&2
    echo "    actual   $got" >&2
    return 1
  fi
  return 0
}

missing=0
while read -r name archive sha url; do
  case "${name:-#}" in '' | '#'*) continue ;; esac

  dest="$DEPS_DIR/$archive"

  if [ -f "$dest" ] && [ "$FORCE" -eq 0 ]; then
    if [ "$VERIFY" -eq 1 ]; then
      echo "[$name] verifying $archive"
      check_sha "$dest" "$sha" || missing=1
    else
      echo "[$name] present: $archive"
    fi
    continue
  fi

  if [ "$url" = "-" ]; then
    echo "[$name] MISSING $archive" >&2
    echo "  this dependency has no public download and must be committed to" >&2
    echo "  $DEPS_DIR" >&2
    missing=1
    continue
  fi

  # git+<url>@<ref>: cloned here on the runner, which can reach czgitlab, and
  # handed to the wheel build as an archive, which cannot.
  case "$url" in
    git+*)
      repo="${url#git+}"
      ref="${repo##*@}"
      repo="${repo%@*}"
      echo "[$name] cloning $ref"
      tmp="$dest.part"
      work="$(mktemp -d)"
      if git clone --quiet --depth 1 --branch "$ref" "$repo" "$work/src" 2>/dev/null &&
        git -C "$work/src" archive --format=tar.gz \
          --prefix="${archive%.tar.gz}/" -o "$tmp" HEAD; then
        rm -rf "$work"
        if check_sha "$tmp" "$sha"; then
          mv "$tmp" "$dest"
        else
          rm -f "$tmp"
          missing=1
        fi
      else
        rm -rf "$work" "$tmp"
        echo "  FAILED to clone $repo at $ref" >&2
        missing=1
      fi
      continue
      ;;
  esac

  src="$url"
  if [ -n "$MIRROR" ]; then
    if [ -d "$MIRROR" ]; then
      src="$MIRROR/$archive"
    else
      src="${MIRROR%/}/$archive"
    fi
  fi

  echo "[$name] fetching $archive"
  tmp="$dest.part"
  if [ -f "$src" ]; then
    cp "$src" "$tmp"
  elif ! curl -fsSL --retry 3 -o "$tmp" "$src"; then
    rm -f "$tmp"
    echo "  FAILED to download $src" >&2
    echo "  commit $archive to $DEPS_DIR (private repo / no network)" >&2
    missing=1
    continue
  fi

  if ! check_sha "$tmp" "$sha"; then
    rm -f "$tmp"
    missing=1
    continue
  fi
  mv "$tmp" "$dest"
done <"$MANIFEST"

if [ "$missing" -ne 0 ]; then
  echo "fetch_deps.sh: one or more dependencies are missing or corrupt" >&2
  exit 1
fi

echo "fetch_deps.sh: all dependencies available in $DEPS_DIR"

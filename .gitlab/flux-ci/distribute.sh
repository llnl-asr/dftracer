#!/bin/bash
# Publish wheels and HTML docs to the dldl workspace:
#   pip install dftracer --find-links $DIST_ROOT/wheels        (tagged release)
#   pip install --pre dftracer --find-links $DIST_ROOT/wheels  (develop build)
set -eo pipefail
cd "$CI_PROJECT_DIR"

# The wheels directory is shared with the other dftracer packages, so one
# --find-links resolves a build and its dependencies together. Everything here
# is therefore scoped to $PKG: another package's files are not ours to prune.
PKG=dftracer
DIST_ROOT="${DFTRACER_DIST_ROOT:-/usr/workspace/dldl/dftracer/distributions}"
KEEP_PRERELEASES="${DFTRACER_DIST_KEEP:-3}"
WHEEL_DIR="${DIST_ROOT}/wheels"
DOC_DIR="${DIST_ROOT}/docs/${PKG}"

# The login umask is 0077, which would publish files only the account that
# built them can open.
umask 0007

ls wheelhouse/${PKG}-*.whl >/dev/null 2>&1 || { echo "ERROR: no ${PKG} wheels in wheelhouse/"; exit 1; }
version=$(basename "$(ls wheelhouse/${PKG}-*.whl | head -1)" | cut -d- -f2)
[ -n "${version}" ] || { echo "ERROR: could not read version from wheel name"; exit 1; }
echo "publishing ${version} to ${DIST_ROOT}"

# setgid carries the group to new files. Only an owner may chmod, so a
# directory another member created is left alone; it inherited the bit.
for d in "${DIST_ROOT}" "${DIST_ROOT}/docs" "${WHEEL_DIR}" "${DOC_DIR}"; do
  mkdir -p "$d"
  [ -g "$d" ] || chmod g+s "$d" 2>/dev/null ||
    echo "WARNING: $d is not setgid and $(id -un) does not own it"
  [ -w "$d" ] || { echo "ERROR: $d is not writable by $(id -un)"; exit 1; }
done
# Same directory as the wheels: --no-binary ignores wheels, so the tarball
# only resolves if it is on the --find-links path too.
cp -f wheelhouse/${PKG}-*.whl "${WHEEL_DIR}/"
cp -f wheelhouse/${PKG}-*.tar.gz "${WHEEL_DIR}/" 2>/dev/null || true

if [ -d public ]; then
  rm -rf "${DOC_DIR}/${version}.tmp"
  cp -r public "${DOC_DIR}/${version}.tmp"
  rm -rf "${DOC_DIR}/${version}"
  mv "${DOC_DIR}/${version}.tmp" "${DOC_DIR}/${version}"
else
  echo "WARNING: no public/ directory, skipping docs"
fi

# Prune by version, never a tagged release. sort -V is not PEP 440 ordering.
python3 - "$WHEEL_DIR" "$DOC_DIR" "$KEEP_PRERELEASES" "$PKG" <<'PYEOF'
import os, re, sys, shutil

wheel_dir, doc_dir, keep, pkg = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]

def key(v):
    # (release tuple, is_final, post, dev) - enough for X.Y.Z[.postN][.devN]
    m = re.match(r"^(\d+(?:\.\d+)*)(?:\.post(\d+))?(?:\.dev(\d+))?$", v)
    if not m:
        return ((0,), 0, 0, 0)
    rel = tuple(int(p) for p in m.group(1).split("."))
    return (rel, 0 if m.group(3) else 1, int(m.group(2) or 0), int(m.group(3) or 0))

def prerelease(v):
    # Off-tag builds are .postN.dev0; a tagged release is the bare X.Y.Z.
    return ".dev" in v

# Grouped by version so an sdist never outlives its wheels.
wheel_versions = {}
for f in os.listdir(wheel_dir):
    if not f.startswith(pkg + "-"):
        continue
    if f.endswith(".whl"):
        v = f.split("-")[1]
    elif f.endswith(".tar.gz"):
        v = f[: -len(".tar.gz")].split("-", 1)[1]
    else:
        continue
    wheel_versions.setdefault(v, []).append(f)

doc_versions = [d for d in os.listdir(doc_dir)
                if os.path.isdir(os.path.join(doc_dir, d))
                and not os.path.islink(os.path.join(doc_dir, d))]

stale = sorted((v for v in wheel_versions if prerelease(v)), key=key)[:-keep or None]
for v in stale:
    for f in wheel_versions[v]:
        os.remove(os.path.join(wheel_dir, f))
    print(f"pruned {v}")

stale_docs = sorted((v for v in doc_versions if prerelease(v)), key=key)[:-keep or None]
for v in stale_docs:
    shutil.rmtree(os.path.join(doc_dir, v))
    print(f"pruned docs {v}")

# latest -> newest tagged, dev -> newest prerelease
remaining = [d for d in os.listdir(doc_dir)
             if os.path.isdir(os.path.join(doc_dir, d))
             and not os.path.islink(os.path.join(doc_dir, d))]
for link, pool in (("latest", [v for v in remaining if not prerelease(v)]),
                   ("dev", [v for v in remaining if prerelease(v)])):
    if not pool:
        continue
    target = max(pool, key=key)
    path = os.path.join(doc_dir, link)
    if os.path.islink(path) or os.path.exists(path):
        os.remove(path)
    os.symlink(target, path)
    print(f"{link} -> {target}")
PYEOF

# Repairs leftovers from older runs. chmod needs ownership, not group write,
# so it must not fail the publish on another member's files.
chmod -R g+rwX "${WHEEL_DIR}" "${DOC_DIR}" 2>/dev/null || true
echo "wheels available:"
ls -1 "${WHEEL_DIR}" | sed 's/^/  /'

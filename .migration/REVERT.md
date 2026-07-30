# Reverting a Migration (back to GitHub-only)

The migration is additive: it only adds a `gitlab` remote, a `.gitlab-ci.yml` (+ `pages` job), and a `gitlab-migration` branch. Nothing on GitHub is ever changed, so reverting is just removing the additions.

## Per-project revert steps

Run inside `<project>/`:

```bash
# 1. Remove the GitLab remote (local only)
git remote remove gitlab

# 2. Drop the local migration branch
git checkout develop            # or the project's default branch
git branch -D gitlab-migration

# 3. If .gitlab-ci.yml was merged into develop, remove it with a revert commit
git rm .gitlab-ci.yml
git commit -m "ci: remove GitLab CI (revert to GitHub-only)"
git push origin develop         # this is the ONLY push to GitHub a revert needs

# 4. Ensure local develop matches GitHub
git fetch origin && git reset --hard origin/develop   # only if you want to discard unmerged gitlab-only commits
```

## In-place source changes (MUST fix when reverting)

Some migrations change source files in place to point dependencies at GitLab. These are NOT additive and must be reverted explicitly — search the repo for `NOTE(gitlab-migration)` comments; each marks a line to restore.

- **brahma** — `dependency/CMakeLists.txt`: cpp-logger `fetch_package` URL changed
  from `https://github.com/LLNL/cpp-logger.git` to
  `ssh://git@czgitlab.llnl.gov:7999/dftracer/cpp-logger.git`. On revert, restore the
  GitHub URL (the old value is recorded in the `NOTE(gitlab-migration)` comment
  directly above the `GIT` line).
- **dftracer** — `dependency/CMakeLists.txt` (two changes):
  - cpp-logger (`dftracer_install_external_project`, tag v0.0.8): URL changed
    from `https://github.com/hariharan-devarajan/cpp-logger.git` to
    `ssh://git@czgitlab.llnl.gov:7999/dftracer/cpp-logger.git`.
  - brahma (`ExternalProject_Add`, `GIT_TAG v1.1.0`): `GIT_REPOSITORY` changed
    from `https://github.com/hariharan-devarajan/brahma.git` to
    `ssh://git@czgitlab.llnl.gov:7999/dftracer/brahma.git`.
  On revert, restore both GitHub URLs (old values recorded in the
  `NOTE(gitlab-migration)` comments directly above each changed line).
  dftracer also had a pre-existing LC HPC `.gitlab-ci.yml` (web-only
  tuolumne/corona jobs): on revert do NOT `git rm` it wholesale — restore the
  pre-migration version from develop@26fb8d9 instead.

## GitLab Pages revert

- The `pages` job lives only in `.gitlab-ci.yml`, so removing that file (step 3 above) removes Pages publishing. To just unpublish, delete the deployment under Settings → Pages on czgitlab (or Deploy → Pages).
- The `docs/` Sphinx tree is intentionally **host-neutral** (plain Sphinx, no GitLab-specific markup) — keep it when reverting; it works as-is with ReadTheDocs on GitHub. If docs were newly authored during migration (e.g. cpp-logger's intro + API pages) and you don't want them on GitHub, `git rm -r docs/` in the same revert commit.

## GitLab-side cleanup (optional)

- Delete or archive the repo at `https://czgitlab.llnl.gov/dftracer/<project>` (Settings → General → Advanced). Archiving is safer than deleting.
- Or just delete the `gitlab-migration` branch there and disable CI/CD + Pages in project settings if you want to keep the mirror.

## Notes

- Step 3 is only needed if `.gitlab-ci.yml` reached `develop`; if it only lives on `gitlab-migration`, deleting that branch (local + GitLab) is enough.
- `.migration/<project>.md` records exactly what was pushed (branches, tags, SHAs) — consult it to know what to undo.
- The skill (`.claude/skills/gitlab-migrate/SKILL.md`) and this folder can stay; they have no effect on GitHub.

# Releasing

OpenPuck uses semantic versioning (below 1.0 for now) and
[Conventional Commits](https://www.conventionalcommits.org/). Releases are
automated with [release-please](https://github.com/googleapis/release-please).

## How changes flow

1. Open a PR. Its **title** must be a Conventional Commit, such as
   `feat: add timer presets` or `fix: stop counter reset on wake`. The
   `PR title` check enforces this. Allowed types: `feat`, `fix`, `perf`, `docs`,
   `refactor`, `test`, `build`, `ci`, `chore`, `revert`.
2. Squash-merge the PR. The PR title becomes the commit message on `main`.
3. release-please keeps a **Release PR** open on `main`. It bumps `version.txt`
   and updates `CHANGELOG.md` from the commits since the last release.
   Before 1.0, `feat` bumps the minor version and `fix` bumps the patch version;
   a breaking change (`feat!:` or a `BREAKING CHANGE:` footer) also bumps the
   minor version. `feat`, `fix`, `perf` and `docs` appear in the changelog;
   `chore`, `ci`, `test`, `build` and `refactor` are hidden.
4. Merge the Release PR to release. This creates the `vX.Y.Z` tag and the
   GitHub Release, then the same workflow builds the release firmware and
   uploads it.

The build runs inside `release.yml` because releases created with the default
`GITHUB_TOKEN` do not trigger other workflows.

## Release PR token

PRs opened with the default `GITHUB_TOKEN` don't get CI runs, so a Release PR
opened that way could never pass the required checks. release-please therefore
uses a token from a GitHub App owned by the maintainer:

1. Create a GitHub App (Settings > Developer settings > GitHub Apps > New).
   Turn off the webhook. Repository permissions: **Contents: Read and write**,
   **Pull requests: Read and write**. Install it on this repository only.
2. Create an environment named `release` (Settings > Environments), limited to
   the `main` branch.
3. In that environment, add the App's client ID as the variable
   `RELEASE_APP_CLIENT_ID` and a private key (generated on the App's page) as
   the secret `RELEASE_APP_PRIVATE_KEY`.

Without the variable, the workflow falls back to `GITHUB_TOKEN`.

## Rebuilding a release

To rebuild and re-upload the files of an existing release (for example after a
failed build), run the **Release** workflow manually (Actions > Release > Run
workflow) and enter the tag, such as `v0.1.0`. The tag must match `version.txt`
at that tag.

## Browser installer

Each release redeploys https://wavemoran.github.io/openpuck/ with that
release's firmware, after verifying its checksum and build provenance.

To redeploy manually (for example to ship an installer fix between releases),
run Actions > Pages > Run workflow. An empty tag uses the latest release; the
installer itself always comes from the branch you run it on (normally `main`).

One-time setup: Settings > Pages > Source: **GitHub Actions**.

## What a release contains

| File | Purpose |
| --- | --- |
| `openpuck-<version>-esp32c3.bin` | Single merged image, flash at offset 0 |
| `openpuck-<version>-parts.zip` | Bootloader, partition table, app and `flash_args` |
| `manifest.json` | ESP Web Tools manifest for the merged image (other web flashers; the OpenPuck installer uses the parts) |
| `SHA256SUMS` | SHA-256 checksums of the files above |

All files carry a build provenance attestation. The firmware is the release
profile (`sdkconfig.defaults`) built in the pinned ESP-IDF v6.1 container.

## Verifying a download

Download the files into one folder, then:

```sh
sha256sum -c SHA256SUMS        # on macOS: shasum -a 256 -c SHA256SUMS
gh attestation verify openpuck-<version>-esp32c3.bin --repo wavemoran/openpuck
```

## Forcing a version

Add a `Release-As` footer to a commit message (for a squash merge, put it in
the PR description body that becomes the commit body, after a blank line):

```text
Release-As: 0.2.0
```

The next Release PR then uses that version.

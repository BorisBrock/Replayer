# AUR packages

- `replayer/` builds the latest tagged release from its GitHub source tarball.
- `replayer-git/` builds the latest commit on `main`.

Each directory is pushed as-is to its own AUR git repository. The AUR needs
a `.SRCINFO` next to every `PKGBUILD`; regenerate it after each change:

```bash
makepkg --printsrcinfo > .SRCINFO
```

## Releasing a new version

Pushing a stable `vX.Y.Z` tag updates the AUR `replayer` package
automatically: the `aur` job in `.github/workflows/release.yml` runs
`update-release.sh`, which takes `replayer/PKGBUILD` from this repository,
sets the version and checksum, test-builds it and pushes it to the AUR.
Pre-releases (`v0.x`, `-rc1` and so on) are skipped.

`replayer/PKGBUILD` here is the template, so its `pkgver` and checksum stay
at the version it was last edited for. Change it here when the build itself
changes; the job fills in the version.

The job logs in with an SSH key of its own, stored as the repository secret
`AUR_SSH_PRIVATE_KEY`. Its public half is one of the keys on the AUR account.

To update by hand instead, for example if the job failed:

```bash
git clone ssh://aur@aur.archlinux.org/replayer.git ~/aur/replayer
packaging/aur/update-release.sh X.Y.Z ~/aur/replayer
git -C ~/aur/replayer push
```

`replayer-git` needs no update per release; its `pkgver()` follows the
repository. Push it again only when the `PKGBUILD` itself changes.

# AUR packages

- `replayer/` builds the latest tagged release from its GitHub source tarball.
- `replayer-git/` builds the latest commit on `main`.

Each directory is pushed as-is to its own AUR git repository. The AUR needs
a `.SRCINFO` next to every `PKGBUILD`; regenerate it after each change:

```bash
makepkg --printsrcinfo > .SRCINFO
```

## Releasing a new version

After pushing a `vX.Y.Z` tag, update `replayer/`:

```bash
cd packaging/aur/replayer
sed -i 's/^pkgver=.*/pkgver=X.Y.Z/; s/^pkgrel=.*/pkgrel=1/' PKGBUILD
updpkgsums                              # from pacman-contrib
makepkg -sf                             # test build
makepkg --printsrcinfo > .SRCINFO
```

Then copy `PKGBUILD` and `.SRCINFO` into the AUR clone, commit and push:

```bash
git clone ssh://aur@aur.archlinux.org/replayer.git ~/aur/replayer
cp PKGBUILD .SRCINFO ~/aur/replayer/
git -C ~/aur/replayer commit -am "Update to X.Y.Z"
git -C ~/aur/replayer push
```

`replayer-git` needs no update per release; its `pkgver()` follows the
repository. Push it again only when the `PKGBUILD` itself changes.

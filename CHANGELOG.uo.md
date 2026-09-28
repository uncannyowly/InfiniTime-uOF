# uo changelog

Changes this fork makes on top of upstream [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime).
Upstream's own changes are in its release notes.

## Versioning

```
1.16.1+uo2
└─┬──┘ └┬┘
  │     └── fork release number on this upstream base
  └──────── upstream InfiniTime version, unchanged
```

- **Releases** are `<upstream>+uo<N>`, tagged `v<upstream>+uo<N>`.
- **Dev builds** are `<upstream>+uo<N>.dev<C>`, where `N` is the release being worked towards and
  `C` counts commits since the previous `uo` release on the same upstream base. A build from a
  tree with uncommitted changes is always a dev build, even at a release tag.
- `N` restarts at 1 whenever the fork is rebased onto a new upstream version, so
  `1.17.0+uo1` follows `1.16.1+uo7`.
- The `+` makes the fork part SemVer build metadata, which SemVer compares as equal to upstream
  `1.16.1`. A SemVer-aware updater should therefore offer real upstream upgrades but not a
  sideways "update" to stock 1.16.1. How each companion app actually parses the firmware
  revision has not been verified.

The logic is in the top-level `CMakeLists.txt` (`FORK_ID`, `FORK_REVISION`). The suffix flows
into `Version::VersionString()`, the BLE firmware revision, System Info, and every build
artifact filename. The mcuboot image header version stays at upstream's hardcoded `1.0.0`.

### Cutting a release

1. Make sure the tree is clean and `FORK_REVISION` in `CMakeLists.txt` is the number you are
   releasing.
2. Move this file's **Unreleased** notes under a new heading for that version.
3. Commit, then tag: `git tag -a v1.16.1+uo<N> -m "1.16.1+uo<N>"`.
4. Reconfigure and build. The version must print without `.dev`.
5. Push `main` and the tag, and publish the GitHub release with the DFU zip and the resource pack.
6. Bump `FORK_REVISION` to `N + 1` and commit. Every build after that is a dev build of the
   next release.

### Rebasing onto a new upstream release

```sh
git fetch upstream --tags
git rebase --onto <new-upstream-tag> 1.16.1 main
```

Then set `FORK_REVISION` back to 1. Conflicts will mostly be in the upstream files this fork
edits: `DisplayApp.cpp`, `Screen.h`, `Messages.h`, `SystemTask.cpp`, `SystemInfo.cpp`,
`FirmwareValidation.cpp`, `Settings.h`, `lv_conf.h`, both linker scripts, and the CMake lists.

## Unreleased (towards 1.16.1+uo2)

- Adopted the `+uo<N>` versioning scheme described above. Dev builds identify themselves on
  the watch.
- System Info and firmware validation show the version on its own line, since dev version
  strings are longer than the 20 characters a line can hold.

## 1.16.1+uo1 (2026-09-28)

Released as `v1.16.1uOF` before this scheme existed. `v1.16.1+uo1` is the same commit.

- Watch faces: LCARS, LCARS Neon, Cybrdek, Cybrdek Rnnr.
- Games: Spaced Perpetrators, Minepeepers, Tanked.
- Settings: Vol Space, showing used and free space on internal and external flash.
- Car: OBD-II speed, boost and HUD screens. Simulator only; no adapter transport yet.
- Opt-in raw button hook on `Screen`, used by Tanked.

## 1.16.1F (2026-09-27)

Released as `v1.16.1F`. The first four watch faces only.

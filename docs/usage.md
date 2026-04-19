# Usage

## Prerequisites

- Arch Linux (or any systemd-based distro with standard `useradd`/`groupadd`)
- `sudo` access for the admin user (`mike`)
- `acl` package installed (`sudo pacman -S acl`) for `setfacl` support
- `git` installed
- Filesystem under the repo mount point must support Unix permissions and ACLs
  (ext4, xfs, btrfs are fine; exFAT/NTFS are not)

## Quick Start

### 1. Clone this repo

```bash
git clone git@github.com:softwarewrighter/devgroup.git /disk1/github/softwarewrighter/devgroup
cd /disk1/github/softwarewrighter/devgroup
```

### 2. Create users and workspace (requires sudo)

```bash
sudo ./scripts/setup-devgroup.sh \
  --group devgroup \
  --admin mike \
  --users devh1,devr1,devo1,devp1
```

This will:
- Create the `devgroup` Unix group (if missing)
- Create each user account with a home directory and bash shell
- Add all users (including `mike`) to `devgroup`
- Set `umask 027` in each user's `.bashrc`
- Create `work/bare/` and `work/<user>/github/` directory trees
- Set ownership and permissions (setgid, group-readable, owner-writable)
- Set ACLs so `mike` can administer all worker subtrees
- Ensure `work/` is in `.gitignore`

### 3. Create bare repos from GitHub (requires sudo)

```bash
sudo ./scripts/setup-bare-repos.sh \
  --admin mike \
  --group devgroup
```

This creates bare clones of the sw-embed repos under `work/bare/sw-embed/`
with correct ownership and permissions.

### 4. Clone repos as each worker (requires sudo)

After bare repos exist, clone them into each worker's subtree:

```bash
# Example for devh1
sudo -u devh1 bash -lc '
  cd /disk1/github/softwarewrighter/devgroup/work/devh1/github
  mkdir -p sw-embed
  git clone /disk1/github/softwarewrighter/devgroup/work/bare/sw-embed/sw-cor24-hlasm.git sw-embed/sw-cor24-hlasm
'
```

Repeat for each worker/repo pair:

| User   | Bare repo source                    | Clone destination                              |
|--------|-------------------------------------|------------------------------------------------|
| `devh1`| `work/bare/sw-embed/sw-cor24-hlasm.git` | `work/devh1/github/sw-embed/sw-cor24-hlasm`  |
| `devr1`| `work/bare/sw-embed/sw-cor24-rpg-ii.git`| `work/devr1/github/sw-embed/sw-cor24-rpg-ii` |
| `devo1`| `work/bare/sw-embed/sw-cor24-ocaml.git` | `work/devo1/github/sw-embed/sw-cor24-ocaml`  |
| `devp1`| `work/bare/sw-embed/sw-cor24-prolog.git`| `work/devp1/github/sw-embed/sw-cor24-prolog`  |

### 5. Validate the setup (requires sudo)

```bash
sudo ./scripts/validate-devgroup.sh \
  --group devgroup \
  --admin mike \
  --users devh1,devr1,devo1,devp1 \
  --test-bare sw-embed/sw-cor24-hlasm.git
```

This tests:
- Users/group exist and membership is correct
- Directory structure and permissions are correct
- Each worker can write only in their own subtree
- Workers cannot write to other workers' trees or bare repos
- Workers can clone from bare repos
- Cloned files are group-readable

## Coordinator Workflow (mike)

### Updating bare repos from GitHub

```bash
cd /disk1/github/softwarewrighter/devgroup/work/bare/sw-embed/sw-cor24-hlasm.git
git fetch origin
```

### Importing worker changes

Option A -- add worker repo as remote:
```bash
cd /path/to/mike/rw-clone/sw-cor24-hlasm
git remote add devh1 /disk1/github/softwarewrighter/devgroup/work/devh1/github/sw-embed/sw-cor24-hlasm
git fetch devh1
git cherry-pick devh1/<branch>
```

Option B -- format-patch:
```bash
git -C /disk1/github/softwarewrighter/devgroup/work/devh1/github/sw-embed/sw-cor24-hlasm \
  format-patch -1 HEAD --stdout > /tmp/devh1.patch
git -C /path/to/mike/rw-clone/sw-cor24-hlasm am /tmp/devh1.patch
```

### Pushing and creating PRs

Only `mike` has GitHub credentials. Push from mike's own r/w clone, not from
worker trees.

## Worker Constraints

- No SSH keys registered on GitHub
- No credential helpers configured
- Cannot push to any remote
- Cannot write outside `work/<username>/`
- Can read all other workers' trees (group read)
- Must authenticate separately for AI tool access (Claude, Codex, etc.)
  using per-user config under `~/.config/` (not under `work/`)

## Notes

- New group membership takes effect on next login shell. Use `su - <user>`
  or log out/in after initial setup.
- Git may warn about "dubious ownership" if mike runs git inside a
  worker-owned repo. Use `git config --global --add safe.directory <path>`
  selectively, or prefer fetching from worker repos as remotes.
- The `work/` directory and all its contents are gitignored and not tracked.

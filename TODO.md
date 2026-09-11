# TODO

## Let users pick a username, or stop Imager offering to

Our images only work with the user `pi`. `config/rnbooscquery.service.usr` and
`config/rnbooscquery.service.local` both hardcode `User=pi` (as does
rnbo-runner-panel's unit), and paths under `/home/pi` are assumed in places.

But Raspberry Pi Imager invites the user to choose a username during
installation, and nothing stops them. If they do, they get an image where the
services do not start, with no explanation.

### Where the rename actually happens

Not where you would guess. pi-gen's `DISABLE_FIRST_BOOT_USER_RENAME=1`, which our
image config sets, only disables *pi-gen's* own first-boot flow (`rename-user -f
-s`, plus removing `piwiz.desktop`). It has no bearing on Imager.

Because `config/repo.json` declares `init_format: "systemd"`, Imager writes its
own `firstrun.sh` into the boot partition and adds it to cmdline. That script
renames the user itself:

* if `/usr/lib/userconf-pi/userconf` exists it calls it as `userconf <newname>
  <passwordhash>`
* otherwise it falls back to `usermod -l` / `usermod -m -d` / `groupmod -n`
  directly

The real `userconf` (RPi-Distro/userconf-pi, branch `pios/trixie`) renames UID
1000, fixes the group, `/etc/subuid`, `/etc/subgid` and
`/etc/sudoers.d/010_pi-nopasswd`, then applies the password with `chpasswd -e`.

Imager itself cannot be told to allow a password but forbid a username: the
os-list schema has no per-field control, and `init_format: "none"` would disable
all customisation including the password.

### Option A: force the username, keep the password

Ship our own `/usr/lib/userconf-pi/userconf` that ignores the new name and only
applies the password hash to `pi`. Imager's generated script then "succeeds", the
user still chooses their own password, and the account stays `pi`.

Caveats, in order of how much they should worry us:

1. The user types a username, gets `pi`, and is told nothing. If we do this, say
   so in the `description` field of the `repo.json` entry at least.
2. It is coupled to a script we do not control. If a future Imager stops
   preferring `userconf-pi`, our override is silently bypassed and renames start
   working again — in the field, with no signal to us.
3. The file must *exist*, not be missing: absence selects the `usermod` fallback,
   which renames anyway. Since the real one comes from the `userconf-pi` package,
   use `dpkg-divert` so an apt upgrade does not restore it.

### Option B: stop needing a fixed username

Resolve the UID 1000 user at runtime, or template the units at first boot,
instead of hardcoding `User=pi`. Then any username works, we are aligned with
where Raspberry Pi is pushing users rather than working around it, and the whole
problem goes away instead of being pinned down.

More work than Option A, and it touches the packaging rather than the image.

### Testing either

The failure mode is silent, so test by actually imaging a card with a non-`pi`
username and checking that the runner and panel come up — not by reasoning about
the scripts.

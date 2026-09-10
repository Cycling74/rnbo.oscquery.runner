# RNBO OSCQuery Runner

A [RNBO](https://cycling74.com/products/rnbo) runner controlled by [OSCQuery](https://github.com/Vidvox/OSCQueryProposal).

## RNBO Move Takeover

The RNBO OSCQuery Runner is a core component of [RNBO Move Takeover](https://cycling74.com/products/rnbo/move).
For more technical information about that project check out [RNBO Move Control](https://github.com/Cycling74/rnbo.move.control)

## Setup

**NOTE** there is a separate [README for rpi](README-rpi.md) that indicates how
to build for rpi.

### Build Requirements

This currently builds and runs on Linux and Mac. Windows is TBD.

`debian` instructions also apply to other `apt` based distros like `ubuntu`.

* [cmake](https://cmake.org/download/) version 3.17 or greater
  * macOS: `brew install cmake`
  * debian: `sudo apt-get install cmake`
  * make sure `cmake` is in your path if you install it with `brew` or directly from the download
* `g++` or `clang++`
  * macOS: `clang++` comes with XCode.
  * debian: `sudo apt-get install build-essential g++`
* [conan](https://conan.io/downloads.html)
  * via pip3:
    * macOS: modern macOS comes with `pip3`
    * debian: `sudo apt-get install python3-pip`
    * `pip3 install --break-system-packages --user conan==1.61.0`
  * make sure that `conan` is in your PATH, I updated my `.bashrc` to add `~/.local/bin/` to my PATH
* `ruby` 2.0+ to run the compile script
  * macOS: modern macOS comes with `ruby`
  * debian: `sudo apt-get install ruby`
* `sdbus` lib or configure with `-DWITH_DBUS=Off
  * debian: `sudo apt-get install libsdbus-c++-dev`

on debian based systems, here is a 1 liner for setting up dependencies

```
sudo apt-get -y install cmake build-essential libavahi-compat-libdnssd-dev libssl-dev libjack-jackd2-dev libdbus-1-dev libxml2-dev libgmock-dev google-mock libsdbus-c++-dev python3-pip ruby libsndfile1-dev
```

on linux at least, the conan profile entry for `libcxx` is important

```
compiler.libcxx=libstdc++11
```

### Building

Build the runner with CMake. You may have to update the `RNBO_DIR` to reflect the path on your system.

If you're on Mac OS using the bundled RNBO version, your `RNBO_DIR` should probably be:

* `~/Documents/Max\ 8/Packages/RNBO/source/rnbo/` if you installed RNBO with the package manager
* `/Applications/Max.app/Contents/Resources/C74/packages/RNBO/source/rnbo` if you're using RNBO bundled with Max.

On Linux you'll likely have to copy the rnbo src dir from a Windows or Mac machine.

```
mkdir build/
cd build/
cmake .. -DRNBO_DIR=~/Documents/Max\ 8/Packages/RNBO/source/rnbo/
cmake --build .
```

If you're on a debian based system, you can also build a `.deb`

```
cpack
```

then you can install it with

```
sudo dpkg -i *.deb`
```

### Configuration

There is an example `runner.json` config file in the config directory.
If you want some customizations you can edit that and copy it here:
`~/.config/rnbo/runner.json`

Here is an example of the contents:
```json
{
    "compile_cache_dir": "~/Documents/rnbo/cache/so/",
    "save_dir": "~/Documents/rnbo/cache/saves/",
    "source_cache_dir": "~/Documents/rnbo/cache/src/",
    "datafile_dir": "~/Documents/rnbo/datafiles/",

    "instance_auto_start_last": true,
    "instance_auto_connect_audio": true,
    "instance_auto_connect_midi": true,
    "jack": {
        "period_frames": 1024,
        "sample_rate": 44100.0,
        "card_name": "hw:ES8",
        "midi_system_name": "raw"
    }
}
```

* `compile_cache_dir`: a path to the directory where compiled shared objects are stored
* `save_dir`: a path to the directory where save data is stored
* `source_cache_dir`: a path to the directory where source files are stored before compiling
* `datafile_dir`: a path to the directory where datafiles are stored, to be loaded as data refs
  * you can put files directly into this directory and load them via the OSCQuery data ref commands
* `instance_auto_start_last`: a boolean that indicates if when the runner starts, if it should attempt to load the last patcher it loaded before restart
* `instance_auto_connect_audio`: a boolean that indicates if the runner should automatically try to connect its audio i/o
  * disabling this can be useful if you want to have a custom jack signal flow, the commandline `jack_connect` can be useful if you have this set to false
* `instance_auto_connect_midi`: a boolean that indicates if the runner should automatically connect to MIDI devices that it sees
  * you can use `jack_connect` on the commandline to connect to specific MIDI devices if you have this set to false

The only file that is currently saved in the `save_dir` is called `last.json`

Here is an example of that file content:
```json
{
    "instances": [
        {
            "config": {
                "datarefs": {
                    "loop": "jongly.aif"
                },
                "inports": [
                    "foo"
                ],
                "outports": [
                    "bar"
                ],
                "presets": {
                    "muted": {
                        "baz": {
                            "value": 0.0
                        }
                    },
                    "snap1": {
                        "baz": {
                            "value": 0.9430000185966492
                        }
                    }
                }
            },
            "so_path": "/home/pi/Documents/rnbo/cache/so/libRNBORunnerSO1634332529.0.13.0-dev.44.so"
        }
    ]
}
```

The saves file only supports 1 instance at the time of this writing but eventually might support more.
If you edit this file you can change values for dataref mappings, presets and also identify which `so` to load on restart.

## Releasing

Pushing a tag to this repo builds the armhf and arm64 `.deb` packages and
attaches them to a GitHub release. The tag is the only input, so its shape
matters:

```
v<rnbo-version>-<runner-version>[-<prerelease>]
```

| part | example | what it drives |
| --- | --- | --- |
| `rnbo-version` | `1.4.5` | which RNBO C++ library to build against: `rnbo/<rnbo-version>@<channel>` |
| `runner-version` | `11` | this repo's own release counter, independent of the RNBO version |
| `prerelease` | `beta1` | optional. marks a **runner** beta. must start with a letter |

Examples:

| tag | builds against | published as |
| --- | --- | --- |
| `v1.4.5-11` | `rnbo/1.4.5@c74/stable` | release |
| `v1.4.5-11-beta1` | `rnbo/1.4.5@c74/stable` | pre-release |
| `v1.5.0-stb-resampling.2-3` | `rnbo/1.5.0-stb-resampling.2@c74/testing` | pre-release |

There are two independent "beta" notions and the tag keeps them apart. A
*runner* beta is the trailing `-<prerelease>` and means this repo's build is not
final. An *RNBO* beta is an `rnbo-version` that is not a plain `x.y.z`; those
live in the `c74/testing` channel on the private conan remote rather than
`c74/stable` on cycling-public, and the workflow selects the channel from the
version's shape. Either one makes the GitHub release a pre-release and lands the
`.deb` in the beta apt repo.

`RUNNER_VERSION` and `RUNNER_PRERELEASE` come from the tag, so releasing does not
require editing `CMakeLists.txt`. The values there are only defaults for local
builds.

### Version spellings

The tag and the `.deb` file name use `-` before the prerelease, but the version
recorded *inside* the package uses `~`:

| | value |
| --- | --- |
| tag | `v1.4.5-11-beta1` |
| file | `rnbooscquery_1.4.5-11-beta1_armhf.deb` |
| deb version | `1.4.5-11~beta1` |

These differ on purpose. `dpkg` sorts `~` before everything, including the empty
string, so `1.4.5-11~beta1` upgrades cleanly to `1.4.5-11` — with a plain `-` it
would sort *above* the final release and beta testers would never move off it.
Git refnames cannot contain `~`, so the tag cannot use that spelling.

### Publishing

The `rnbo-runner-publish` workflow in `rnbo-build-runner` takes the tag without
its leading `v` as `release-version` (e.g. `1.4.5-11-beta1`), downloads the
matching assets, and picks the apt repo: anything that is not exactly
`x.y.z-<digits>` goes to the `-beta` repos.

### Gotchas

- The tag is validated before anything builds; a malformed one fails the `parse`
  job with a message instead of dying minutes later inside cmake.
- The `rnbo-version` part must name a package that actually exists. A typo such
  as `v1.4.5-oops-10` parses as the RNBO prerelease `1.4.5-oops` and fails at
  conan, since a typo and a real prerelease are indistinguishable by shape.
- Building against an RNBO prerelease needs the private conan remote, which the
  workflow configures from the `C74_CONAN_REMOTE_URL`, `C74_CONAN_USER` and
  `C74_CONAN_PASSWORD` secrets.

## Running

If you haven't run jack before you probably want to set it up with `qjackctl`, you can leave that running while running the runner.

Simply run the runner from the build directory `./bin/rnbooscquery`
Then start up Max. The RNBO sidebar should list your host as a `OSCQuery Runner Export`.

## Connecting over a direct Ethernet cable

You can plug a computer straight into the runner's Ethernet port, with no router, switch or
DHCP server in between. With nothing to hand out addresses, both ends assign themselves an
IPv4 *link-local* address from `169.254.0.0/16`
([RFC 3927](https://datatracker.ietf.org/doc/html/rfc3927)) and find each other by name over
mDNS, so the usual URLs work:

```
http://<hostname>.local:5678       OSCQuery / websocket
osc.udp://<hostname>.local:1234    OSC
http://<hostname>.local:3000       runner panel web interface, if installed
```

What to expect:

* it takes a few seconds (macOS, Linux) to about a minute (Windows) after plugging in before
  an address is self-assigned — DHCP has to time out first
* both ends need an address in `169.254.0.0/16`. If either side has IPv4 turned off, or set
  manually with no address, nothing on the link is reachable over IPv4
* the link also has IPv6 link-local (`fe80::`) addresses and the runner listens on those too,
  but they can only be used with an interface zone suffix (`fe80::1%eth0`), and a URL cannot
  carry one at all. Prefer the `.local` name or the `169.254.x.x` address.

### Runner configuration

Images built with the `stage2/05-net-linklocal` step have this configured already. Otherwise,
on a NetworkManager system:

```shell
nmcli device status                    # find your ethernet device: eth0, end0, enp1s0 ...
sudo nmcli con mod 'Wired connection 1' ipv4.link-local fallback ipv4.dhcp-timeout 2147483647
sudo nmcli device reapply <device>
```

Both settings are needed; neither works alone.

* `ipv4.link-local fallback` assigns a `169.254.x.x` address when DHCP produces nothing, which
  is what gives avahi an A record to publish. It needs NetworkManager 1.52 or newer (Debian 13
  "trixie"). On older NetworkManager use `ipv4.link-local enabled` instead — see below.
* `ipv4.dhcp-timeout 2147483647` is "infinity", and it is the setting that actually keeps the
  link usable. Without it the connection fails about 45 seconds in with
  `ip-config-unavailable`, NetworkManager flushes the interface — taking its addresses and its
  published mDNS records with it — and immediately retries, forever. A cable-connected runner
  then appears and disappears every minute or so.

**NOTE** use `nmcli device reapply` rather than `nmcli con up` when you are connected over the
very cable you are reconfiguring. `con up` deactivates the connection first and will drop your
own session.

Check the result with:

```shell
nmcli -f GENERAL.STATE,IP4.ADDRESS,IP6.ADDRESS dev show <device>
```

You should see a `169.254.x.x` address. The state stays at `connecting (getting IP
configuration)` because the DHCP request never completes; that is expected. One consequence is
that `NetworkManager-wait-online` waits out its full timeout at boot when no DHCP server is
present.

On NetworkManager older than 1.52 (Debian 12 "bookworm" ships 1.42) there is no `fallback`
mode, but `enabled` does the same job on that version:

```shell
sudo nmcli con mod 'Wired connection 1' ipv4.link-local enabled ipv4.dhcp-timeout 2147483647
```

Verified on 1.42.4: `enabled` does not add a link-local address alongside a *working* DHCP
lease, so on an ordinary network the interface simply takes its lease — but on a link where
DHCP never succeeds the `169.254.x.x` address does appear, which is the case that matters
here.

The runner also needs `avahi-daemon` installed and running to be reachable by name.

### Client configuration

**macOS** — System Settings > Network > your Ethernet service > Details > TCP/IP, then set
**Configure IPv4** to **Using DHCP**. If it is set to **Off**, or to **Manually** with no
address, macOS will not self-assign a link-local address and the connection cannot work. See
[Change TCP/IP settings on Mac](https://support.apple.com/guide/mac-help/mh14129/mac).

```shell
ifconfig en6 | grep "inet "        # expect 169.254.x.x
```

**Windows** — Settings > Network & internet > Ethernet > **IP assignment** > Edit >
**Automatic (DHCP)**. Windows then self-assigns a `169.254.x.x` address (APIPA) when no DHCP
server answers, which can take up to about a minute. See
[Essential Network Settings and Tasks in Windows](https://support.microsoft.com/en-us/windows/change-tcp-ip-settings-bd0a07af-15f5-cd6a-363f-ca2b6f391ace).

```shell
ipconfig                           # expect "Autoconfiguration IPv4 Address"
```

`.local` names resolve natively on Windows 10 and later. On older versions either install
Apple's Bonjour or connect by `169.254.x.x` address.

**Linux** — with NetworkManager, the same settings as the runner:

```shell
sudo nmcli con mod <profile> ipv4.link-local fallback ipv4.dhcp-timeout 2147483647
sudo nmcli device reapply <iface>
ip -4 addr show <iface>            # expect 169.254.x.x
```

Install `avahi-daemon` (and `libnss-mdns`) if `.local` names do not resolve.

### Making the connection come up faster

There are two separate waits here and they have different causes.

**Name resolution is usually not the slow part.** Once both ends have an IPv4 address, resolving
`<hostname>.local` takes single digit milliseconds. But if the *client* has no IPv4 address on
the link, every lookup costs a fixed five seconds — even a lookup that only wants the IPv6
record — because the A query has no interface to go out on and must run to its timeout before
the resolver answers. On macOS that is the whole difference between `Configure IPv4: Off` and
`Using DHCP`; nothing else needs changing.

**The wait you actually notice is address acquisition.** Both ends have to give up on DHCP
before assigning themselves a link-local address: a few seconds on macOS and Linux, up to about
a minute on Windows. Giving the client's adapter a static link-local address skips it entirely.

* **macOS** — Configure IPv4 > Manually, address `169.254.1.10`, subnet mask `255.255.0.0`, no
  router. `networksetup -listallnetworkservices` lists the service names:

    ```shell
    networksetup -setmanual "<service name>" 169.254.1.10 255.255.0.0 ""
    networksetup -setdhcp "<service name>"                 # to put it back
    ```

* **Windows** — Settings > Network & internet > Ethernet > IP assignment > Edit > **Manual**,
  turn IPv4 on, address `169.254.1.10`, mask `255.255.0.0`, no gateway.

* **Linux** —

    ```shell
    sudo nmcli con mod <profile> ipv4.method manual ipv4.addresses 169.254.1.10/16
    sudo nmcli con mod <profile> ipv4.method auto          # to put it back
    ```

Three things worth knowing before you do that: the adapter will not work on an ordinary DHCP
network until you set it back; a manual address skips the duplicate address detection described
in RFC 3927, so pick a host part unlikely to collide; and the runner's own link-local address is
stable in practice — NetworkManager derives it deterministically and it survives reboots — so
once you have seen it, `http://169.254.x.x:3000` is a bookmark that skips name resolution
altogether.

**Linux clients — check mDNS is wired into the resolver.** Install `libnss-mdns` and confirm
that `/etc/nsswitch.conf` lists `mdns4_minimal` ahead of `dns`:

```
hosts:          files mdns4_minimal [NOTFOUND=return] dns
```

Without it, `.local` lookups fall through to your unicast DNS server and wait for that to fail
before anything else is tried.

**Windows clients — check nothing is suppressing mDNS.** Windows 10 and later resolve `.local`
names natively. If they do not, check that the adapter's network profile is **Private** rather
than **Public**, since the public profile blocks inbound traffic including mDNS responses, and
that the "Turn off multicast name resolution" group policy is not enabled.

### Troubleshooting a direct connection

If the name does not resolve, browse for the service and connect by address instead:

```shell
dns-sd -B _oscjson._tcp            # macOS, or Windows with Bonjour
avahi-browse -tr _oscjson._tcp     # Linux
```

An IPv4 link-local address carries no interface identifier, so the client picks an interface by
route — and if more than one of its interfaces has a `169.254.0.0/16` route, it can pick the
wrong one. This bites when the runner has recently been on another network: the client
remembers its MAC on that interface and pins a host route to it, and connections then fail with
`EHOSTDOWN` or a timeout even though both ends have addresses. On macOS, `route -n get
169.254.x.x` shows which interface is being used, and an `R` (reject) flag means it is stuck.
The cheapest fix is to send one packet the other way, from the runner to the client's
link-local address, which corrects the client's route and ARP entry:

```shell
ping -c 3 169.254.x.x               # from the runner, to the client
```

If the runner drops off the link periodically, look for physical link problems — on the runner,
`dmesg | grep -i "link is"` lists Ethernet link up/down events. Some USB Ethernet adapters and
marginal cables renegotiate repeatedly at gigabit; pinning the link to 100 Mb full duplex often
settles it:

```shell
sudo nmcli con mod 'Wired connection 1' 802-3-ethernet.auto-negotiate yes \
    802-3-ethernet.speed 100 802-3-ethernet.duplex full
```

### Further reading

* [RFC 3927 — Dynamic Configuration of IPv4 Link-Local Addresses](https://datatracker.ietf.org/doc/html/rfc3927)
* [NetworkManager `ipv4` settings reference](https://networkmanager.dev/docs/api/latest/settings-ipv4.html) — `link-local`, `dhcp-timeout`
* [Change TCP/IP settings on Mac](https://support.apple.com/guide/mac-help/mh14129/mac)
* [Essential Network Settings and Tasks in Windows](https://support.microsoft.com/en-us/windows/change-tcp-ip-settings-bd0a07af-15f5-cd6a-363f-ca2b6f391ace)
* [Avahi](https://avahi.org/) — the mDNS/DNS-SD implementation used on Linux

## Communicating with the runner

You can communicate with the runner via [Open Sound Control (OSC)](http://opensoundcontrol.stanford.edu/) over either websockets or UDP.

If you have sucessfully connected to a runner in Max, the associated target sidebar info should show you the UDP and HTTP/WS host port and, for OSC, transport.

By default the HTTP and websocket port are `5678` and OSC is UDP at `1234` so if you know the `ip` of your runner, you should be able to load a webpage with the url:
`http://<ipoftherunner>:5678` and send OSC messages at `osc.udp://<ipoftherunner>:1234`
If you have a hostname like `c74rpi.local` that works, you can also use that `http://c74rpi.local:5678` `osc.udp://c74rpi.local:1234`

The websocket interface is created via an [http upgrade](https://developer.mozilla.org/en-US/docs/Web/HTTP/Protocol_upgrade_mechanism) from the HTTP host and port.

*NOTE* the websocket interface is used for more than just `OSC`, so you'll want to detect the type of the websocket messages and only try to parse the Binary messages.

### OSC Namespace

If you've sent a patch to your runner, you should be able to investigate the
runner's [OSCQuery](https://github.com/Vidvox/OSCQueryProposal) namespace via
HTTP.  For instance, if my runner is at `c74rpi.local`, I might see the
below in my web browser if I load the URL `http://c74rpi.local:5678`

```json
{
  "FULL_PATH":"/",
  "CONTENTS":{
    "rnbo":{
      "FULL_PATH":"/rnbo",
      "CONTENTS":{
        "info":{
          "FULL_PATH":"/rnbo/info",
          "DESCRIPTION":"information about RNBO and the running system",
          "CONTENTS":{
            "version":{
              "FULL_PATH":"/rnbo/info/version",
              "TYPE":"s",
              "VALUE":"0.11.0-dev",
              "ACCESS":1,
              "CLIPMODE":"none"
            },
            "system_name":{
              "FULL_PATH":"/rnbo/info/system_name",
              "TYPE":"s",
              "VALUE":"Linux",
              "ACCESS":1,
              "CLIPMODE":"none"
            },
            "system_processor":{
              "FULL_PATH":"/rnbo/info/system_processor",
              "TYPE":"s",
              "VALUE":"armv7",
              "ACCESS":1,
              "CLIPMODE":"none"
            },
            "system_id":{
              "FULL_PATH":"/rnbo/info/system_id",
              "TYPE":"s",
              "VALUE":"c516613b-449f-49c7-a81b-f4de411f8d1e",
              "ACCESS":1,
              "CLIPMODE":"none",
              "DESCRIPTION":"a unique, one time generated id for this system"
            },
            "disk_bytes_available":{
              "FULL_PATH":"/rnbo/info/disk_bytes_available",
              "TYPE":"s",
              "VALUE":"11332669440",
              "ACCESS":1,
              "CLIPMODE":"none"
            },
            "update":{
              "FULL_PATH":"/rnbo/info/update",
              "DESCRIPTION":"Self upgrade/downgrade",
              "CONTENTS":{
                "state":{
                  "FULL_PATH":"/rnbo/info/update/state",
                  "TYPE":"s",
                  "VALUE":"idle",
                  "RANGE":[
                    {
                      "VALS":[
                        "idle",
                        "active",
                        "failed"
                      ]
                    }
                  ],
                  "ACCESS":1,
                  "CLIPMODE":"both",
                  "DESCRIPTION":"Update state"
                },
                "status":{
                  "FULL_PATH":"/rnbo/info/update/status",
                  "TYPE":"s",
                  "VALUE":"waiting",
                  "ACCESS":1,
                  "CLIPMODE":"none",
                  "DESCRIPTION":"Latest update status"
                },
                "supported":{
                  "FULL_PATH":"/rnbo/info/update/supported",
                  "TYPE":"T",
                  "VALUE":null,
                  "ACCESS":1,
                  "CLIPMODE":"none",
                  "DESCRIPTION":"Does this runner support remote upgrade/downgrade"
                }
              }
            }
          }
        },
        "cmd":{
          "FULL_PATH":"/rnbo/cmd",
          "TYPE":"s",
          "VALUE":"",
          "ACCESS":2,
          "CLIPMODE":"none",
          "DESCRIPTION":"command handler"
        },
        "resp":{
          "FULL_PATH":"/rnbo/resp",
          "TYPE":"s",
          "VALUE":"",
          "ACCESS":1,
          "CLIPMODE":"none",
          "DESCRIPTION":"command response"
        },
        "jack":{
          "FULL_PATH":"/rnbo/jack",
          "CONTENTS":{
            "info":{
              "FULL_PATH":"/rnbo/jack/info",
              "CONTENTS":{
                "alsa_cards":{
                  "FULL_PATH":"/rnbo/jack/info/alsa_cards",
                  "CONTENTS":{
                    "hw:ES8":{
                      "FULL_PATH":"/rnbo/jack/info/alsa_cards/hw:ES8",
                      "TYPE":"s",
                      "VALUE":"USB-Audio - ES-8\nExpert Sleepers Ltd ES-8 at usb-0000:01:00.0-1.4, high speed",
                      "ACCESS":1,
                      "CLIPMODE":"none"
                    },
                    "hw:1":{
                      "FULL_PATH":"/rnbo/jack/info/alsa_cards/hw:1",
                      "TYPE":"s",
                      "VALUE":"USB-Audio - ES-8\nExpert Sleepers Ltd ES-8 at usb-0000:01:00.0-1.4, high speed",
                      "ACCESS":1,
                      "CLIPMODE":"none"
                    }
                  }
                },
                "is_realtime":{
                  "FULL_PATH":"/rnbo/jack/info/is_realtime",
                  "TYPE":"T",
                  "VALUE":null,
                  "ACCESS":1,
                  "CLIPMODE":"none",
                  "DESCRIPTION":"indicates if jack is running in realtime mode or not"
                }
              }
            },
            "config":{
              "FULL_PATH":"/rnbo/jack/config",
              "DESCRIPTION":"Jack configuration parameters",
              "CONTENTS":{
                "card":{
                  "FULL_PATH":"/rnbo/jack/config/card",
                  "TYPE":"s",
                  "VALUE":"hw:ES8",
                  "RANGE":[
                    {
                      "VALS":[
                        "hw:ES8",
                        "hw:1"
                      ]
                    }
                  ],
                  "ACCESS":3,
                  "CLIPMODE":"both",
                  "DESCRIPTION":"ALSA device name"
                },
                "num_periods":{
                  "FULL_PATH":"/rnbo/jack/config/num_periods",
                  "TYPE":"i",
                  "VALUE":2,
                  "RANGE":[
                    {
                      "VALS":[
                        1,
                        2,
                        3,
                        4
                      ]
                    }
                  ],
                  "ACCESS":3,
                  "CLIPMODE":"both",
                  "DESCRIPTION":"Number of periods of playback latency"
                },
                "period_frames":{
                  "FULL_PATH":"/rnbo/jack/config/period_frames",
                  "TYPE":"i",
                  "VALUE":1024,
                  "RANGE":[
                    {
                      "VALS":[
                        32,
                        64,
                        128,
                        256,
                        512,
                        1024
                      ]
                    }
                  ],
                  "ACCESS":3,
                  "CLIPMODE":"both",
                  "DESCRIPTION":"Frames per period"
                },
                "sample_rate":{
                  "FULL_PATH":"/rnbo/jack/config/sample_rate",
                  "TYPE":"f",
                  "VALUE":48000.0,
                  "RANGE":[
                    {
                      "MIN":22050.0
                    }
                  ],
                  "ACCESS":3,
                  "CLIPMODE":"both",
                  "DESCRIPTION":"Sample rate"
                }
              }
            },
            "active":{
              "FULL_PATH":"/rnbo/jack/active",
              "TYPE":"T",
              "VALUE":null,
              "ACCESS":3,
              "CLIPMODE":"none"
            },
            "transport":{
              "FULL_PATH":"/rnbo/jack/transport",
              "CONTENTS":{
                "bpm":{
                  "FULL_PATH":"/rnbo/jack/transport/bpm",
                  "TYPE":"f",
                  "VALUE":100.0,
                  "ACCESS":3,
                  "CLIPMODE":"none"
                },
                "rolling":{
                  "FULL_PATH":"/rnbo/jack/transport/rolling",
                  "TYPE":"F",
                  "VALUE":null,
                  "ACCESS":3,
                  "CLIPMODE":"none"
                }
              }
            }
          }
        },
        "inst":{
          "FULL_PATH":"/rnbo/inst",
          "DESCRIPTION":"command response",
          "CONTENTS":{
            "0":{
              "FULL_PATH":"/rnbo/inst/0",
              "CONTENTS":{
                "jack":{
                  "FULL_PATH":"/rnbo/inst/0/jack",
                  "CONTENTS":{
                    "audio_ins":{
                      "FULL_PATH":"/rnbo/inst/0/jack/audio_ins",
                      "TYPE":"",
                      "VALUE":[

                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list"
                    },
                    "audio_outs":{
                      "FULL_PATH":"/rnbo/inst/0/jack/audio_outs",
                      "TYPE":"",
                      "VALUE":[

                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list"
                    },
                    "midi_ins":{
                      "FULL_PATH":"/rnbo/inst/0/jack/midi_ins",
                      "TYPE":"s",
                      "VALUE":[
                        "rnbo0:midiin1"
                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list"
                    },
                    "midi_outs":{
                      "FULL_PATH":"/rnbo/inst/0/jack/midi_outs",
                      "TYPE":"s",
                      "VALUE":[
                        "rnbo0:midiout1"
                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list"
                    }
                  }
                },
                "params":{
                  "FULL_PATH":"/rnbo/inst/0/params",
                  "DESCRIPTION":"Parameter get/set",
                  "CONTENTS":{
                    "foo":{
                      "FULL_PATH":"/rnbo/inst/0/params/foo",
                      "TYPE":"s",
                      "VALUE":"x",
                      "RANGE":[
                        {
                          "VALS":[
                            "x",
                            "y",
                            "z"
                          ]
                        }
                      ],
                      "ACCESS":3,
                      "CLIPMODE":"both",
                      "CONTENTS":{
                        "normalized":{
                          "FULL_PATH":"/rnbo/inst/0/params/foo/normalized",
                          "TYPE":"f",
                          "VALUE":0.20000000298023225,
                          "RANGE":[
                            {
                              "MIN":0.0,
                              "MAX":1.0
                            }
                          ],
                          "ACCESS":3,
                          "CLIPMODE":"both"
                        }
                      }
                    },
                    "bar":{
                      "FULL_PATH":"/rnbo/inst/0/params/bar",
                      "TYPE":"f",
                      "VALUE":0.0,
                      "RANGE":[
                        {
                          "MIN":0.0,
                          "MAX":100.0
                        }
                      ],
                      "ACCESS":3,
                      "CLIPMODE":"both",
                      "CONTENTS":{
                        "normalized":{
                          "FULL_PATH":"/rnbo/inst/0/params/bar/normalized",
                          "TYPE":"f",
                          "VALUE":0.0,
                          "RANGE":[
                            {
                              "MIN":0.0,
                              "MAX":1.0
                            }
                          ],
                          "ACCESS":3,
                          "CLIPMODE":"both"
                        }
                      }
                    }
                  }
                },
                "data_refs":{
                  "FULL_PATH":"/rnbo/inst/0/data_refs"
                },
                "presets":{
                  "FULL_PATH":"/rnbo/inst/0/presets",
                  "CONTENTS":{
                    "entries":{
                      "FULL_PATH":"/rnbo/inst/0/presets/entries",
                      "TYPE":"s",
                      "VALUE":[
                        "untitled 1"
                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list",
                      "DESCRIPTION":"A list of presets that can be loaded"
                    },
                    "save":{
                      "FULL_PATH":"/rnbo/inst/0/presets/save",
                      "TYPE":"s",
                      "VALUE":"",
                      "ACCESS":2,
                      "CLIPMODE":"none",
                      "DESCRIPTION":"Save the current settings as a preset with the given name"
                    },
                    "load":{
                      "FULL_PATH":"/rnbo/inst/0/presets/load",
                      "TYPE":"s",
                      "VALUE":"",
                      "ACCESS":2,
                      "CLIPMODE":"none",
                      "DESCRIPTION":"Load a preset with the given name"
                    },
                    "initial":{
                      "FULL_PATH":"/rnbo/inst/0/presets/initial",
                      "TYPE":"s",
                      "VALUE":"",
                      "ACCESS":3,
                      "CLIPMODE":"none",
                      "DESCRIPTION":"Indicate a preset, by name, that should be loaded every time this patch is reloaded. Set to an empty string to load the loaded preset instead"
                    }
                  }
                },
                "midi":{
                  "FULL_PATH":"/rnbo/inst/0/midi",
                  "CONTENTS":{
                    "in":{
                      "FULL_PATH":"/rnbo/inst/0/midi/in",
                      "TYPE":"",
                      "VALUE":[

                      ],
                      "ACCESS":2,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list",
                      "DESCRIPTION":"midi events in to your RNBO patch"
                    },
                    "out":{
                      "FULL_PATH":"/rnbo/inst/0/midi/out",
                      "TYPE":"",
                      "VALUE":[

                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list",
                      "DESCRIPTION":"midi events out of your RNBO patch"
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
```

Those `FULL_PATH` entires correspond to `OSC` addresses, and the `TYPE`
identifies the `OSC` type that those parameters expect, if any.

If the `ACCESS` value is `2` (set only) or `3` (get set) then you can send OSC
messages to that address to alter parameters.

Most of what you'll want to interact with will be below the `/rnbo/inst/0`
path, this is the path that identifies the running codegen export.

See the [OSCQueryProposal](https://github.com/Vidvox/OSCQueryProposal) for more details on OSCQuery.

### Basic Javascript Websocket Example

```javascript
const OSC = require("osc");

{
	let ws = new WebSocket(YOUR_RUNNER_URL);

	ws.on('message', (d) => {
		//must be a buffer because there are other non OSC websocket messages as well
		if (Buffer.isBuffer(d)) {
			try {
				const msg = OSC.readPacket(d, {metadata: true});
				//process
			} catch (e) {
			}
		}
	});

	ws.on('open', () => {
		//send OSC
		const array = OSC.writePacket({
			address: "/rnbo/inst/0/params/foo",
			args: [
				{
					type: "f",
					value: 1.0
				}
			]
		},
		{ metadata: true });
		ws.send(array);
	});
}
```

### Commandline OSC Example

Here we use `oscsend`, which is available in homebrew, to send a normalized
parameter update to `c74rpi.local`.

```shell
oscsend osc.udp://c74rpi.local:1234 /rnbo/inst/0/params/foo/normalized f 0.2
```

If `foo` is a valid parameter in your loaded patch, and you send that, then
load `http://c74rpi.local:5678/rnbo/inst/0/params/` in a webbrowser, you should
see that both `foo` and `foo/normalized` have been updated.


## Commands

Uses a modified [jsonRPC](https://www.jsonrpc.org/specification) for comand communication.

modifications:
  * `id` is a uuid.
  * method calls may have multiple responces indicating progress.

misc commands worth exploring
```
oscsend osc.udp://localhost:1234 /rnbo/cmd s '{"method": "file_read", "id": "foo", "params": {"filetype": "sets_presets", "filename": "foo", "size": 50000}}'
oscsend osc.udp://localhost:1234 /rnbo/cmd s '{"method": "file_read", "id": "foo", "params": {"filetype": "set_preset", "filename": "foo", "name": "x", "size": 50000}}'
```

## Notes/Links

* [rnbo](https://cycling74.com/products/rnbo)
* [libossia](https://ossia.io/site-libossia/about.html)
  * [thread safety](https://github.com/ossia/libossia/issues/656)
* [osc.js](https://github.com/colinbdclark/osc.js)

### To add an OSC listener

If the runner is running on the same machine as you want to listen on, you can use `localhost` for the `ip`.

```
oscsend osc.udp://localhost:1234 /rnbo/cmd s '{"method": "listener_add", "id": "foo", "params": {"ip": "localhost", "port": 9999}}'
oscsend osc.udp://localhost:1234 /rnbo/cmd s '{"method": "package_create", "id": "foo", "params": {"set": "granulator"}}'
oscsend osc.udp://localhost:1234 /rnbo/cmd s '{"method": "package_create", "id": "foo", "params": {"all": true}}'
oscsend osc.udp://localhost:1234 /rnbo/cmd s '{"method": "package_install", "id": "foo", "params": {"filename": "graph-granulator-rnbo-1.4.0-control.11.rnbopack"}}'
oscsend osc.udp://localhost:1234 /rnbo/cmd s '{"method": "db_backup", "id": "foo", "params": {}}'
oscsend osc.udp://localhost:1234 /rnbo/cmd s '{"method": "db_backup", "id": "foo", "params": {"name": "foo"}}'
oscsend osc.udp://localhost:1234 /rnbo/cmd s '{"method": "db_restore", "id": "foo", "params": {"name": "foo.sqlite"}}'
```

### Packages

Packages are simply tar files with a custom extension `.rnbopack`

You can open the tar file and edit it and the tar it again, but if you do that
on a mac you may have some errors installing because mac's tar creates some
extra files that the runner doesn't expect.

To correctly tar a directory on mac you can use these extra flags:

```
tar cvf the-name-of-my-pack-RNBOVERSION.rnbopack --no-mac-metadata --no-xattrs the-name-of-my-pack-RNBOVERSION
```

The runner expects that `the-name-of-my-pack-RNBOVERSION.rnbopack` has a single directory in it named `the-name-of-my-pack-RNBOVERSION`
You'll want to replace the `RNBOVERSION` with the actual version of RNBO that the package is targeted for.

### Metadata

If you have your own uses for the `meta` entry, you can add anything you'd like but it has to be a `JSON` key-value map at the top level.

The runner supports the following `meta` entries directly.

#### OSC

Inports, Outports, and Parameters can take metadata that extend their mapping to/from OSC messages.

*Format*

The simplest of forms, `{"osc": "/foo/bar"}` maps the item to/from the OSC message `/foo/bar`.
You can also use a more verbose format `{"osc": {"addr": "/your/addr", "out": true}}`

*Direction*

* Inports can only listen to OSC messages
* Outports can only send OSC messages
* Parameters only listen by default but can be made to send with a more verbose OSC meta entry: `{"osc":{"addr": "/foo/bar/", "out": true}}`
  * **NOTE**: a parameter with the above meta will only send on `/foo/bar`, it will not also listen.
  If you want to do both you need to add, `"in": true` eg `{"osc":{"addr": "/foo/bar/", "out": true, "in": true}}`

*Normalized*

By default parameter OSC values map to/from unnormalized values but if you add `"norm": true` to your meta you map to/from normalized values.

*Misc notes*

* Inport and Outports default to mapping to/from OSC addresses if you prefix their name with a `/`, for instance `[inport /synth/freq]`
  * You can toggle this behavior with the `Instance: Port To OSC` setting in the [Web Interface](https://rnbo.cycling74.com/learn/raspberry-pi-web-interface-guide) settings.
  * You an disable OSC mapping for an inport or outport by setting its meta `{"osc": false}`

#### MIDI

Parameters and Inports support MIDI mapping via a `midi` entry in their metadata. The [Web Interface](https://rnbo.cycling74.com/learn/raspberry-pi-web-interface-guide)
now helps automate setting this value but you can set it explicitly if you prefer.

*Misc notes*

* As of this writing the MIDI value is scaled to `0..1` and applied, without any additional augmentation, to the normalized value for a parameter.
    * Values aren't scaled before sent to an Inport.
* Notes to Params simply map to `0` for note off and `1` for note on indendent of velocity.
* When you map a MIDI message, it is filtered out and not sent along your patcher beyond setting the parameter/inport value(s) it is associated with.
* The `chan` entry is `1` based, so valid values are `1-16`.
* The `chan` entry is optional and defaults to `1` if it isn't present.

*midi JSON format*

* multiple per channel mappings
  * note: `{"note": 2, "chan": 10}`
  * controller change: `{"ctrl": 5, "chan": 10}`
  * key pressure: `{"keypress": 1, "chan": 1}`
* one per channel mappings
  * pitch bend: `{"bend": 1}`
  * program change: `{"prgchg": 10}`
  * channel pressure: `{"chanpress": 1}`

As an example, a parameter might have meta with: `{"midi": {"ctrl": 4, "chan": 16}}`.
This would map controller change 4 on channel 16's value, scaled to 0..1 to the parameter's normalized value.

#### Buffers

Buffers support the `meta` entry. The `share` and `observe` keys let you share buffers between your devices.
The `system` key attempts to load your buffer into shared memory so it can be viewed by other applications running on your system.

**NOTE** - calling "resize" on a shared or system buffer might reallocate the buffer and lose its shared or system status.
Also, the runner can execute devices in parallel sometimes so it is up to you to make sure that your devices are reading from valid locations in your buffers.
You can make sure that devices aren't running at the same time by connecting them in series with audio connections.

* `"share": "<sharekey>"` - specify that this buffer should be shared with the specified key.
* `"observe": "<sharekey>"` - specify that this buffer should be loaded with the data of the buffer shared with the specified key.
* `"system": true` - specify that this buffer should be loaded into shared memory if possible.

### Testing out discovery

`dns-sd` can show you available services:

```shell
dns-sd -B _oscjson._tcp
```

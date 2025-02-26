# Raspberry Pi Setup

## Normal use

You can install rnbooscquery on an existing bookworm image or start from scratch.
Feel free to customize your hostname and password, but at this time you should
keep the user name `pi`.

* Install latest `bookworm 32-bit` OS with [raspberry pi imager](https://www.raspberrypi.com/software/)
  * use the gear icon to set your hostname, password, and enable SSH
    * we use `c74rpi` for the host name and `c74rnbo` for the initial password
  * do not change the user name, it needs to be `pi`
  * optionally setup wireless lan (wifi)
* Boot the Pi with the `bookworm` SD card you just created, connect via Ethernet to the host machine.
* send over files that are needed for the repo:
  ```shell
  rsync config/apt-cycling74-pubkey.asc config/cycling74.list pi@c74rpi.local:
  ```
* setup pi (pw: `c74rnbo`)
  * disable screen reader
  * setup our private apt repo
  * uninstall pulse audio
  * install some packages, including rnbooscquery
  * set the CPU to not scale
  * disable RemoveIPC
    * https://www.man7.org/linux/man-pages/man5/logind.conf.5.html
  * enable realtime
  * install rnbooscquery
  * reboot (easiest way to update your group security settings)

  ssh to do the pi and get into sudo.

  ```shell
  ssh pi@c74rpi.local
  sudo -s
  ```
  add the c74 apt repo, install/setup.

  ```shell
  rm -f /etc/xdg/autostart/piwiz.desktop
  mv apt-cycling74-pubkey.asc /usr/share/keyrings/
  mv cycling74.list /etc/apt/sources.list.d/
  apt update
  apt install --no-install-recommends jackd2 ccache cpufrequtils
  echo "GOVERNOR=\"performance\"" > /etc/default/cpufrequtils
  echo "RemoveIPC=no" >> /etc/systemd/logind.conf
  apt -y upgrade
  apt-get -y autoremove
  ```

  Configure jack for realtime if you didn't when you installed jackd2
  ```shell
  dpkg-reconfigure jackd2
  ```

  Optionally, enable the dummy audio interface. This allows for operation without an audio interface, for instance, for MIDI only.
  ```shell
  echo snd-dummy >> /etc/modules
  ```

  Install a specific verison and hold it there. You'll want to update the
  version string to be in line with the version of RNBO you want to start out
  with. You can see all the versions available with `apt-cache madison rnbooscquery`

  ```shell
  apt-get install -y --allow-change-held-packages --allow-downgrades rnbooscquery=1.3.2
  apt-get install -y jack_transport_link rnbo-runner-panel
  apt-mark hold rnbooscquery
  ```

  At this point you should be all set to go, best to reboot to make sure that
  the jack realtime mode is set up for the pi user.

  ```shell
  reboot
  ```

### Beta

  If you're configuring for beta usage, you'll want to add the `beta` component to
  `/etc/apt/sources.list.d/cycling74.list`

## Wifi Setup

See the official documentation [Setting up a wireless LAN via the command line](https://www.raspberrypi.org/documentation/configuration/wireless/wireless-cli.md)

## Development

Do all the normal use stuff then:

* Update, upgrade, install packages
* setup python3
* install conan
* make directories for local builds and config
  ```shell
  sudo -s
  apt-get update && apt-get upgrade
  apt-get -y install libavahi-compat-libdnssd-dev build-essential libssl-dev libjack-jackd2-dev libdbus-1-dev libxml2-dev libgmock-dev google-mock libsdbus-c++-dev libsndfile1-dev cmake
  apt-get -y --no-install-recommends install ruby python3-pip
  update-alternatives --install /usr/bin/python python /usr/bin/python2.7 1
  update-alternatives --install /usr/bin/python python /usr/bin/python3.7 2
  exit
  pip3 install --break-system-packages --user conan==1.61.0
  mkdir -p ~/.conan/profiles/
  mkdir -p ~/local/src/
  ```
* send over conan default profile (from host pc)
  ```shell
  rsync config/conan-rpi-default pi@c74rpi.local:.conan/profiles/default
  ```

**NOTE** at this point you can save the SD image for future *fresh* images.

### Copy and Build runner

* copy runner to the pi (from your host PC):
  ```shell
  ./scripts/deploy.rb pi@c74rpi.local
  ```
* build and install the runner (on pi)
  ```shell
  ssh pi@c74rpi.local
  cd ~/local/src/rnbo.oscquery.runner/ && mkdir build && cd build && CC=gcc CXX=g++ ASMFLAGS="-mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard" CFLAGS="-mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard" CXXFLAGS="-mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard" cmake -DCMAKE_BUILD_TYPE=Release  .. && cmake --build . && cpack

  sudo dpkg -i *.deb
  ```
  * you could also do a standard make install, but then you'll need to setup the service file yourself.
  ```shell
  sudo make install && ldconfig
  ```

### Install the service file

  **NOTE**:
  You should only need to do this the first time you install a dev version with
  `make install`, or if the service file contents changes. If you install via a
  `.deb` file (as detailed above), you shouldn't need to do this at all.

  From whereever `rnbooscquery.service` is, either `cd ~/local/src/rnbo.oscquery.runner/config/` or maybe its in your homedir.

  ```shell
  sudo -s
  cp rnbooscquery.service /lib/systemd/system/rnbooscquery.service
  chown root:root /lib/systemd/system/rnbooscquery.service
  chmod 644 /lib/systemd/system/rnbooscquery.service
  systemctl daemon-reload
  systemctl enable rnbooscquery.service
  reboot
  ```

## Troubleshooting

  To see the status of the service:

  ```
  journalctl -u rnbooscquery
  ```

# Backup your SD card

* find the device:
  * mac: `diskutil list`
    ```shell
    ...
/dev/disk5 (external, physical):
   #:                       TYPE NAME                    SIZE       IDENTIFIER
   0:     FDisk_partition_scheme                        *31.9 GB    disk5
   1:             Windows_FAT_32 boot                    268.4 MB   disk5s1
   2:                      Linux                         31.6 GB    disk5s2
    ```
* backup with dd (**BE CAREFUL**):
  ```shell
  sudo dd if=/dev/disk5 of=2020-12-02-raspios-bookworm-armhf-setup.dmg bs=1024k
  ```
  *note* if you're on linux you can add `status=progress` to see the progress
* shrink, on linux:
  * shrink with: [PiShrink](https://github.com/Drewsif/PiShrink)


# Transport

The runner is setup to synchronize to [Jack Transport](https://jackaudio.org/api/transport-design.html).

## Ableton's Link

In order to sync your runner with external clients, you can use the [jack_transport_link](https://github.com/x37v/jack_transport_link) application.
This drives the Jack Transport and synchronizes it with other [Link](https://www.ableton.com/en/link/) clients on your network.
By default, this application is automatically installed on the rpi if you've added the `extra` component to your apt entry.
`extra` will be added by default for images for rnbo `0.11.0` and later.

If `jack_transport_link` is running, you should be able to control your tempo and start/stop the transport with both OSCQuery methods as well as with external link clients.


# Package management

## Aptly

Aptly lets us manage a package repository and push it up to an s3 compatible
location and then allow our users to get these packages with `apt-get`.

[create gpg key](https://fedoraproject.org/wiki/Creating_GPG_Keys)

A cycling74 pub/priv key are in 1password. The pub key is also in `config/apt-cycling74-pubkey.asc`
The `aptly.conf` file is there too. At this point that should all be managed by the build machine.

```shell
brew install aptly
brew install gnupg
```

to create a package and upload it, from a mac, in the root of this repo.
```shell
./build-rpi.sh
aptly -distribution=bookworm repo create bookworm-rpi
aptly repo create -distribution=bookworm -component=extra bookworm-rpi-extra
aptly repo create -distribution=bookworm -component=beta bookworm-rpi-beta
aptly repo add bookworm-rpi-extra ~/Documents/bookworm/jack_transport_link_0.0.8-1_armhf.deb
aptly repo add bookworm-rpi ./examples/rnbo.oscquery.runner/update/build-rpi/rnbo-update-service_0.2.6-1_armhf.deb
aptly publish repo -component=,, -passphrase-file=/home/runner/.apt-gpg.txt bookworm-rpi bookworm-rpi-extra bookworm-rpi-beta s3:c74:
```

to update the repo
```shell
aptly publish update bookworm s3:c74:
```

to overwrite a package
```shell
aptly -force-replace repo add bookworm-rpi examples/rnbo.oscquery.runner/build-rpi/rnbooscquery_0.9.0.deb
aptly -force-overwrite publish update bookworm s3:c74:
```

to remove a package from a repo.. (dry-run)

```shell
aptly repo remove -dry-run buster-rpi 'Name (% rnbooscquery), Version (% 0.17.0-dev.43)'
```

## List installed version of package

```shell
apt-cache policy rnbooscquery
```

## List all versions of a package

```shell
apt-cache madison rnbooscquery
```

## How to hold a package and then install a specific version

* mark it held:
  ```shell
  apt-mark hold rnbooscquery
  ```
* list held packages
  ```shell
  apt-mark showhold
  ```
* install a specific version while still held, allowing downgrades and upgrades.
  ```shell
  apt install -y --allow-change-held-packages --allow-downgrades rnbooscquery=1.10-2ubuntu1
  ```
* but it looks like you have to hold it again after
  ```shell
  apt-mark hold rnbooscquery
  ```
* unhold
  ```shell
  apt-mark unhold rnbooscquery
  ```
# To prepare a new image

Assuming the runner version to install is `0.11.0`

Boot into an old image, update the packages, install the new version, mark the package held, upgrade other packages, cleanup.

```shell
  sudo -s
  apt-get update
  apt-get install -y --allow-change-held-packages --allow-downgrades rnbooscquery=1.3.0-dev.75
  apt-mark hold rnbooscquery
  apt-get -y upgrade && apt-get -y autoremove && apt-get -y clean
  journalctl --vacuum-time=2d
  rm -r ~pi/.config/rnbo ~pi/Documents/rnbo/ ~pi/.bash_history
  shutdown -P -h now
```

Then remove the SD card and do a backup.

# TODO

* [Watchdog?](https://madskjeldgaard.dk/posts/raspi4-notes/#watchdog)
* Ubuntu + [kxstudio](https://kx.studio/Repositories:Extras) for newer jack etc?

# More reading

* [linux audio rpi notes](https://wiki.linuxaudio.org/wiki/raspberrypi)
* https://ma.ttias.be/auto-restart-crashed-service-systemd/
* [jack systemd service](https://bbs.archlinux.org/viewtopic.php?id=165545)
* [jack 2 systemd service](https://raspberrypi.stackexchange.com/questions/112195/jack-audio-server-can-start-on-cli-but-not-as-a-systemd-service)
* [headless rpi setup](https://desertbot.io/blog/headless-raspberry-pi-4-ssh-wifi-setup)
* [cross compilers](https://github.com/abhiTronix/raspberry-pi-cross-compilers)



# PI 5 notes

## With pipewire

used default conan profile but set

```
compiler.libcxx=libstdc++11
```

```
CC=gcc CXX=g++ ASMFLAGS="-mcpu=cortex-a76" CFLAGS="-mcpu=cortex-a76" CXXFLAGS="-mcpu=cortex-a76" cmake -DCMAKE_BUILD_TYPE=Release  -DWITH_DBUS=Off .. && cmake --build .
```

```
sudo apt-get install pipewire-jack
wpctl status
pw-jack ./bin/rnbooscquery

systemctl --user daemon-reload
systemctl --user enable rnbooscquery.service
systemctl --user is-enabled  rnbooscquery.service
systemctl --user start rnbooscquery.service
journalctl --user -u rnbooscquery
```

works but pipewire is a little bit of pain to control remotely, so we're gonna install jackd2

## Without pipewire

same build steps as above

```
apt remove pipewire
apt autoremove
```

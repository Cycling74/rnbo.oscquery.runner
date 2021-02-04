# Raspberry Pi Setup

## Normal use

* [inital setup](https://desertbot.io/blog/headless-raspberry-pi-4-ssh-wifi-setup)
  * I used `Raspberry Pi OS with desktop`.
  * The important detail is that you need to create a file `/boot/ssh` to enable ssh:
    * with the SD card mounted on mac: `touch /Volumes/boot/ssh`
* Boot the Pi, connect via Ethernet to the host machine.
* send over files that are needed for the repo:  (pw: `raspberry`)
  ```shell
  rsync config/apt-cycling74-pubkey.asc config/cycling74.list pi@raspberrypi.local:
  ```
* setup pi (pw: `raspberry`)
  * disable screen reader
  * setup our private apt repo
  * uninstall pulse audio
  * install jackd2 and rnbooscquery
  * enable relatime
    * reboot (easiest way to update your group security settings)
  ```shell
  ssh pi@raspberrypi.local
  sudo -s
  rm -f /etc/xdg/autostart/piwiz.desktop
  apt-key add apt-cycling74-pubkey.asc
  mv cycling74.list /etc/apt/sources.list.d/
  apt update
  apt -y remove pulseaudio libpulse0 pulseaudio-utils libpulsedsp
  apt-get -y autoremove
  apt -y install jackd2 rnbooscquery
  dpkg-reconfigure jackd2
  reboot
  ```

## Development

* [inital setup](https://desertbot.io/blog/headless-raspberry-pi-4-ssh-wifi-setup)
  * I used `Raspberry Pi OS with desktop`.
  * The important detail is that you need to create a file `/boot/ssh` to enable ssh:
    * with the SD card mounted on mac: `touch /Volumes/boot/ssh`
* Boot the Pi, connect via Ethernet to the host machine: `ssh pi@raspberrypi.local` (pw: `raspberry`)
* Set new password and hostname:
  * `sudo raspi-config`
    Change User Password: `c74rnbo`
    Network Options > Hostname: `c74rpi`
    Update
* Reboot: `sudo reboot`
* Log back into the Pi using new credentials: `ssh pi@c74rpi.local` (pw: `c74rnbo`)
* Update, upgrade, install packages:
  ```shell
  sudo -s
  apt-get update && apt-get upgrade -y
  apt-get -y install libavahi-compat-libdnssd-dev build-essential libsndfile1-dev libssl-dev libjack-jackd2-dev libboost1.67-all-dev libdbus-cpp-dev
  apt-get -y --no-install-recommends install jackd2 ruby python3-pip
  ```
* uninstall pulse audio
  ```shell
  sudo apt-get remove pulseaudio libpulse0 pulseaudio-utils libpulsedsp && sudo apt-get -y autoremove
  ```
* Configure realtime
  ```shell
  sudo dpkg-reconfigure jackd2
  ```
* setup python3
  ```shell
  sudo -s
  update-alternatives --install /usr/bin/python python /usr/bin/python2.7 1
  update-alternatives --install /usr/bin/python python /usr/bin/python3.7 2
  ```
* install conan
  ```shell
  pip3 install conan
  ```
* make directories for local builds and config
  ```shell
  mkdir -p .conan/profiles/
  mkdir -p ~/local/src/
  ```
* build and install the latest [cmake](https://cmake.org/install/)
  * Alex did this in `~/local/src/`
* Disable the screen reader audio prompt at startup
  ```shell
  sudo rm -f /etc/xdg/autostart/piwiz.desktop
  ```
* send over conan default profile (from host pc)
  ```shell
  rsync config/conan-rpi-default pi@c74rpi.local:.conan/profiles/default
  ```
* add the cycling74 pgp pub key (from host pc)
  ```shell
  rsync config/apt-cycling74-pubkey.asc pi@c74rpi.local:
  ```
  on rpi
  ```shell
  sudo apt-key add apt-cycling74-pubkey.asc
  ```
* add the cycling74 apt repo (from host pc)
  ```shell
  rsync config/cycling74.list pi@c74rpi.local:
  ```
  on rpi
  ```shell
  sudo mv cycling74.list /etc/apt/sources.list.d/
  sudo apt update
  ```

**NOTE** at this point you can save the SD image for future *fresh* images.

### Copy and Build runner

* copy runner to the pi (from your host PC):
  ```shell
  ./deploy.rb pi@c74rpi.local
  ```
* build and install the runner (on pi)
  ```shell
  cd ~/local/src/RNBOOSCQueryRunner/ && mkdir build && cd build && cmake .. && make
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

  From whereever `rnbooscquery.service` is, either `cd ~/local/src/RNBOOSCQueryRunner/config/` or maybe its in your homedir.

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

## Install a prebuilt binary

  You're probably better off just using the .deb install.

  ```shell
  scp examples/RNBOOSCQueryRunner/config/rnbooscquery.service examples/RNBOOSCQueryRunner/build-rpi/rnbooscquery-*-Linux-armv7.tar.gz pi@c74rpi.local:
  ssh pi@c74rpi.local
  sudo tar xvf rnbooscquery-0.9.0-Linux-armv7.tar.gz -C /usr/local/
  sudo ldconfig
  ```

  Install the service file if you haven't already. If you have, then just restart the service:

  ```shell
  sudo service rnbooscquery restart
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
  sudo dd if=/dev/disk5 of=2020-12-02-raspios-buster-armhf-setup.dmg bs=1024k
  ```
  *note* if you're on linux you can add `status=progress` to see the progress
* shrink, on linux:
  * shrink with: [PiShrink](https://github.com/Drewsif/PiShrink)


# Package management

## Aptly

Aptly lets us manage a package repository and push it up to an s3 compatible
location and then allow our users to get these packages with `apt-get`.


[create gpg key](https://fedoraproject.org/wiki/Creating_GPG_Keys)

A cycling74 pub/priv key are in 1password. The pub key is also in `config/apt-cycling74-pubkey.asc`

```shell
brew install aptly
brew install gnupg
```

to create a package and upload it, from a mac, in the root of this repo.
```shell
./build-rpi.sh
aptly -distribution=buster repo create rnbo
aptly repo add rnbo examples/RNBOOSCQueryRunner/build-rpi/rnbooscquery_0.9.0.deb
aptly publish repo rnbo s3:c74:
```

to update the repo
```shell
aptly publish update buster s3:c74:
```

to overwrite a package
```shell
aptly -force-replace repo add rnbo examples/RNBOOSCQueryRunner/build-rpi/rnbooscquery_0.9.0.deb
aptly -force-overwrite publish update buster s3:c74:
```

## List installed version of package

```shell
apt-cache policy rnbooscquery
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

# TODO

* Github action that builds the runner.
* [Watchdog?](https://madskjeldgaard.dk/posts/raspi4-notes/#watchdog)
* Disable CPU scaling?


# More reading

* [linux audio rpi notes](https://wiki.linuxaudio.org/wiki/raspberrypi)
  disable cpu scaling
  ```shell
  for cpu in /sys/devices/system/cpu/cpu[0-9]*; do echo -n performance | sudo tee $cpu/cpufreq/scaling_governor; done
  ```
* https://ma.ttias.be/auto-restart-crashed-service-systemd/
* [jack systemd service](https://bbs.archlinux.org/viewtopic.php?id=165545)
* [jack 2 systemd service](https://raspberrypi.stackexchange.com/questions/112195/jack-audio-server-can-start-on-cli-but-not-as-a-systemd-service)
* [headless rpi setup](https://desertbot.io/blog/headless-raspberry-pi-4-ssh-wifi-setup)
* [cross compilers](https://github.com/abhiTronix/raspberry-pi-cross-compilers)



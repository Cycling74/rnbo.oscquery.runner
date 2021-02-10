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
  * install some packages, including rnbooscquery
  * enable relatime
  * update host name
  * update password
  * set the CPU to not scale
  * reboot (easiest way to update your group security settings)

  you should be able to copy and paste this and do it all at once
  ```shell
  ssh pi@raspberrypi.local
  sudo -s
  export NEW_HOST_NAME=c74rpi
  rm -f /etc/xdg/autostart/piwiz.desktop
  apt-key add apt-cycling74-pubkey.asc
  mv cycling74.list /etc/apt/sources.list.d/
  apt -y remove pulseaudio libpulse0 pulseaudio-utils libpulsedsp
  apt update
  apt -y upgrade
  apt-get -y autoremove
  apt -y install jackd2 ccache cpufrequtils
  sed -i 's/127.0.1.1.*/127.0.1.1\t'"$NEW_HOST_NAME"'/g' /etc/hosts
  hostnamectl set-hostname ${NEW_HOST_NAME}
  echo "pi:c74rnbo" | chpasswd
  echo "GOVERNOR=\"performance\"" > /etc/default/cpufrequtils
  ```

  do these one at a time
  ```shell
  dpkg-reconfigure jackd2
  reboot
  ```

  now you have a base package, to setup rnbooscquery

  ```shell
  apt -y install rnbooscquery
  ```

## Development

Do all the normal use stuff then:

* Update, upgrade, install packages
* setup python3
* install conan
* make directories for local builds and config
  ```shell
  sudo apt-get -y install libavahi-compat-libdnssd-dev build-essential libsndfile1-dev libssl-dev libjack-jackd2-dev libboost1.67-all-dev libdbus-cpp-dev
  sudo apt-get -y --no-install-recommends install ruby python3-pip
  sudo update-alternatives --install /usr/bin/python python /usr/bin/python2.7 1
  sudo update-alternatives --install /usr/bin/python python /usr/bin/python3.7 2
  pip3 install conan
  mkdir -p ~/.conan/profiles/
  mkdir -p ~/local/src/
  ```
* build and install the latest [cmake](https://cmake.org/install/)
  * Alex did this in `~/local/src/`
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
* Ubuntu + [kxstudio](https://kx.studio/Repositories:Extras) for newer jack etc?

# More reading

* [linux audio rpi notes](https://wiki.linuxaudio.org/wiki/raspberrypi)
* https://ma.ttias.be/auto-restart-crashed-service-systemd/
* [jack systemd service](https://bbs.archlinux.org/viewtopic.php?id=165545)
* [jack 2 systemd service](https://raspberrypi.stackexchange.com/questions/112195/jack-audio-server-can-start-on-cli-but-not-as-a-systemd-service)
* [headless rpi setup](https://desertbot.io/blog/headless-raspberry-pi-4-ssh-wifi-setup)
* [cross compilers](https://github.com/abhiTronix/raspberry-pi-cross-compilers)



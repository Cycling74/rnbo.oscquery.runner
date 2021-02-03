# Raspberry Pi Setup

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

**NOTE** at this point you can save the SD image for future *fresh* images.

# Copy and Build runner

**NOTE** this part will likely change to a dpkg based setup.

* copy runner to the pi (from your host PC):
  ```shell
  ./deploy.rb pi@c74rpi.local
  ```
* build and install the runner (on pi)
  ```shell
  cd ~/local/src/RNBOOSCQueryRunner/ && mkdir build && cd build && cmake .. && make
  sudo make install && ldconfig
  ```

# Install the service file

  from wherever `rnbo.service` is, either `cd ~/local/src/RNBOOSCQueryRunner/config/` or maybe its in your homedir.

  ```shell
  sudo -s
  cp rnbo.service /lib/systemd/system/rnbo.service
  chown root:root /lib/systemd/system/rnbo.service
  chmod 644 /lib/systemd/system/rnbo.service
  systemctl daemon-reload
  systemctl enable rnbo.service
  reboot
  ```

  see the status of the service:
  ```
  journalctl -u rnbo
  ```

# Install a prebuilt binary

  ```shell
  scp examples/RNBOOSCQueryRunner/config/rnbo.service examples/RNBOOSCQueryRunner/build-rpi/rnbo-oscquery-*-Linux-armv7.tar.gz pi@c74rpi.local:
  ssh pi@c74rpi.local
  sudo tar xvf rnbo-oscquery-0.9.0-Linux-armv7.tar.gz -C /usr/local/
  sudo ldconfig
  ```

  Install the service file if you haven't already. If you have, then just restart the service:

  ```shell
  sudo service rnbo restart
  ```

# Backup

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

# TODO

* Github action that builds the runner.
* dpkg or apt based installation.
* [Watchdog?](https://madskjeldgaard.dk/posts/raspi4-notes/#watchdog)


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



# Package management

## List installed version

```shell
apt-cache policy gzip
```

## How to hold a package and then install a specific version

* mark it held:
  ```shell
  apt-mark hold gzip
  ```
* list held packages
  ```shell
  apt-mark showhold
  ```
* install a specific version while still held, allowing downgrades and upgrades.
  ```shell
  apt install -y --allow-change-held-packages --allow-downgrades gzip=1.10-2ubuntu1
  ```
* but it looks like you have to hold it again after
  ```shell
  apt-mark hold gzip
  ```
* unhold
  ```shell
  apt-mark unhold gzip
  ```

## Aptly

[create gpg key](https://fedoraproject.org/wiki/Creating_GPG_Keys)

A cycling74 pub/priv key are in 1password. The pub key is also in `config/apt-cycling74-pubkey.asc`

```shell
brew install aptly
brew install gnupg
```


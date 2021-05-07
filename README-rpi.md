# Raspberry Pi Setup

## Normal use

You can install rnbooscquery on an existing buster image or start from scratch.
Feel free to customize your hostname and password, but at this time you should
keep the user name `pi`.

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
  * update host name
  * update password
  * disable screen reader
  * setup our private apt repo
  * uninstall pulse audio
  * install some packages, including rnbooscquery
  * set the CPU to not scale
  * enable realtime
  * install rnbooscquery
  * reboot (easiest way to update your group security settings)

  ssh to do the pi and get into sudo.

  ```shell
  ssh pi@raspberrypi.local
  sudo -s
  ```

  Setup the initial host, you can customize this with your own hostname and
  password if you want, or skip it if you've already set up a buster image
  that you just want to run rnbo on. You should keep the user name `pi` though.
  This part is optional.

  ```shell
  export NEW_HOST_NAME=c74rpi
  sed -i 's/127.0.1.1.*/127.0.1.1\t'"$NEW_HOST_NAME"'/g' /etc/hosts
  hostnamectl set-hostname ${NEW_HOST_NAME}
  echo "pi:c74rnbo" | chpasswd
  ```

  Remove some stuff that causes problems, add the c74 apt repo, install/setup.

  ```shell
  rm -f /etc/xdg/autostart/piwiz.desktop
  apt-key add apt-cycling74-pubkey.asc
  mv cycling74.list /etc/apt/sources.list.d/
  apt -y remove pulseaudio libpulse0 pulseaudio-utils libpulsedsp
  apt update
  apt -y upgrade
  apt-get -y autoremove
  apt -y install jackd2 ccache cpufrequtils
  echo "GOVERNOR=\"performance\"" > /etc/default/cpufrequtils
  ```

  configure jack for realtime
  ```shell
  dpkg-reconfigure jackd2
  ```

  Install a specific verison and hold it there. You'll want to update the
  version string to be in line with the version of RNBO you want to start out
  with. You can see all the versions available with `apt-cache madison rnbooscquery`

  ```shell
  apt-get install -y --allow-change-held-packages --allow-downgrades --install-recommends --install-suggests rnbooscquery=0.11.0-xnor-jack2-properties-server.0
  apt-mark hold rnbooscquery
  ```

  At this point you should be all set to go, best to reboot to make sure that
  the jack realtime mode is set up for the pi user.

  ```shell
  reboot
  ```

## Wifi Setup

See the official documentation [Setting up a wireless LAN via the command line](https://www.raspberrypi.org/documentation/configuration/wireless/wireless-cli.md)

## Development

Do all the normal use stuff then:

* Update, upgrade, install packages
* setup python3
* install conan
* make directories for local builds and config
  ```shell
  sudo apt-get -y install libavahi-compat-libdnssd-dev build-essential libsndfile1-dev libssl-dev libjack-jackd2-dev libdbus-1-dev libxml2-dev libgmock-dev google-mock
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
  cd ~/local/src/RNBOOSCQueryRunner/ && mkdir build && cd build && cmake .. && make && cpack
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
aptly -distribution=buster repo create buster-rpi
aptly repo add buster-rpi examples/RNBOOSCQueryRunner/build-rpi/rnbooscquery_0.9.0.deb
aptly publish repo -component=, buster-rpi buster-rpi-extra s3:c74:
```

to update the repo
```shell
aptly publish update buster s3:c74:
```

to overwrite a package
```shell
aptly -force-replace repo add buster-rpi examples/RNBOOSCQueryRunner/build-rpi/rnbooscquery_0.9.0.deb
aptly -force-overwrite publish update buster s3:c74:
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
  apt-get install -y --allow-change-held-packages --allow-downgrades --install-recommends --install-suggests rnbooscquery=0.11.0
  apt-mark hold rnbooscquery
  apt-get -y upgrade && apt-get -y autoremove && apt-get -y clean
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



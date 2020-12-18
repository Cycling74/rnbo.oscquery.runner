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
  apt-get -y --no-install-recommends install jackd2 libavahi-compat-libdnssd-dev build-essential libsndfile1-dev libssl-dev libjack-jackd2-dev ruby
  ```
* uninstall pulse audio
  ```shell
  sudo apt-get remove pulseaudio && sudo apt-get -y autoremove
  ```
* Configure realtime
  ```shell
  sudo dpkg-reconfigure jackd2
  ```
* build and install the latest [cmake](https://cmake.org/install/)
* install conan
  ```shell
  pip install conan
  ```
* make directories for local builds and config
  ```shell
  mkdir -p ~/local/src/
  mkdir -p ~/.config/rnbo/
  ```
* download and build libossia
  ```shell
  cd local/src/ && git clone -b develop https://github.com/cycling74/libossia
  cd libossia && mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug -DOSSIA_CPP=ON -DOSSIA_CPP_ONLY=ON -DOSSIA_DATAFLOW=OFF -DOSSIA_EDITOR=OFF -DOSSIA_GFX=OFF -DOSSIA_PCH=OFF -DOSSIA_PROTOCOL_ARTNET=OFF -DOSSIA_PROTOCOL_AUDIO=OFF -DOSSIA_PROTOCOL_MIDI=OFF -DOSSIA_PROTOCOL_WIIMOTE=OFF -DOSSIA_PROTOCOL_JOYSTICK=OFF
  make -j4 && sudo make install
  ```

# Copy and Build runner

**NOTE** this part will likely change to a dpkg based setup.

* copy runner to the pi (from your host PC):
  ```shell
  ./deploy.rb pi@c74rpi.local
  ```
* build the runner (on pi)
  ```shell
  cd ~/local/src/RNBOOSCQueryRunner/runner/ && mkdir build && cd build && cmake .. && make
  ```

# Update the config and copy to the needed location:
  ```shell
  cd ~/local/src/RNBOOSCQueryRunner/
  vi runner-rpi.json
  cp runner-rpi.json ~/.config/rnbo/runner.json
  ```

# Install the service file


  ```shell
  cd ~/local/src/RNBOOSCQueryRunner/
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

# Disable the screen reader audio prompt at startup

```shell
sudo rm -f /etc/xdg/autostart/piwiz.desktop
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
  sudo dd if=/dev/disk5 of=2020-12-02-raspios-buster-armhf-setup.dmg bs=1024k status=progress
  ```
* shrink, on linux:
  * shrink with: [PiShrink](https://github.com/Drewsif/PiShrink)

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

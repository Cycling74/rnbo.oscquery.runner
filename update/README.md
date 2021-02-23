# rnbo-update-service

This is a DBus based service for Linux that lets us selectively install versions of the rnbo oscquery runner.
It should run as root.

## TODO

* Allow for querying the number of packages that want updates, indicate if any are security updates.

## Dependencies

```shell
sudo apt install ruby-dbus
```

## Package for Debian systems

### Dependencies

```shell
apt-get install dh-make devscripts
```

### Instructions

There is a script that *should* build the package for you. It has been setup on
Debian buster, rpi. It may need some alterations for other systems. Your best
bet there is to comment out the *debuild* system command at the end and then
edit the files in `build/rnbo-update-service-<version>` then run the `debuild`
command from there.

```shell
ruby package.rb
```

## Package Installation

If you've built a package, just update this for your current version:

```shell
sudo dpkg -i ./build/rnbo-update-service_0.1.0-1_all.deb
```

Though, ideally you'll push this to an apt repository so you can `apt install rnbo-update-service`

## Manual Installation

There are 2 dbus configuration files, one describing the namespace and one identifying permissions.

There is also a systemd service file, `rnbo-update-service.service`

Here is how I install it all:

```shell
sudo -s
cp rnbo-update-service /usr/bin/ && \
  cp com.cycling74.rnbo.conf /usr/share/dbus-1/system.d/ && \
  cp com.cycling74.rnbo.xml /usr/share/dbus-1/interfaces/ && \
  cp rnbo-update-service.service /lib/systemd/system/ && \
  chown root:root /usr/share/dbus-1/system.d/com.cycling74.rnbo.conf /usr/share/dbus-1/interfaces/com.cycling74.rnbo.xml /lib/systemd/system/rnbo-update.service && \
  chmod 644 /usr/share/dbus-1/system.d/com.cycling74.rnbo.conf /usr/share/dbus-1/interfaces/com.cycling74.rnbo.xml /lib/systemd/system/rnbo-update.service && \
  systemctl reload dbus && \
  systemctl daemon-reload && \
  systemctl enable rnbo-update-service.service && \
  service rnbo-update-service start
```

## Testing

To get status of the service:

```shell
journalctl -u rnbo-update-service
```

The following command should indicate that there are a few methods registered:

```shell
dbus-send --system        --dest=com.cycling74.rnbo --type=method_call    --print-reply          /com/cycling74/rnbo     org.freedesktop.DBus.Introspectable.Introspect
```
dbus-send --system --dest=com.cycling74.rnbo --print-reply /com/cycling74/rnbo org.freedesktop.DBus.Properties.Get string:com.pgaur.GDBUS string:Status

gdbus introspect -r --system -o /com/cycling74/rnbo -d com.cycling74.rnbo

WORKS!!
dbus-send --system --print-reply --dest=com.cycling74.rnbo /com/cycling74/rnbo org.freedesktop.DBus.Properties.Get string:com.cycling74.rnbo string:active
dbus-send --system --print-reply --dest=com.cycling74.rnbo /com/cycling74/rnbo org.freedesktop.DBus.Properties.Get string:com.cycling74.rnbo string:status

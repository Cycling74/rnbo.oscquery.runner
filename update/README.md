# rnbo-update-service

This is a DBus based service for Linux that lets us selectively install versions of the rnbo oscquery runner.
It should run as root.

## TODO

allow for querying the number of packages that want updates, indicate if any are security updates.

## Dependencies

```shell
sudo apt install ruby-dbus
```

## To package

```shell
VERSION=0.1.0
DIR=rnbo-update-service-${VERSION}
mkdir ${DIR}
cp rnbo-update-service ${DIR}/
cd ${DIR}
```

## Installation

There is an example DBus configuration file `rnbo-system.conf` in this directory.
This should go in `/etc/dbus-1/system.d/`
There is also a service file, `rnbo-update.service` that should go in `/lib/systemd/system/`

Here is how I install it all:

```shell
sudo -s
cp rnbo-update-service /usr/bin/ && \
  cp rnbo-system.conf /etc/dbus-1/system.d/ && \
  cp rnbo-update.service /lib/systemd/system/ && \
  chown root:root /etc/dbus-1/system.d/rnbo-system.conf /lib/systemd/system/rnbo-update.service && \
  chmod 644 /lib/systemd/system/rnbo-update.service  /lib/systemd/system/rnbo-update.service && \
  systemctl reload dbus && \
  systemctl daemon-reload && \
  systemctl enable rnbo-update.service && \
  service rnbo-update start
```

## Testing

The following command should indicate that there are a few methods registered:

```shell
dbus-send --system          \
  --dest=com.cycling74.rnbo \
  --type=method_call        \
  --print-reply             \
  /com/cycling74/rnbo       \
  org.freedesktop.DBus.Introspectable.Introspect
```

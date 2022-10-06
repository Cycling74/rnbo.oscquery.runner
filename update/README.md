# rnbo-update-service

This is a systemd based service for Linux that provides a
[DBus](https://www.freedesktop.org/wiki/Software/dbus/) interface and lets us
selectively install versions of the rnbo.oscquery.runner, triggered by dbus methods
from the rnbo.oscquery.runner itself.

This service should be run as root (the systemd configuration does that).

It is currently configured to only allow root to create the service and only
allow the `pi` user to communicate with it, but altering that wouldn't be too
hard.

## TODO

* Allow for querying the number of packages that want updates, indicate if any are security updates.
* Allow for updating other system packages.

## Dependencies

Uses [sdbus-c++](https://github.com/Kistler-Group/sdbus-cpp/blob/master/docs/using-sdbus-c%2B%2B.md).

```shell
sudo apt install cmake
```

## Build and Install on Debian

```shell
mkdir build && cd build && cmake .. && make && cpack && sudo dpkg -i rnbo-update-service_0.1.deb
```

## How to generate the glue files

I do this all on the pi.  Build the project, in the build directory there
should be an exeuctable called `sdbus-c++-xml2cpp`

```shell
./sdbus-c++-xml2cpp ../../config/rnbo-update-service-bindings.xml --adaptor=../src/UpdateServiceServerGlue.h --proxy=../../src/UpdateServiceProxyGlue.h
```

Copy those .h files back to your computer and copy them into their appropriate places

## Testing

To get status of the service:

```shell
journalctl -u rnbo-update-service
```

monitor all the dbus communication with the service:

```shell
dbus-monitor --system path=/com/cycling74/rnbo
```

Check out the status of some properties:

```shell
dbus-send --system --print-reply --dest=com.cycling74.rnbo /com/cycling74/rnbo org.freedesktop.DBus.Properties.Get string:com.cycling74.rnbo string:State
dbus-send --system --print-reply --dest=com.cycling74.rnbo /com/cycling74/rnbo org.freedesktop.DBus.Properties.Get string:com.cycling74.rnbo string:Status
dbus-send --system --print-reply --dest=com.cycling74.rnbo /com/cycling74/rnbo org.freedesktop.DBus.Properties.Get string:com.cycling74.rnbo string:OutdatedPackages
```

Tell the service to install a specific version of the runner:

```shell
dbus-send --system --print-reply --type="method_call" --dest=com.cycling74.rnbo /com/cycling74/rnbo com.cycling74.rnbo.QueueRunnerInstall string:"0.9.0-alpha.0"
```

osc to the runner
```
oscsend osc.udp://xnor-rnbo-rpi.local:1234 /rnbo/cmd s '{"id": "fake-uuid", "method": "install", "params": {"version": "0.9.0-alpha.1"}}'
```

Discover the details of the service
```shell
dbus-send --system --dest=com.cycling74.rnbo --type=method_call --print-reply /com/cycling74/rnbo org.freedesktop.DBus.Introspectable.Introspect
```

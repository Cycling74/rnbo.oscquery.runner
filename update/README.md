# rnbo-update-service

This is a systemd based service for Linux provides a [DBus](https://www.freedesktop.org/wiki/Software/dbus/) interface that lets
us selectively install versions of the RNBOOSCQueryRunner.

This service should be run as root (the systemd configuration does that).

It is currently configured to only allow root to create the service and only
allow the `pi` user to communicate with it, but altering that wouldn't be too
hard.

## TODO

* Allow for querying the number of packages that want updates, indicate if any are security updates.
* Allow for updating other system packages.

## Dependencies

```shell
sudo apt install cmake libboost1.62-all-dev libdbus-cpp-dev libproperties-cpp-dev
```

## Build and Install on Debian

```shell
mkdir build && cd build && cmake .. && make && cpack && sudo dpkg -i rnbo-update-service_0.1.deb
```

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
dbus-send --system --print-reply --dest=com.cycling74.rnbo /com/cycling74/rnbo org.freedesktop.DBus.Properties.Get string:com.cycling74.rnbo string:Active
dbus-send --system --print-reply --dest=com.cycling74.rnbo /com/cycling74/rnbo org.freedesktop.DBus.Properties.Get string:com.cycling74.rnbo string:Status
```

Tell the service to install a specific version of the runner:

```shell
dbus-send --system --print-reply --type="method_call" --dest=com.cycling74.rnbo /com/cycling74/rnbo com.cycling74.rnbo.QueueRunnerInstall string:"0.9.0-alpha.0"
```

# rnbo-update-service

This is a DBus based service for Linux that lets us selectively install versions of the rnbo oscquery runner.
It should run as root.

There is an example DBus configuration file `rnbo-system.conf` in this directory.
This should go in `/etc/dbus-1/system.d/`
After installation or modification, you'll need to execute `systemctl reload dbus`.

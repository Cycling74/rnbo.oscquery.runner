# RNBO OSCQuery Runner

A RNBO runner controlled by [OSCQuery](https://github.com/Vidvox/OSCQueryProposal).

## Setup

**NOTE** there is a separate [README for rpi](README-rpi.md) that indicates how
to build for rpi.

### Requirements

This currently runs on Linux and Mac, Windows is TBD.

#### Mac

* [cmake](https://cmake.org/download/) version 3.17 or greater
  * `brew install cmake`
* `g++` or `clang++`
  * `clang++` comes with XCode.
* [conan](https://conan.io/downloads.html)
* `libsndfile` `jackd`
  * `brew install jack libsndfile`
  * you likely want `qjackctl` if you're running on you laptop/desktop.
    * if you want MIDI on Mac OS, you'll want to set your *server prefix* in the *advanced* section of *qjackctl* to `jackd -Xcoremidi`
* `ruby` 2.0+ to run the compile script
  * `cmake` should be in your `PATH`.

### Building

Build the runner:

```
mkdir build/
cd build/
cmake .. && make -j8
```

### Configuration

There is an example `runner.json` config file in the config directory.
If you want some customizations you can edit that and copy it here:
`~/.config/rnbo/runner.json`

## Running

If you haven't run jack before you probably want to set it up with `qjackctl`, you can leave that running while running the runner.

Simply run the runner from the build directory `./bin/rnbooscquery`
Then start up Max. The RNBO sidebar should list your host as a `OSCQuery Runner Export`.


## Commands

Uses a modified [jsonRPC](https://www.jsonrpc.org/specification) for comand communication.

modifications:
  * `id` is a uuid.
  * method calls may have multiple responces indicating progress.


## Notes/Links

* [libossia thread safety](https://github.com/ossia/libossia/issues/656)
* [osc.js](https://github.com/colinbdclark/osc.js)

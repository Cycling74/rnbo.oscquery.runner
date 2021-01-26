# RNBO OSCQuery Runner

A RNBO runner controlled by [OSCQuery](https://github.com/Vidvox/OSCQueryProposal).

## Setup

### Requirements

This currently runs on Linux and Mac, Windows is TBD.

* [cmake](https://cmake.org/download/) version 3.17 or greater
  * this is newer than what buster provides, so for rpi you need to manually build/install.
  * on mac you can just `brew install cmake`
* `g++` or `clang++`
* [conan](https://conan.io/downloads.html)
* `libsndfile`
  * `apt-get install  libsndfile1-dev`
  * `brew install libsndfile`
* `jackd`
  * `apt-get install jackd2 libjack-jackd2-dev`
  * `brew install jack`
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

### Running

If you haven't run jack before you probably want to set it up with `qjackctl`, you can leave that running while running the runner.

Simply run the runner from the build directory `./bin/rnbooscquery`
Then start up Max. The RNBO sidebar should list your host as a `Raspberry Pi Export` even if its just your PC.


## Commands

Uses a modified [jsonRPC](https://www.jsonrpc.org/specification) for comand communication.

modifications:
  * `id` is a uuid.
  * method calls may have multiple responces indicating progress.


## Notes/Links

* [libossia thread safety](https://github.com/ossia/libossia/issues/656)
* [osc.js](https://github.com/colinbdclark/osc.js)

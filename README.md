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
* `jackd`
  * `apt-get install jackd`
  * `brew install jack`
  * you likely want `qjackctl` if you're running on you laptop/desktop.
* [libossia](https://github.com/cycling74/libossia)
  * We have a fork with some updates/fixes, those are in the `develop` branch.
  * Alex plans to make this work with conan so others don't have to configure or likely even compile this.
  * check out the develop branch and build with cmake.
    Alex built with these options:
    ```
    -DCMAKE_BUILD_TYPE=Debug -DOSSIA_CPP=ON -DOSSIA_CPP_ONLY=ON -DOSSIA_DATAFLOW=OFF -DOSSIA_EDITOR=OFF -DOSSIA_GFX=OFF -DOSSIA_PCH=OFF -DOSSIA_PROTOCOL_ARTNET=OFF -DOSSIA_PROTOCOL_AUDIO=OFF -DOSSIA_PROTOCOL_MIDI=OFF -DOSSIA_PROTOCOL_WIIMOTE=OFF -DOSSIA_PROTOCOL_JOYSTICK=OFF
    ```
* `ruby` 2.0+ to run the compile script
  * `cmake` should be in your `PATH`.

### Building

Build the runner:

```
cd runner/
mkdir build/
cd build/
cmake .. && make -j8
```

### Configuration

There is an example `runner.json` config file in this directory. Update it as needed and put it here:
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


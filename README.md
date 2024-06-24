# RNBO OSCQuery Runner

A [RNBO](https://cycling74.com/products/rnbo) runner controlled by [OSCQuery](https://github.com/Vidvox/OSCQueryProposal).

## Setup

**NOTE** there is a separate [README for rpi](README-rpi.md) that indicates how
to build for rpi.

### Build Requirements

This currently builds and runs on Linux and Mac. Windows is TBD.

`debian` instructions also apply to other `apt` based distros like `ubuntu`.

* [cmake](https://cmake.org/download/) version 3.17 or greater
  * macOS: `brew install cmake`
  * debian: `sudo apt-get install cmake`
  * make sure `cmake` is in your path if you install it with `brew` or directly from the download
* `g++` or `clang++`
  * macOS: `clang++` comes with XCode.
  * debian: `sudo apt-get install build-essential g++`
* [conan](https://conan.io/downloads.html)
  * via pip3:
    * macOS: modern macOS comes with `pip3`
    * debian: `sudo apt-get install python3-pip`
    * `pip3 install --break-system-packages --user conan==1.61.0`
  * make sure that `conan` is in your PATH, I updated my `.bashrc` to add `~/.local/bin/` to my PATH
* `ruby` 2.0+ to run the compile script
  * macOS: modern macOS comes with `ruby`
  * debian: `sudo apt-get install ruby`
* `sdbus` lib or configure with `-DWITH_DBUS=Off
  * debian: `sudo apt-get install libsdbus-c++-dev`

on debian based systems, here is a 1 liner for setting up dependencies

```
sudo apt-get -y install cmake build-essential libavahi-compat-libdnssd-dev libssl-dev libjack-jackd2-dev libdbus-1-dev libxml2-dev libgmock-dev google-mock libsdbus-c++-dev python3-pip ruby
```

on linux at least, the conan profile entry for `libcxx` is important

```
compiler.libcxx=libstdc++11
```

### Building

Build the runner with CMake. You may have to update the `RNBO_DIR` to reflect the path on your system.

If you're on Mac OS using the bundled RNBO version, your `RNBO_DIR` should probably be:

* `~/Documents/Max\ 8/Packages/RNBO/source/rnbo/` if you installed RNBO with the package manager
* `/Applications/Max.app/Contents/Resources/C74/packages/RNBO/source/rnbo` if you're using RNBO bundled with Max.

On Linux you'll likely have to copy the rnbo src dir from a Windows or Mac machine.

```
mkdir build/
cd build/
cmake .. -DRNBO_DIR=~/Documents/Max\ 8/Packages/RNBO/source/rnbo/
cmake --build .
```

If you're on a debian based system, you can also build a `.deb`

```
cpack
```

then you can install it with

```
sudo dpkg -i *.deb`
```

### Configuration

There is an example `runner.json` config file in the config directory.
If you want some customizations you can edit that and copy it here:
`~/.config/rnbo/runner.json`

Here is an example of the contents:
```json
{
    "compile_cache_dir": "~/Documents/rnbo/cache/so/",
    "save_dir": "~/Documents/rnbo/cache/saves/",
    "source_cache_dir": "~/Documents/rnbo/cache/src/",
    "datafile_dir": "~/Documents/rnbo/datafiles/",

    "instance_auto_start_last": true,
    "instance_auto_connect_audio": true,
    "instance_auto_connect_midi": true,
    "jack": {
        "period_frames": 1024,
        "sample_rate": 44100.0,
        "card_name": "hw:ES8",
        "midi_system_name": "raw"
    }
}
```

* `compile_cache_dir`: a path to the directory where compiled shared objects are stored
* `save_dir`: a path to the directory where save data is stored
* `source_cache_dir`: a path to the directory where source files are stored before compiling
* `datafile_dir`: a path to the directory where datafiles are stored, to be loaded as data refs
  * you can put files directly into this directory and load them via the OSCQuery data ref commands
* `instance_auto_start_last`: a boolean that indicates if when the runner starts, if it should attempt to load the last patcher it loaded before restart
* `instance_auto_connect_audio`: a boolean that indicates if the runner should automatically try to connect its audio i/o
  * disabling this can be useful if you want to have a custom jack signal flow, the commandline `jack_connect` can be useful if you have this set to false
* `instance_auto_connect_midi`: a boolean that indicates if the runner should automatically connect to MIDI devices that it sees
  * you can use `jack_connect` on the commandline to connect to specific MIDI devices if you have this set to false

The only file that is currently saved in the `save_dir` is called `last.json`

Here is an example of that file content:
```json
{
    "instances": [
        {
            "config": {
                "datarefs": {
                    "loop": "jongly.aif"
                },
                "inports": [
                    "foo"
                ],
                "outports": [
                    "bar"
                ],
                "presets": {
                    "muted": {
                        "baz": {
                            "value": 0.0
                        }
                    },
                    "snap1": {
                        "baz": {
                            "value": 0.9430000185966492
                        }
                    }
                }
            },
            "so_path": "/home/pi/Documents/rnbo/cache/so/libRNBORunnerSO1634332529.0.13.0-dev.44.so"
        }
    ]
}
```

The saves file only supports 1 instance at the time of this writing but eventually might support more.
If you edit this file you can change values for dataref mappings, presets and also identify which `so` to load on restart.

## Running

If you haven't run jack before you probably want to set it up with `qjackctl`, you can leave that running while running the runner.

Simply run the runner from the build directory `./bin/rnbooscquery`
Then start up Max. The RNBO sidebar should list your host as a `OSCQuery Runner Export`.

## Communicating with the runner

You can communicate with the runner via [Open Sound Control (OSC)](http://opensoundcontrol.stanford.edu/) over either websockets or UDP.

If you have sucessfully connected to a runner in Max, the associated target sidebar info should show you the UDP and HTTP/WS host port and, for OSC, transport.

By default the HTTP and websocket port are `5678` and OSC is UDP at `1234` so if you know the `ip` of your runner, you should be able to load a webpage with the url:
`http://<ipoftherunner>:5678` and send OSC messages at `osc.udp://<ipoftherunner>:1234`
If you have a hostname like `c74rpi.local` that works, you can also use that `http://c74rpi.local:5678` `osc.udp://c74rpi.local:1234`

The websocket interface is created via an [http upgrade](https://developer.mozilla.org/en-US/docs/Web/HTTP/Protocol_upgrade_mechanism) from the HTTP host and port.

*NOTE* the websocket interface is used for more than just `OSC`, so you'll want to detect the type of the websocket messages and only try to parse the Binary messages.

### OSC Namespace

If you've sent a patch to your runner, you should be able to investigate the
runner's [OSCQuery](https://github.com/Vidvox/OSCQueryProposal) namespace via
HTTP.  For instance, if my runner is at `c74rpi.local`, I might see the
below in my web browser if I load the URL `http://c74rpi.local:5678`

```json
{
  "FULL_PATH":"/",
  "CONTENTS":{
    "rnbo":{
      "FULL_PATH":"/rnbo",
      "CONTENTS":{
        "info":{
          "FULL_PATH":"/rnbo/info",
          "DESCRIPTION":"information about RNBO and the running system",
          "CONTENTS":{
            "version":{
              "FULL_PATH":"/rnbo/info/version",
              "TYPE":"s",
              "VALUE":"0.11.0-dev",
              "ACCESS":1,
              "CLIPMODE":"none"
            },
            "system_name":{
              "FULL_PATH":"/rnbo/info/system_name",
              "TYPE":"s",
              "VALUE":"Linux",
              "ACCESS":1,
              "CLIPMODE":"none"
            },
            "system_processor":{
              "FULL_PATH":"/rnbo/info/system_processor",
              "TYPE":"s",
              "VALUE":"armv7",
              "ACCESS":1,
              "CLIPMODE":"none"
            },
            "system_id":{
              "FULL_PATH":"/rnbo/info/system_id",
              "TYPE":"s",
              "VALUE":"c516613b-449f-49c7-a81b-f4de411f8d1e",
              "ACCESS":1,
              "CLIPMODE":"none",
              "DESCRIPTION":"a unique, one time generated id for this system"
            },
            "disk_bytes_available":{
              "FULL_PATH":"/rnbo/info/disk_bytes_available",
              "TYPE":"s",
              "VALUE":"11332669440",
              "ACCESS":1,
              "CLIPMODE":"none"
            },
            "update":{
              "FULL_PATH":"/rnbo/info/update",
              "DESCRIPTION":"Self upgrade/downgrade",
              "CONTENTS":{
                "state":{
                  "FULL_PATH":"/rnbo/info/update/state",
                  "TYPE":"s",
                  "VALUE":"idle",
                  "RANGE":[
                    {
                      "VALS":[
                        "idle",
                        "active",
                        "failed"
                      ]
                    }
                  ],
                  "ACCESS":1,
                  "CLIPMODE":"both",
                  "DESCRIPTION":"Update state"
                },
                "status":{
                  "FULL_PATH":"/rnbo/info/update/status",
                  "TYPE":"s",
                  "VALUE":"waiting",
                  "ACCESS":1,
                  "CLIPMODE":"none",
                  "DESCRIPTION":"Latest update status"
                },
                "supported":{
                  "FULL_PATH":"/rnbo/info/update/supported",
                  "TYPE":"T",
                  "VALUE":null,
                  "ACCESS":1,
                  "CLIPMODE":"none",
                  "DESCRIPTION":"Does this runner support remote upgrade/downgrade"
                }
              }
            }
          }
        },
        "cmd":{
          "FULL_PATH":"/rnbo/cmd",
          "TYPE":"s",
          "VALUE":"",
          "ACCESS":2,
          "CLIPMODE":"none",
          "DESCRIPTION":"command handler"
        },
        "resp":{
          "FULL_PATH":"/rnbo/resp",
          "TYPE":"s",
          "VALUE":"",
          "ACCESS":1,
          "CLIPMODE":"none",
          "DESCRIPTION":"command response"
        },
        "jack":{
          "FULL_PATH":"/rnbo/jack",
          "CONTENTS":{
            "info":{
              "FULL_PATH":"/rnbo/jack/info",
              "CONTENTS":{
                "alsa_cards":{
                  "FULL_PATH":"/rnbo/jack/info/alsa_cards",
                  "CONTENTS":{
                    "hw:ES8":{
                      "FULL_PATH":"/rnbo/jack/info/alsa_cards/hw:ES8",
                      "TYPE":"s",
                      "VALUE":"USB-Audio - ES-8\nExpert Sleepers Ltd ES-8 at usb-0000:01:00.0-1.4, high speed",
                      "ACCESS":1,
                      "CLIPMODE":"none"
                    },
                    "hw:1":{
                      "FULL_PATH":"/rnbo/jack/info/alsa_cards/hw:1",
                      "TYPE":"s",
                      "VALUE":"USB-Audio - ES-8\nExpert Sleepers Ltd ES-8 at usb-0000:01:00.0-1.4, high speed",
                      "ACCESS":1,
                      "CLIPMODE":"none"
                    }
                  }
                },
                "is_realtime":{
                  "FULL_PATH":"/rnbo/jack/info/is_realtime",
                  "TYPE":"T",
                  "VALUE":null,
                  "ACCESS":1,
                  "CLIPMODE":"none",
                  "DESCRIPTION":"indicates if jack is running in realtime mode or not"
                }
              }
            },
            "config":{
              "FULL_PATH":"/rnbo/jack/config",
              "DESCRIPTION":"Jack configuration parameters",
              "CONTENTS":{
                "card":{
                  "FULL_PATH":"/rnbo/jack/config/card",
                  "TYPE":"s",
                  "VALUE":"hw:ES8",
                  "RANGE":[
                    {
                      "VALS":[
                        "hw:ES8",
                        "hw:1"
                      ]
                    }
                  ],
                  "ACCESS":3,
                  "CLIPMODE":"both",
                  "DESCRIPTION":"ALSA device name"
                },
                "num_periods":{
                  "FULL_PATH":"/rnbo/jack/config/num_periods",
                  "TYPE":"i",
                  "VALUE":2,
                  "RANGE":[
                    {
                      "VALS":[
                        1,
                        2,
                        3,
                        4
                      ]
                    }
                  ],
                  "ACCESS":3,
                  "CLIPMODE":"both",
                  "DESCRIPTION":"Number of periods of playback latency"
                },
                "period_frames":{
                  "FULL_PATH":"/rnbo/jack/config/period_frames",
                  "TYPE":"i",
                  "VALUE":1024,
                  "RANGE":[
                    {
                      "VALS":[
                        32,
                        64,
                        128,
                        256,
                        512,
                        1024
                      ]
                    }
                  ],
                  "ACCESS":3,
                  "CLIPMODE":"both",
                  "DESCRIPTION":"Frames per period"
                },
                "sample_rate":{
                  "FULL_PATH":"/rnbo/jack/config/sample_rate",
                  "TYPE":"f",
                  "VALUE":48000.0,
                  "RANGE":[
                    {
                      "MIN":22050.0
                    }
                  ],
                  "ACCESS":3,
                  "CLIPMODE":"both",
                  "DESCRIPTION":"Sample rate"
                }
              }
            },
            "active":{
              "FULL_PATH":"/rnbo/jack/active",
              "TYPE":"T",
              "VALUE":null,
              "ACCESS":3,
              "CLIPMODE":"none"
            },
            "transport":{
              "FULL_PATH":"/rnbo/jack/transport",
              "CONTENTS":{
                "bpm":{
                  "FULL_PATH":"/rnbo/jack/transport/bpm",
                  "TYPE":"f",
                  "VALUE":100.0,
                  "ACCESS":3,
                  "CLIPMODE":"none"
                },
                "rolling":{
                  "FULL_PATH":"/rnbo/jack/transport/rolling",
                  "TYPE":"F",
                  "VALUE":null,
                  "ACCESS":3,
                  "CLIPMODE":"none"
                }
              }
            }
          }
        },
        "inst":{
          "FULL_PATH":"/rnbo/inst",
          "DESCRIPTION":"command response",
          "CONTENTS":{
            "0":{
              "FULL_PATH":"/rnbo/inst/0",
              "CONTENTS":{
                "jack":{
                  "FULL_PATH":"/rnbo/inst/0/jack",
                  "CONTENTS":{
                    "audio_ins":{
                      "FULL_PATH":"/rnbo/inst/0/jack/audio_ins",
                      "TYPE":"",
                      "VALUE":[

                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list"
                    },
                    "audio_outs":{
                      "FULL_PATH":"/rnbo/inst/0/jack/audio_outs",
                      "TYPE":"",
                      "VALUE":[

                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list"
                    },
                    "midi_ins":{
                      "FULL_PATH":"/rnbo/inst/0/jack/midi_ins",
                      "TYPE":"s",
                      "VALUE":[
                        "rnbo0:midiin1"
                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list"
                    },
                    "midi_outs":{
                      "FULL_PATH":"/rnbo/inst/0/jack/midi_outs",
                      "TYPE":"s",
                      "VALUE":[
                        "rnbo0:midiout1"
                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list"
                    }
                  }
                },
                "params":{
                  "FULL_PATH":"/rnbo/inst/0/params",
                  "DESCRIPTION":"Parameter get/set",
                  "CONTENTS":{
                    "foo":{
                      "FULL_PATH":"/rnbo/inst/0/params/foo",
                      "TYPE":"s",
                      "VALUE":"x",
                      "RANGE":[
                        {
                          "VALS":[
                            "x",
                            "y",
                            "z"
                          ]
                        }
                      ],
                      "ACCESS":3,
                      "CLIPMODE":"both",
                      "CONTENTS":{
                        "normalized":{
                          "FULL_PATH":"/rnbo/inst/0/params/foo/normalized",
                          "TYPE":"f",
                          "VALUE":0.20000000298023225,
                          "RANGE":[
                            {
                              "MIN":0.0,
                              "MAX":1.0
                            }
                          ],
                          "ACCESS":3,
                          "CLIPMODE":"both"
                        }
                      }
                    },
                    "bar":{
                      "FULL_PATH":"/rnbo/inst/0/params/bar",
                      "TYPE":"f",
                      "VALUE":0.0,
                      "RANGE":[
                        {
                          "MIN":0.0,
                          "MAX":100.0
                        }
                      ],
                      "ACCESS":3,
                      "CLIPMODE":"both",
                      "CONTENTS":{
                        "normalized":{
                          "FULL_PATH":"/rnbo/inst/0/params/bar/normalized",
                          "TYPE":"f",
                          "VALUE":0.0,
                          "RANGE":[
                            {
                              "MIN":0.0,
                              "MAX":1.0
                            }
                          ],
                          "ACCESS":3,
                          "CLIPMODE":"both"
                        }
                      }
                    }
                  }
                },
                "data_refs":{
                  "FULL_PATH":"/rnbo/inst/0/data_refs"
                },
                "presets":{
                  "FULL_PATH":"/rnbo/inst/0/presets",
                  "CONTENTS":{
                    "entries":{
                      "FULL_PATH":"/rnbo/inst/0/presets/entries",
                      "TYPE":"s",
                      "VALUE":[
                        "untitled 1"
                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list",
                      "DESCRIPTION":"A list of presets that can be loaded"
                    },
                    "save":{
                      "FULL_PATH":"/rnbo/inst/0/presets/save",
                      "TYPE":"s",
                      "VALUE":"",
                      "ACCESS":2,
                      "CLIPMODE":"none",
                      "DESCRIPTION":"Save the current settings as a preset with the given name"
                    },
                    "load":{
                      "FULL_PATH":"/rnbo/inst/0/presets/load",
                      "TYPE":"s",
                      "VALUE":"",
                      "ACCESS":2,
                      "CLIPMODE":"none",
                      "DESCRIPTION":"Load a preset with the given name"
                    },
                    "initial":{
                      "FULL_PATH":"/rnbo/inst/0/presets/initial",
                      "TYPE":"s",
                      "VALUE":"",
                      "ACCESS":3,
                      "CLIPMODE":"none",
                      "DESCRIPTION":"Indicate a preset, by name, that should be loaded every time this patch is reloaded. Set to an empty string to load the loaded preset instead"
                    }
                  }
                },
                "midi":{
                  "FULL_PATH":"/rnbo/inst/0/midi",
                  "CONTENTS":{
                    "in":{
                      "FULL_PATH":"/rnbo/inst/0/midi/in",
                      "TYPE":"",
                      "VALUE":[

                      ],
                      "ACCESS":2,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list",
                      "DESCRIPTION":"midi events in to your RNBO patch"
                    },
                    "out":{
                      "FULL_PATH":"/rnbo/inst/0/midi/out",
                      "TYPE":"",
                      "VALUE":[

                      ],
                      "ACCESS":1,
                      "CLIPMODE":"none",
                      "EXTENDED_TYPE":"list",
                      "DESCRIPTION":"midi events out of your RNBO patch"
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
```

Those `FULL_PATH` entires correspond to `OSC` addresses, and the `TYPE`
identifies the `OSC` type that those parameters expect, if any.

If the `ACCESS` value is `2` (set only) or `3` (get set) then you can send OSC
messages to that address to alter parameters.

Most of what you'll want to interact with will be below the `/rnbo/inst/0`
path, this is the path that identifies the running codegen export.

See the [OSCQueryProposal](https://github.com/Vidvox/OSCQueryProposal) for more details on OSCQuery.

### Basic Javascript Websocket Example

```javascript
const OSC = require("osc");

{
	let ws = new WebSocket(YOUR_RUNNER_URL);

	ws.on('message', (d) => {
		//must be a buffer because there are other non OSC websocket messages as well
		if (Buffer.isBuffer(d)) {
			try {
				const msg = OSC.readPacket(d, {metadata: true});
				//process
			} catch (e) {
			}
		}
	});

	ws.on('open', () => {
		//send OSC
		const array = OSC.writePacket({
			address: "/rnbo/inst/0/params/foo",
			args: [
				{
					type: "f",
					value: 1.0
				}
			]
		},
		{ metadata: true });
		ws.send(array);
	});
}
```

### Commandline OSC Example

Here we use `oscsend`, which is available in homebrew, to send a normalized
parameter update to `c74rpi.local`.

```shell
oscsend osc.udp://c74rpi.local:1234 /rnbo/inst/0/params/foo/normalized f 0.2
```

If `foo` is a valid parameter in your loaded patch, and you send that, then
load `http://c74rpi.local:5678/rnbo/inst/0/params/` in a webbrowser, you should
see that both `foo` and `foo/normalized` have been updated.


## Commands

Uses a modified [jsonRPC](https://www.jsonrpc.org/specification) for comand communication.

modifications:
  * `id` is a uuid.
  * method calls may have multiple responces indicating progress.


## Notes/Links

* [rnbo](https://cycling74.com/products/rnbo)
* [libossia](https://ossia.io/site-libossia/about.html)
  * [thread safety](https://github.com/ossia/libossia/issues/656)
* [osc.js](https://github.com/colinbdclark/osc.js)

### To add an OSC listener

If the runner is running on the same machine as you want to listen on, you can use `localhost` for the `ip`.

```
oscsend osc.udp://localhost:1234 /rnbo/cmd s '{"method": "listener_add", "id": "foo", "params": {"ip": "localhost", "port": 9999}}'
```

### Metadata

If you have your own uses for the `meta` entry, you can add anything you'd like but it has to be a `JSON` key-value map at the top level.

The runner supports the following `meta` entries directly.

#### OSC

Inports, Outports, and Parameters can take metadata that extend their mapping to/from OSC messages.

*Format*

The simplest of forms, `{"osc": "/foo/bar"}` maps the item to/from the OSC message `/foo/bar`.
You can also use a more verbose format `{"osc": {"addr": "/your/addr", "out": true}}`

*Direction*

* Inports can only listen to OSC messages
* Outports can only send OSC messages
* Parameters only listen by default but can be made to send with a more verbose OSC meta entry: `{"osc":{"addr": "/foo/bar/", "out": true}}`
  * **NOTE**: a parameter with the above meta will only send on `/foo/bar`, it will not also listen.
  If you want to do both you need to add, `"in": true` eg `{"osc":{"addr": "/foo/bar/", "out": true, "in": true}}`

*Normalized*

By default parameter OSC values map to/from unnormalized values but if you add `"norm": true` to your meta you map to/from normalized values.

*Misc notes*

* Inport and Outports default to mapping to/from OSC addresses if you prefix their name with a `/`, for instance `[inport /synth/freq]`
  * You can toggle this behavior with the `Instance: Port To OSC` setting in the [Web Interface](https://rnbo.cycling74.com/learn/raspberry-pi-web-interface-guide) settings.

#### MIDI

Parameters support MIDI mapping via a `midi` entry in their metadata. The [Web Interface](https://rnbo.cycling74.com/learn/raspberry-pi-web-interface-guide)
now helps automate setting this value but you can set it explicitly if you prefer.

*Misc notes*

* As of this writing the MIDI value is scaled to `0..1` and applied, without any additional augmentation, to the normalized value for a parameter.
* Notes simply map to `0` for note off and `1` for note on indendent of velocity.
* When you map a MIDI message, it is filtered out and not sent along your patcher beyond setting the parameter value it is associated with.
* You can only map 1 parameter per source, so for instance, ctrl 12 on channel 2 can only control a single parameter.
* The `chan` entry is `1` based, so valid values are `1-16`.
* The `chan` entry is optional and defaults to `1` if it isn't present.

*midi JSON format*

* multiple per channel mappings
  * note: `{"note": 2, "chan": 10}`
  * controller change: `{"ctrl": 5, "chan": 10}`
  * key pressure: `{"keypress": 1, "chan": 1}`
* one per channel mappings
  * pitch bend: `{"bend": 1}`
  * program change: `{"prgchg": 10}`
  * channel pressure: `{"chanpress": 1}`

As an example, a parameter might have meta with: `{"midi": {"ctrl": 4, "chan": 16}}`.
This would map controller change 4 on channel 16's value, scaled to 0..1 to the parameter's normalized value.

### Testing out discovery

`dns-sd` can show you available services:

```shell
dns-sd -B _oscjson._tcp
```

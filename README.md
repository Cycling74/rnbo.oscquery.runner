# RNBO OSCQuery Runner


## Requirements

* `cmake` version 3.17 or greater
* `jackd`
* `ruby` 2.0+ to run the compile script
  * the `rnbo-compile-so` script should be in your path
  * the `CMakeLists.txt` file from `./so/` either be in the directory pointed to by the build variable `RNBO_SO_BUILD_DIR` or in a directory pointed to by the config file `${RNBO_CONFIG_DIR}/runner.json` with key `so_build_dir`.

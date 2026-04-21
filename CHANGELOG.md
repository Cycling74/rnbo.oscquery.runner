# RNBO Runner Changes

* *current*
    * relaxed rnbo version requirements
        * `1.4.3` and later will just have to match major.minor, calling this the compatibility version
            * for `versions >= 1.4.3 and < 1.5.0` that compatibility version is `1.4.3`
            * for `versions >= 1.5.0` the compatibility version is `major.minor.0`
        * reduces needs for migrations
        * using this for package installation version matching
        * added OSC endpoints for patcher versions
            * the rnbo version the patcher was built with: `/rnbo/patchers/<PATCHER NAME>/rnbo_version`
            * the rnbo compatibility version for the patcher: `/rnbo/patchers/<PATCHER NAME>/rnbo_compatibility_version`
        * added OSC endpoints for the current compatibility version: `/rnbo/info/rnbo_compatibility_version`
        * `/rnbo/info/update/migration_available` is now based on compatibility version
    * added optional device midi input filtering
        * OSCQuery endpoint: `/rnbo/inst/<INSTANCE ID>/config/midi_input_channel`
            * set to "all" (default) for no filtering
            * set to "1" to only accept messages on channel 1
            * any realtime or non channel based messages should always get thru
    * fixed bug where `set_preset` config for instance wasn't persisting with graph
    * expanded ability to read from / write to various runner OSC endpoints with patcher exports via inport, outport, param
    * added the concept of `Param Views`
        * these are sorted collections of parameters that can span multiple devices
        * they are associated with graphs
    * added package creation and installation commands
        * these can create `.rnbopack` files (which are just tar files) that can be installed on other people's devices
        * packages can be `all` of your content, a specific `set` (aka graph) or a `patcher`
            * there are options
                * `include_presets`: boolean (default true)
                * `include_datafiles`: boolean (default true)
                * `include_views`: boolean (default true) - param views
                * `include_binaries`: boolean (default true) - param views
                * `rnbo_version`: string (defaults to the version running on the device) - optional get data from earlier versions
    * Added `uuid` for patchers and sets (aka graphs)
        * initial `uuid` is deterministic based on `name`, `created_at` and the name of the table that they're in
        * new saves of patchers and graphs get new random `uuid`
        * package exports include `uuid`
        * package installation should maintain `uuid` values
            * package install now skips installing when existing content with matching `uuid` exists
            * package install now has 2 optional params `skip_patchers` and `skip_sets`
                * these are both lists of strings tell the installer to not install items from the package with those names
        * an open question is, should saving presets, param views, etc change the `uuid` of a graph or patcher?
    * package installation command also has new `force` boolean parameter that will ignore any collisions and force installation
    * added `VACUUM` to db on startup
    * updated dataref loading, can now load from subdirectories
    * added optional configuration to put recordings in a separate directory, `recording_dir`
    * transport
        * you can now alter transport parameters (tempo, start stop state, location) even while not syncing
        * with the latest `jack_transport_link` you should now get link peer count and a new option to toggle syncing transport to link or not
    * param OSCQuery
        * added unit name, may be empty, re #12
    * add commandline option to not load inital graph on startup: `--delay-start`
        * new OSCQuery endpoint: `/rnbo/inst/control/sets/load/initial`
            * allows for delayed start, triggered with OSC
    * added indexes to presets and graph presets
        * there are a variety of new preset endpoints related to the indexes
    * added new instance configuration to opt in/out of graph presets: `/rnbo/inst/<INSTANCE ID>/config/set_preset`
        * this way you can have some devices in your graph that don't change when you load graph presets

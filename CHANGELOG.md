# RNBO Runner Changes

* *current*
    * Added `uuid` for patchers and sets (aka graphs)
        * initial `uuid` is deterministic based on `name`, `created_at` and the name of the table that they're in
        * new saves of patchers and sets get new random `uuid`
        * package exports include `uuid`
        * package installation should maintain `uuid` values
            * package install now skips installing when existing content with matching `uuid` exists
            * package install now has 2 optional params `skip_patchers` and `skip_sets`
                * these are both lists of strings tell the installer to not install items from the package with those names
        * an open question is, should saving presets, set views, etc change the `uuid` of a set or patcher?
    * package installation command also has new `force` boolean parameter that will ignore any collisions and force installation
    * added `VACUUM` to db on startup

# Auto-update: on the 4 MB Super Mini the public stubs in updater.c replace
# lib/libupdater_impl.a. There is no inactive OTA slot, so pull-OTA cannot run.
idf_component_register(
    SRCS "updater.c"
    INCLUDE_DIRS "include"
)

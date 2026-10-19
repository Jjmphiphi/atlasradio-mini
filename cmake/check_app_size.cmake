# App .bin must fit the 4 MB factory partition (0x340000 = 3,407,872 bytes).
# Leave ~100 KB for image headers / padding, matching the old OTA-slot margin.
if(NOT BIN OR NOT MAX)
    message(FATAL_ERROR "check_app_size.cmake expects -DBIN= and -DMAX=")
endif()
if(NOT EXISTS "${BIN}")
    message(FATAL_ERROR "Firmware binary not found: ${BIN}")
endif()
file(SIZE "${BIN}" sz)
message(STATUS "AtlasRadio Mini app size: ${sz} / ${MAX} bytes")
if(sz GREATER MAX)
    message(FATAL_ERROR "App is ${sz} bytes — exceeds the 4 MB factory partition budget (${MAX}). Drop more UI/fonts or disable features.")
endif()

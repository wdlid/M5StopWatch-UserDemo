if(DEFINED ENV{BUILD_RECOVERY} OR BUILD_RECOVERY)
    message(STATUS ">>> Building RECOVERY target <<<")
    idf_component_register(
        SRCS 
            "recovery/recovery_main.cpp"
        INCLUDE_DIRS 
            "."
        REQUIRES
            esp_partition
            esp_wifi
            esp_http_server
            esp_netif
            fatfs
            wear_levelling
            nvs_flash
            M5GFX
            M5PM1
            M5IOE1
            driver
    )
else()
    file(GLOB_RECURSE MY_SRCS
        "apps/*.c"
        "apps/*.cc"
        "apps/*.cpp"
        "assets/*.c"
        "assets/*.cc"
        "assets/*.cpp"
        "hal/*.c"
        "hal/*.cc"
        "hal/*.cpp"
    )

    set(MY_INCS
        "."
    )

    idf_component_register(
        SRCS 
            "main.cpp"
            ${MY_SRCS}
        INCLUDE_DIRS 
            ${MY_INCS}
        EMBED_FILES
            "assets/sfx/boot_sfx.bin"
        EMBED_TXTFILES
            "hal/utils/config_ap/assets/badge_config_ap.html"
    )
endif()

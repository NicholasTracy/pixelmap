idf_component_register(
    SRCS "src/map_store.c"
    INCLUDE_DIRS "include"
    REQUIRES mapping log
    PRIV_REQUIRES spiffs
)

# Ensure SPIFFS public headers are visible (some IDF scans omit them from REQUIRES).
idf_build_get_property(idf_path IDF_PATH)
target_include_directories(${COMPONENT_LIB} PRIVATE "${idf_path}/components/spiffs/include")

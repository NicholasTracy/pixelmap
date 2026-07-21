# Lua 5.4 core (sandboxed: no io/os/package/debug)
idf_component_register(
    SRCS
        "src/lapi.c"
        "src/lauxlib.c"
        "src/lbaselib.c"
        "src/lcode.c"
        "src/lctype.c"
        "src/ldebug.c"
        "src/ldo.c"
        "src/ldump.c"
        "src/lfunc.c"
        "src/lgc.c"
        "src/llex.c"
        "src/lmathlib.c"
        "src/lmem.c"
        "src/lobject.c"
        "src/lopcodes.c"
        "src/lparser.c"
        "src/lstate.c"
        "src/lstring.c"
        "src/lstrlib.c"
        "src/ltable.c"
        "src/ltablib.c"
        "src/ltm.c"
        "src/lundump.c"
        "src/lutf8lib.c"
        "src/lvm.c"
        "src/lzio.c"
    INCLUDE_DIRS "include" "src"
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC LUA_USE_C89)
# Silence pedantic warnings from upstream Lua on GCC/Clang
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-parameter)

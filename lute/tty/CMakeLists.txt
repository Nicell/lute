add_library(Lute.TTY STATIC)

target_sources(Lute.TTY PRIVATE
    include/lute/tty.h

    src/tty.cpp
)

target_compile_features(Lute.TTY PUBLIC cxx_std_17)
target_include_directories(Lute.TTY PUBLIC "include" ${LIBUV_INCLUDE_DIR})
target_link_libraries(Lute.TTY PRIVATE Lute.Runtime Luau.VM uv_a)
target_compile_options(Lute.TTY PRIVATE ${LUTE_OPTIONS})

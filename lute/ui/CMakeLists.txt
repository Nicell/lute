add_library(Lute.UI STATIC)

target_sources(Lute.UI PRIVATE
    include/lute/ui.h
    include/lute/ui/Accessibility.h
    include/lute/ui/Context.h
    include/lute/ui/Core.h
    include/lute/ui/Input.h
    include/lute/ui/Layout.h
    include/lute/ui/Node.h
    include/lute/ui/Platform.h
    include/lute/ui/Profile.h
    include/lute/ui/Render.h
    include/lute/ui/Scene.h
    include/lute/ui/Signal.h
    include/lute/ui/Style.h
    include/lute/ui/Text.h

    src/ui/accessibility/Semantics.cpp
    src/ui/core/Dirty.cpp
    src/ui/core/NodeTree.cpp
    src/ui/core/Profile.cpp
    src/ui/core/UiContext.cpp
    src/ui/Input.cpp
    src/ui/layout/Layout.cpp
    src/ui/luau/LuauUiModule.cpp
    src/ui/platform/NativeShell.cpp
    src/ui/reactive/Signal.cpp
    src/ui/render/dawn/DawnDevice.cpp
    src/ui/scene/Scene.cpp
    src/ui/text/Shaper.cpp
)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(Lute.UI PRIVATE
        src/ui/platform/macos/NativeShell.mm
    )
    set_source_files_properties(src/ui/platform/macos/NativeShell.mm PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(Lute.UI PUBLIC "-framework AppKit" "-framework QuartzCore" "-framework CoreText" "-framework CoreGraphics" "-framework ImageIO")
endif()

target_compile_features(Lute.UI PUBLIC cxx_std_17 PRIVATE cxx_std_20)
target_include_directories(Lute.UI PUBLIC "include")
target_link_libraries(Lute.UI PRIVATE Lute.Common Lute.Runtime Luau.VM)

if(TARGET harfbuzz)
    target_compile_definitions(Lute.UI PRIVATE LUTE_UI_USE_HARFBUZZ=1)
    target_link_libraries(Lute.UI PUBLIC harfbuzz)
endif()

if(TARGET harfbuzz-gpu)
    target_compile_definitions(Lute.UI PRIVATE LUTE_UI_USE_HARFBUZZ_GPU=1)
    target_link_libraries(Lute.UI PUBLIC harfbuzz-gpu)
endif()

if(TARGET dawn::webgpu_dawn)
    target_compile_definitions(Lute.UI PRIVATE LUTE_UI_USE_DAWN=1)
    target_link_libraries(Lute.UI PUBLIC dawn::webgpu_dawn)
endif()

target_compile_options(Lute.UI PRIVATE ${LUTE_OPTIONS})

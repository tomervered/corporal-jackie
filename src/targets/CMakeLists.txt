find_package(Qt6 REQUIRED COMPONENTS Network OpenGL Quick)

add_library(cj_targets STATIC
    MpvTarget.cpp    MpvTarget.h
    ScopedHttp.cpp   ScopedHttp.h
    MpvVideoItem.cpp MpvVideoItem.h
    SleepBlocker.cpp SleepBlocker.h
)
# AMD FSR 1, ported to an mpv user shader (MIT; see the file's header).
qt_add_resources(cj_targets "cj_shaders" PREFIX "/cj" FILES shaders/fsr1.glsl)
target_include_directories(cj_targets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cj_targets
    PUBLIC  cj_common cj_session Qt6::Core Qt6::Network Qt6::Quick Qt6::OpenGL
    PRIVATE cj::mpv)

if(WIN32)
    target_link_libraries(cj_targets PRIVATE dxgi)  # finds the graphics card for VSR
endif()

if(APPLE)
    target_link_libraries(cj_targets PRIVATE "-framework IOKit" "-framework CoreFoundation")
    # Apple MetalFX upscaling. MetalFX is weak-linked: on macOS 12 the
    # option is simply not offered.
    target_sources(cj_targets PRIVATE MetalFxUpscaler.mm MetalFxUpscaler.h)
    set_source_files_properties(MetalFxUpscaler.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(cj_targets PRIVATE
        "-framework Metal" "-framework IOSurface" "-framework OpenGL" "-weak_framework MetalFX")
endif()

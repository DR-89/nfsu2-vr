# SDL is built for the target ABI by android/runtime/CMakeLists.txt.
# Do not accidentally locate an installed Windows/Linux SDL library.
if(NOT TARGET SDL2::SDL2)
    message(FATAL_ERROR "Add the Android SDL2 source target before xboxrecomp")
endif()
set(SDL2_FOUND TRUE)

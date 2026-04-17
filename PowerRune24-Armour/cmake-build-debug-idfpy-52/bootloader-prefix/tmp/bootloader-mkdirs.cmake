# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/env/Espressif/frameworks/esp-idf-v5.2.6/components/bootloader/subproject")
  file(MAKE_DIRECTORY "C:/env/Espressif/frameworks/esp-idf-v5.2.6/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "D:/Program/MekCraft-Labs/forks/powerrune24/PowerRune24-Armour/cmake-build-debug-idfpy-52/bootloader"
  "D:/Program/MekCraft-Labs/forks/powerrune24/PowerRune24-Armour/cmake-build-debug-idfpy-52/bootloader-prefix"
  "D:/Program/MekCraft-Labs/forks/powerrune24/PowerRune24-Armour/cmake-build-debug-idfpy-52/bootloader-prefix/tmp"
  "D:/Program/MekCraft-Labs/forks/powerrune24/PowerRune24-Armour/cmake-build-debug-idfpy-52/bootloader-prefix/src/bootloader-stamp"
  "D:/Program/MekCraft-Labs/forks/powerrune24/PowerRune24-Armour/cmake-build-debug-idfpy-52/bootloader-prefix/src"
  "D:/Program/MekCraft-Labs/forks/powerrune24/PowerRune24-Armour/cmake-build-debug-idfpy-52/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Program/MekCraft-Labs/forks/powerrune24/PowerRune24-Armour/cmake-build-debug-idfpy-52/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Program/MekCraft-Labs/forks/powerrune24/PowerRune24-Armour/cmake-build-debug-idfpy-52/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()

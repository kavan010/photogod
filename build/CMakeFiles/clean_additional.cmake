# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles/photogod_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/photogod_autogen.dir/ParseCache.txt"
  "photogod_autogen"
  )
endif()

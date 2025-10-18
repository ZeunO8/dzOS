# append_file.cmake
# Usage:
#   cmake -DOUT="<path>" -DNAME="<name>" -DFS_PATH="<dzfs_path>" -P append_file.cmake
if(NOT DEFINED OUT)
  message(FATAL_ERROR "append_file.cmake: OUT variable not defined")
endif()

if(NOT DEFINED NAME)
  message(FATAL_ERROR "append_file.cmake: NAME variable not defined")
endif()

if(NOT DEFINED FS_PATH)
  message(FATAL_ERROR "append_file.cmake: FS_PATH variable not defined")
endif()

set(APP "const char* fs_path_${NAME} = \"${FS_PATH}\"")
string(APPEND APP ";")
file(APPEND "${OUT}" "${APP}")
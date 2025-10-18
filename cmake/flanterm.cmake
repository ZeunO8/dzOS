FetchContent_Declare(flanterm
    GIT_REPOSITORY https://codeberg.org/Mintsuki/Flanterm.git
    GIT_TAG trunk
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(flanterm)
if(NOT flanterm_POPULATED)
  FetchContent_Populate(flanterm)
endif()
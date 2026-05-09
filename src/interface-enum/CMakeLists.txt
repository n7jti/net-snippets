add_executable(interface-enum main.c)

target_compile_definitions(interface-enum PRIVATE
  WIN32_LEAN_AND_MEAN
  UNICODE
  _UNICODE
  _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(interface-enum PRIVATE iphlpapi ws2_32 shell32)

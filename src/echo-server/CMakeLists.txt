add_executable(echo-server main.cpp)

target_compile_definitions(echo-server PRIVATE
  WIN32_LEAN_AND_MEAN
  UNICODE
  _UNICODE
  _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(echo-server PRIVATE ws2_32 shell32)

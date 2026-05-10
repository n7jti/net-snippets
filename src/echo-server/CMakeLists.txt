find_program(NUGET_EXE nuget REQUIRED)

set(_wil_nuget_root "${CMAKE_BINARY_DIR}/_deps/wil")
set(_wil_nuget_header "")

if(NOT EXISTS "${_wil_nuget_header}")
  file(MAKE_DIRECTORY "${_wil_nuget_root}")
  execute_process(
    COMMAND "${NUGET_EXE}" install Microsoft.Windows.ImplementationLibrary
            -OutputDirectory "${_wil_nuget_root}"
            -Source https://api.nuget.org/v3/index.json
            -NonInteractive
    RESULT_VARIABLE _wil_nuget_result
    OUTPUT_QUIET
    ERROR_QUIET
  )
endif()

file(GLOB _wil_nuget_package_dirs LIST_DIRECTORIES TRUE "${_wil_nuget_root}/Microsoft.Windows.ImplementationLibrary.*")
if(_wil_nuget_package_dirs)
  list(SORT _wil_nuget_package_dirs)
  list(GET _wil_nuget_package_dirs -1 _wil_nuget_package_dir)
  set(_wil_nuget_include_dir "${_wil_nuget_package_dir}/include")
  set(_wil_nuget_header "${_wil_nuget_include_dir}/wil/result.h")
endif()

if(NOT EXISTS "${_wil_nuget_header}")
  message(FATAL_ERROR "Failed to install the latest Microsoft.Windows.ImplementationLibrary via NuGet.")
endif()

add_library(WIL::WIL INTERFACE IMPORTED)
set_target_properties(WIL::WIL PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${_wil_nuget_include_dir}"
)

add_executable(echo-server main.cpp)

target_compile_definitions(echo-server PRIVATE
  WIN32_LEAN_AND_MEAN
  UNICODE
  _UNICODE
  _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(echo-server PRIVATE WIL::WIL ws2_32 shell32)

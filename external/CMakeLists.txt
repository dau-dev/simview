
if(NOT SIMVIEW_USE_HOST_ABSL OR (SIMVIEW_BUILD_TESTS AND NOT SIMVIEW_USE_HOST_GTEST))
  find_package(Git QUIET)
  if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
      message(STATUS "Submodule update")
      execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                      RESULT_VARIABLE GIT_SUBMOD_RESULT)
      if(NOT GIT_SUBMOD_RESULT EQUAL "0")
        message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
      endif()
    endif()
  endif()
endif()

if(NOT SIMVIEW_USE_HOST_ABSL)
  # Abseil can trigger this warning at current version.
  set(CMAKE_CXX_FLAGS  "-Wno-maybe-uninitialized")

  # Add all expected submodules.
  set(ABSL_PROPAGATE_CXX_STD ON)
  add_subdirectory(abseil-cpp EXCLUDE_FROM_ALL)
else()
  find_package(absl CONFIG REQUIRED)
  set_target_properties(absl::flat_hash_map PROPERTIES IMPORTED_GLOBAL True)
  set_target_properties(absl::str_format PROPERTIES IMPORTED_GLOBAL True)
  set_target_properties(absl::time PROPERTIES IMPORTED_GLOBAL True)
endif()

if(SIMVIEW_BUILD_TESTS AND NOT SIMVIEW_USE_HOST_GTEST)
  add_subdirectory(googletest EXCLUDE_FROM_ALL)
elseif(SIMVIEW_BUILD_TESTS)
  find_package(GTest CONFIG)
  set_target_properties(GTest::gtest_main PROPERTIES IMPORTED_GLOBAL True)
endif()

add_subdirectory(fst)

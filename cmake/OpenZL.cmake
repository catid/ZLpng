include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Keep the codec and its graph format reproducible. Dependencies live only in
# the build tree; a checkout is never required to contain vendored archives.
set(OPENZL_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(OPENZL_BUILD_CPP OFF CACHE BOOL "" FORCE)
set(OPENZL_BUILD_CUSTOM_PARSERS OFF CACHE BOOL "" FORCE)
set(OPENZL_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENZL_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(OPENZL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OPENZL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OPENZL_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(OPENZL_ALLOW_INTROSPECTION OFF CACHE BOOL "" FORCE)
set(OPENZL_INSTALL OFF CACHE BOOL "" FORCE)
set(OPENZL_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(openzl
    URL https://codeload.github.com/facebook/openzl/tar.gz/32246b48faee46807f84183dac4db479089f5445
    URL_HASH SHA256=0d40606949d34c168ea3ac54035f689c03e3b64d2b2358fa52cb990d92c217aa)
FetchContent_MakeAvailable(openzl)

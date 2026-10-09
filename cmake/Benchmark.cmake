# Download the unchanged original codec only when benchmarks are requested.
FetchContent_Declare(zpng_reference_source
    URL https://codeload.github.com/catid/Zpng/tar.gz/ead281baeaf83bfc45fc5eac61bd0e41a57b007a
    URL_HASH SHA256=6001b1492dd38343a8c73d38e6ba6cbe823687d3b4970bd989d7806cf8caa01c
    SOURCE_SUBDIR skip_upstream_build)
FetchContent_MakeAvailable(zpng_reference_source)
file(GLOB reference_zstd_sources "${zpng_reference_source_SOURCE_DIR}/zstd/*.c")
add_library(zpng_reference SHARED
    "${zpng_reference_source_SOURCE_DIR}/zpng.cpp" ${reference_zstd_sources})
target_link_libraries(zpng_reference PRIVATE Threads::Threads)
# The two codecs embed different Zstd versions; prevent symbol interposition.
target_link_options(zpng_reference PRIVATE -Wl,-Bsymbolic
    -Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/reference.map)

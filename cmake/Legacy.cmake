# Optional original-codec and experiment targets.
# Zstd library source files
set(ZSTD_LIB_SRCFILES
        zstd/bitstream.h
        zstd/compiler.h
        zstd/cover.c
        zstd/cpu.h
        zstd/divsufsort.c
        zstd/divsufsort.h
        zstd/entropy_common.c
        zstd/error_private.c
        zstd/error_private.h
        zstd/fse.h
        zstd/fse_compress.c
        zstd/fse_decompress.c
        zstd/huf.h
        zstd/huf_compress.c
        zstd/huf_decompress.c
        zstd/mem.h
        zstd/pool.c
        zstd/pool.h
        zstd/threading.c
        zstd/threading.h
        zstd/xxhash.c
        zstd/xxhash.h
        zstd/zdict.c
        zstd/zdict.h
        zstd/zstd.h
        zstd/zstdmt_compress.c
        zstd/zstdmt_compress.h
        zstd/zstd_common.c
        zstd/zstd_compress.c
        zstd/zstd_compress_internal.h
        zstd/zstd_decompress.c
        zstd/zstd_double_fast.c
        zstd/zstd_double_fast.h
        zstd/zstd_errors.h
        zstd/zstd_fast.c
        zstd/zstd_fast.h
        zstd/zstd_internal.h
        zstd/zstd_lazy.c
        zstd/zstd_lazy.h
        zstd/zstd_ldm.c
        zstd/zstd_ldm.h
        zstd/zstd_opt.c
        zstd/zstd_opt.h
)

# Zpng library source files
set(ZPNG_LIB_SRCFILES
        zpng.cpp
        zpng.h
)

# Zpng unit tester
set(ZPNG_TEST_SRCFILES
        apps/zpng_test.cpp
)

# Zpng app
set(ZPNG_APP_SRCFILES
        apps/zpng_app.cpp
)

if(ZLPNG_BUILD_LEGACY OR ZPNG_BUILD_EXPERIMENTS)
add_library(zpnglib ${ZPNG_LIB_SRCFILES} ${ZSTD_LIB_SRCFILES})

add_executable(unit_test ${ZPNG_TEST_SRCFILES})
target_link_libraries(unit_test zpnglib Threads::Threads)

add_executable(zpng ${ZPNG_APP_SRCFILES})
target_link_libraries(zpng zpnglib Threads::Threads)

endif()
if(ZPNG_BUILD_EXPERIMENTS)
    add_executable(filter_bench experiments/filter_bench.cpp)
    target_link_libraries(filter_bench zpnglib Threads::Threads)
    add_executable(filter_bench_fast experiments/filter_bench.cpp)
    target_compile_definitions(filter_bench_fast PRIVATE ZPNG_FUSED8=1)
    target_link_libraries(filter_bench_fast zpnglib Threads::Threads)
endif()

if(ZPNG_BUILD_EXPERIMENTS)
    add_executable(round2_bench experiments/round2/bench.cpp)
    target_compile_definitions(round2_bench PRIVATE ZPNG_FUSED8=1)
    target_link_libraries(round2_bench zpnglib Threads::Threads)
endif()

if(ZPNG_BUILD_EXPERIMENTS)
    add_executable(round3_bench experiments/round3/bench.cpp)
    target_compile_definitions(round3_bench PRIVATE ZPNG_FUSED8=1)
    target_link_libraries(round3_bench zpnglib Threads::Threads)
endif()

if(ZPNG_BUILD_EXPERIMENTS)
    add_executable(round3_kernel_probe experiments/round3/kernel_probe.cpp)
    target_compile_definitions(round3_kernel_probe PRIVATE ZPNG_FUSED8=1)
    target_link_libraries(round3_kernel_probe zpnglib Threads::Threads)
endif()

if(ZPNG_BUILD_EXPERIMENTS)
    add_library(zpng_effortlib experiments/round4/effort.cpp)
    target_compile_definitions(zpng_effortlib PRIVATE ZPNG_FUSED8=1)
    target_link_libraries(zpng_effortlib zpnglib Threads::Threads)
    add_executable(zpng_effort experiments/round4/effort_cli.cpp)
    target_link_libraries(zpng_effort zpng_effortlib)
    add_executable(round4_effort_bench experiments/round4/effort_bench.cpp)
    target_link_libraries(round4_effort_bench zpng_effortlib)
    add_executable(round4_effort_test experiments/round4/test_effort.cpp)
    target_compile_definitions(round4_effort_test PRIVATE ZPNG_FUSED8=1)
    target_link_libraries(round4_effort_test zpnglib Threads::Threads)
    add_executable(round5_legacy_test experiments/round5/test_legacy.cpp)
    target_link_libraries(round5_legacy_test zpnglib Threads::Threads)
endif()

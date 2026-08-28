# This file is included by DuckDB's build system. It specifies which extension to load
#
# This branch targets DuckDB v1.5.5. The duckdb submodule pins the hlinander
# ThreadCPUProfiler tree, the same engine duckvis and SwanLake bundle — an
# extension built against any other engine shifts OperatorProfiler and faults
# the host on its first profiler-map lookup.

# Extension from this repo
duckdb_extension_load(airport
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)

# Left off: CMake FetchContent-clones this at configure time, and the nix build
# has no network. nho builds httpfs separately against the same engine
# (modules/duckdb-extensions.nix). Tag kept for reference — it is the commit
# DuckDB v1.5.5 pins, matching the duckdb submodule.
#duckdb_extension_load(httpfs
#    LOAD_TESTS
#    DONT_LINK
#    GIT_URL https://github.com/duckdb/duckdb-httpfs
#    GIT_TAG 827222fb45a043a7a852d1f7aae46901492a3cda
#    INCLUDE_DIR extension/httpfs/include
#)


# Any extra extensions that should be built
# e.g.: duckdb_extension_load(json)

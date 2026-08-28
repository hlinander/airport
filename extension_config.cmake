# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(airport
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)

# httpfs pinned to the commit used by DuckDB v1.5.5 (matches the duckdb submodule)
duckdb_extension_load(httpfs
    LOAD_TESTS
    DONT_LINK
    GIT_URL https://github.com/duckdb/duckdb-httpfs
    GIT_TAG 827222fb45a043a7a852d1f7aae46901492a3cda
    INCLUDE_DIR extension/httpfs/include
)


# Any extra extensions that should be built
# e.g.: duckdb_extension_load(json)
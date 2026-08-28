PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Core extensions that we need for testing
#CORE_EXTENSIONS='httpfs'

# Configuration of extension
EXT_NAME=airport
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# duckvis embeds this pinned DuckDB fork through duckdb-rs. A loadable extension
# must match the host version string, so keep Airport on the same one.
OVERRIDE_GIT_DESCRIBE ?= v1.5.5

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile
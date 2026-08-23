PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Core extensions that we need for testing
#CORE_EXTENSIONS='httpfs'

# Configuration of extension
EXT_NAME=airport
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Duckvis embeds this pinned DuckDB fork through duckdb-rs under the v1.4.4
# compatibility version. Keep Airport's extension metadata on the same version.
OVERRIDE_GIT_DESCRIBE ?= v1.4.4

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

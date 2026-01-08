{
  description = "Development environment for SwanLake";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    rust-overlay = {
      url = "github:oxalica/rust-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, rust-overlay, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        overlays = [ (import rust-overlay) ];
        pkgs = import nixpkgs {
          inherit system overlays;
        };

        rustToolchain = pkgs.rust-bin.stable.latest.default.override {
          extensions = [ "rust-src" "rust-analyzer" "clippy" "rustfmt" ];
        };

        # Arrow with static libraries enabled for self-contained extension builds
        # Disable cloud storage integrations to reduce dependencies
        arrow-cpp-static = pkgs.arrow-cpp.overrideAttrs (oldAttrs: {
          cmakeFlags = (oldAttrs.cmakeFlags or []) ++ [
            "-DARROW_BUILD_STATIC=ON"
            "-DARROW_BUILD_SHARED=ON"
            # Disable cloud storage to avoid needing Azure SDK, GCS SDK, S3 SDK
            "-DARROW_S3=OFF"
            "-DARROW_GCS=OFF"
            "-DARROW_AZURE=OFF"
            # Keep what we need
            "-DARROW_FLIGHT=ON"
            "-DARROW_PARQUET=ON"
            "-DARROW_ORC=OFF"
          ];
        });

        # Dependencies required for building
        nativeBuildInputs = with pkgs; [
          rustToolchain
          pkg-config
          cmake
          ninja
          protobuf
          git
          tmux
	  helix
 	  fish
        ];

        # Dependencies required at runtime or for linking
        buildInputs = with pkgs; [
          openssl
          duckdb
          zlib
          sqlite
          llvmPackages.libclang
          # Airport extension dependencies (using static arrow build)
          grpc
          msgpack-cxx
          c-ares
          abseil-cpp
          re2
          snappy
          lz4
          zstd
          brotli
          thrift
          rapidjson
          xsimd
          utf8proc
          boost
          glog
          gflags
          bzip2
          nlohmann_json
          curl
        ] ++ [ arrow-cpp-static ] ++ pkgs.lib.optionals pkgs.stdenv.isDarwin [
          pkgs.darwin.apple_sdk.frameworks.Security
          pkgs.darwin.apple_sdk.frameworks.SystemConfiguration
          pkgs.darwin.apple_sdk.frameworks.CoreFoundation
          pkgs.libiconv
        ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
          # This provides libstdc++.so.6 on Linux
          pkgs.stdenv.cc.cc.lib
        ];

        # Library path for runtime linking
        libraryPath = pkgs.lib.makeLibraryPath ([
          pkgs.openssl
          pkgs.duckdb
          arrow-cpp-static
          pkgs.grpc
          pkgs.c-ares
          pkgs.abseil-cpp
          pkgs.re2
        ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
          pkgs.stdenv.cc.cc.lib
        ]);
      in
      {
        devShells.default = pkgs.mkShell {
          inherit nativeBuildInputs buildInputs;

          # Environment variables to help Rust find tools/libraries
          PROTOC = "${pkgs.protobuf}/bin/protoc";
          PROTOC_INCLUDE = "${pkgs.protobuf}/include";
          LIBCLANG_PATH = "${pkgs.llvmPackages.libclang.lib}/lib";

          # Platform-specific library path
          LD_LIBRARY_PATH = pkgs.lib.optionalString pkgs.stdenv.isLinux libraryPath;
          DYLD_LIBRARY_PATH = pkgs.lib.optionalString pkgs.stdenv.isDarwin libraryPath;

          # Help cmake find Arrow and other packages
          CMAKE_PREFIX_PATH = pkgs.lib.makeSearchPath "" ([
            arrow-cpp-static
            pkgs.grpc
            pkgs.msgpack-cxx
            pkgs.c-ares
            pkgs.abseil-cpp
            pkgs.re2
            pkgs.snappy
            pkgs.lz4
            pkgs.zstd
            pkgs.brotli
            pkgs.thrift
            pkgs.rapidjson
            pkgs.xsimd
            pkgs.utf8proc
            pkgs.boost
            pkgs.glog
            pkgs.gflags
            pkgs.bzip2
            pkgs.nlohmann_json
            pkgs.curl
          ]);

          # Set CMAKE_MODULE_PATH so Arrow's custom Find modules can be found
          CMAKE_MODULE_PATH = "${arrow-cpp-static}/lib/cmake/Arrow";

          shellHook = ''
            echo "--- SwanLake Dev Shell ---"
            echo "Platform: ${pkgs.stdenv.hostPlatform.system}"
            echo "Ready to run: cargo run --bin swanlake"
          '';
        };
      }
    );
}

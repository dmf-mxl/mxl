# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

{
  description = "Flake for MXL dev";

  inputs = {
    nixpkgs.url = "https://flakehub.com/f/NixOS/nixpkgs/*.tar.gz";
    rust-overlay = {
      url = "github:oxalica/rust-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = {
    self,
    nixpkgs,
    rust-overlay
  }: let
    overlays = [
      (import rust-overlay)
      (self: super: {
        rustStable = super.rust-bin.stable."1.88.0".default;
        rustNightly = super.rust-bin.nightly."2025-06-26".default;
      })
    ];

    allSystems = [
      "x86_64-linux" # 64-bit Intel/AMD Linux
      "aarch64-linux" # 64-bit ARM Linux
      "x86_64-darwin" # 64-bit Intel macOS
      "aarch64-darwin" # 64-bit ARM macOS
    ];

    forAllSystems = f:
      nixpkgs.lib.genAttrs allSystems (system:
        f {
          pkgs = import nixpkgs {
            inherit overlays system;
          };
        }
      );
  in {
    devShells = forAllSystems ({pkgs}: {
      default = pkgs.mkShell {
        LIBCLANG_PATH = pkgs.lib.makeLibraryPath [pkgs.llvmPackages_latest.libclang.lib];
        packages =
          (with pkgs; [
            rustStable
            rust-analyzer
            clang
            cmake
            pkg-config
          ]);
        };
      }
    );
    nightly = forAllSystems ({pkgs}: {
      default = pkgs.mkShell {
        LIBCLANG_PATH = pkgs.lib.makeLibraryPath [pkgs.llvmPackages_latest.libclang.lib];
        packages =
          (with pkgs; [
            rustNightly
            rust-analyzer
            clang
            cmake
            pkg-config
          ]);
        };
      }
    );
  };
}

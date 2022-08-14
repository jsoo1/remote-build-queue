{
  description = "Experimental queueing system for Nix' remote builds";

  inputs = {
    flake-utils = {
      url = "github:numtide/flake-utils";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, flake-utils, nixpkgs, ... }:

    let
      overlay = import ./nix/overlay.nix;
    in

    flake-utils.lib.eachDefaultSystem
      (system: rec {
        defaultPackage = packages.callPackage ./. rec {
          POSTGRES_URI = "postgres://${builder}@localhost/${database}";
          database = "remote_builds";
          admin = "nix";
          builder = "nixbld";
        };

        devShell = defaultPackage.overrideAttrs (o: {
          nativeBuildInputs = o.nativeBuildInputs ++ [
            packages.clang-tools
            packages.gdb
            packages.gnumake
            packages.nixpkgs-fmt
            packages.postgresql
          ];
        });

        packages = import nixpkgs {
          inherit system;
          overlays = [ overlay ];
        };
      }) // rec {

      overlays.default = overlay;

      nixosModules.default = nixosModules.remote-build-queue;

      nixosModules.remote-build-queue = import ./nix/module.nix;
    };
}

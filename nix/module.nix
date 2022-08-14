{ config, lib, pkgs, ... }:

let
  options = {
    nix.remote-build-queue.enable = lib.mkEnableOption "remote-build-queue";
  };
in

{
  inherit options;
  imports = [ ./postgres.nix ];
  config = lib.mkIf config.nix.remote-build-queue.enable {
    # inherit nix;
  };
}

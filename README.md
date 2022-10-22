# remote-build-queue - a drop-in replacement for Nix' [build-hook](https://github.com/NixOS/nix/blob/master/src/build-remote/build-remote.cc)

This is in a draft state - *DO NOT USE IT*. This is in the open as a reference and source for ideas, not for production (or even personal) use.

`remote-build-queue` currently implements:
- `enqueue` - `build-hook` to be exec'd by nix-build as a drop-in replacement for the [current build-hook](https://github.com/NixOS/nix/blob/master/src/build-remote/build-remote.cc)
- `remote-build-queue` daemon - collects events and builds jobs
- A postgres database acting as an append-only message-queue
- A nixos configuration for `remote-build-queue` and the associated postgres database

Todo:
- [ ] Job cancellation on client disconnect
  + This is hard because the build-hook is killed with `SIGKILL`, given no chance to cleanup
- [ ] Broadcasting build logs
  + My initial thought was to broadcast over UDP
- [ ] Copying build products back to the client
  + Still thinking about how this might work
- [ ] Content-addressable builds
  + Should be a matter of translating some of the finnickier bits of the current hook.
- [ ] Simplify postgres nixos configuration
  + Currently the configuration is too complex, assuming a multi-tenent database on the host
  + Would be a good fit for nixos-containers instead
- [ ] Proper settings transfer to daemon
  + Settings should propagate from client to daemon, like the current hook does.
- [ ] Improved scheduling, machine availability management
  + One goal of this project is to improve scheduling and machine availability
  + But right now, it just does the simplest thing and copies the current build-hook

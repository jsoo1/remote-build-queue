# This makefile is just here for convenience' sake
# Production use cases are via nix/OS.

# Standard compiling and checking stuff
# Helpful for nvim :make command.
# (In particular, :make format compile is pretty sweet)
compile: NINJA ?= ninja
compile: build ; $(NINJA) -C build

build: SYSTEM ?= $(shell nix-instantiate --eval --expr 'builtins.currentSystem')
build: MESON ?= meson
build: | ; CPPFLAGS='-DSYSTEM=\"$(SYSTEM)\"' $(MESON) $@

format: NIXPKGS_FMT ?= nixpkgs-fmt
format: NINJA ?= ninja
format: build; $(NINJA) -C build clang-format; $(NIXPKGS_FMT) *.nix

clean: ; rm -rf build || true

serve: VERBOSITY ?= -vvvvv
serve: nixclean result ; ./result/bin/remote-build-queue $(VERBOSITY)

BUILD_LOCALLY := --builders ''
DEFAULT_HOOK := --build-hook "$(shell which nix) __build-remote"

# More convoluted nix situations for running the app
# This is to make this distclean/run cycle work
result: clean nixclean | ; nix build .# $(BUILD_LOCALLY) $(DEFAULT_HOOK)

result-debug: clean nixclean | ; nix build .# .#remote-build-queue.debug $(BUILD_LOCALLY) $(DEFAULT_HOOK)

debug: PID ?=
debug: nixclean result-debug
	NIX_DEBUG_INFO_DIRS=./result-debug/lib/debug$${NIX_DEBUG_INFO_DIRS:+:$$NIX_DEBUG_INFO_DIRS} \
		gdb ./result/bin/remote-build-queue $(PID) --directory=src

nixclean: ; rm result* || true
distclean: nixclean ; nix-store --delete $(shell readlink result)

# Test the current changes. Thanks to the funky execve of the build-hook
# This is the only real way to run the enqueue binary (aside from making
# a test harness, maybe?) because argv[0] gets overwritten.
run: VERBOSITY ?= -vvvv
run: SYSTEM ?= $(shell nix-instantiate --eval --expr 'builtins.currentSystem')
run: ATTR ?= linux
run: nixclean result
	nix build --rebuild -L .#packages.$(SYSTEM).$(ATTR) \
		--build-hook "$$(readlink result)/libexec/enqueue nixbld localhost 5432 remote_builds" \
		$(BUILDERS) \
		--max-jobs 0 \
		$(VERBOSITY)

# Force the remote-build-queue to do table init again
# I am really just storing this for posterity. I was so
# impressed with this umask usage. The umask is actually
# xored or something via sudo such that the resulting
# permissions are 0600 (what we want, aka only readable
# by nix user). See this answer:
# https://unix.stackexchange.com/a/696393
ADMIN_HOME = $(shell echo ~nix)
$(ADMIN_HOME)/.remote-build-queue-init: | ; echo -n '' | (umask 0077; sudo -u nix tee $@)

# Fast way of reloading sql definitions
api db schema job:
	psql $(POSTGRES_URI) -f <(sed -E 's @admin@ nix g' sql/$@.sql \
		| sed -E 's @builder@ nixbld g' \
		| sed -E 's @schema@ nix g' \
		| sed -E 's @database@ remote_builds g')

.PHONY: api clean compile nixclean debug distclean format run job db schema serve

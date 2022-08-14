{ boost
, fetchFromGitHub
, lib
, makeWrapper
, meson
, ninja
, nix
, nlohmann_json
, openssh
, pkg-config
, postgresql
, stdenv
  # Required as a build parameter since the
  # build-hook runs in a sandboxed environment
, POSTGRES_URI ? "postgres://${builder}@localhost/${database}"
, database ? "remote_builds"
, admin ? "nix"
, builder ? "nixbld"
, system
}:

stdenv.mkDerivation {
  name = "remote-build-queue";

  version = "0.1.0";

  src = ./.;

  separateDebugInfo = true;

  nativeBuildInputs = [ makeWrapper meson ninja pkg-config ];

  buildInputs = [ boost nix openssh postgresql ];

  CPPFLAGS = "-DSYSTEM=\\\"${system}\\\"";

  postPatch = ''
    echo substituting for ${database} ${admin} and ${builder}
    for script in sql/*.sql src/{*.{cc,hh},**/*.{cc,hh}}
    do
      echo substituting $script
      substituteInPlace $script \
          --subst-var-by database ${database} \
          --subst-var-by admin ${admin} \
          --subst-var-by schema ${admin} \
          --subst-var-by builder ${builder} 
    done
  '';

  postInstall = ''
    wrapProgram $out/bin/remote-build-queue --prefix PATH : ${openssh}/bin
  '';

  meta = {
    description = "Experimental queueing system for Nix' remote builds";

    platforms = lib.platforms.all;

    maintainers = [ lib.maintainers.jsoo1 ];

    license = lib.licenses.asl20;
  };
}

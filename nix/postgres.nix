{ config, lib, pkgs, ... }:

let
  self = {
    options.nix.remote-build-queue.postgres = {
      enable = lib.mkOption {
        type = lib.types.bool;
        default = true;
        description = ''
          Enables the postgres backend of the remote build queue.
        '';
      };

      host = lib.mkOption {
        type = lib.types.str;
        example = "/run/my-postgresql-dir";
        description = ''
          Host of the remote build queue.
        '';
      };

      database = lib.mkOption {
        type = lib.types.str;
        default = "remote_builds";
        description = ''
          The database name to use for postgres backend of the remote
          build queue.

          WARNING: There is a possibility that this role can have
          superuser if systemd.services.remote-build-queue-init-db
          does not succeed and revoke the role.
        '';
      };

      admin = lib.mkOption {
        type = lib.types.str;
        default = "nix";
        description = ''
          The database administrator user for the postgres backend of
          the remote build queue.  Also determines a user-private schema
          for the role.
        '';
      };

      adminPasswordFile = lib.mkOption {
        type = lib.types.path;
        example = "/etc/postgresql/admin-pass";
        description = ''
          Path to a text file containing the admin users' password on
          a single line. It must be readable by the postgres user.
        '';
      };

      builder = lib.mkOption {
        type = lib.types.str;
        default = "nixbld";
        description = ''
          The database role for builders for the postgres backend of
          the remote build queue.
        '';
      };

      builderPasswordFile = lib.mkOption {
        type = lib.types.path;
        example = "/etc/postgresql/builder-pass";
        description = ''
          Path to a text file containing the builder users' password on.
          on a single line. It must be readable by the postgres user.
        '';
      };

      authFormat = lib.mkOption {
        type = lib.types.functionTo (lib.types.functionTo lib.types.str);
        example = lib.literalExpression ''user: dbname: "local ''${dbname} ''${user} peer'';
        description = ''
          How to format a postgres auth line. Might be used to override
          default authentication method or other parameters.

          Default distinguishes between tcp/ip and local connection
          types and uses scram-sha-256 authentication.

          It is provided the configured database name. And the username
          of the builder and admin users.
        '';
      };

      pgPassFormat = lib.mkOption {
        type = lib.types.functionTo
          (lib.types.functionTo
            (lib.types.functionTo
              (lib.types.functionTo
                (lib.types.functionTo
                  (lib.types.functionTo lib.types.str)))));
        example = lib.literalExpression ''
          user: host: port: database: username: password:
            "*:*:''${database}:''${username}:''${password};
        '';
        description = ''
          How to format the pgpass files for the builder and admin system
          users. Used to authenticate to the remote_builds database.

          The parameters provided in order are:

          user, host, port, database, username, and password.

          (user is the system user and username is the database username)

          The type of pgPassFormat is:

          pgPassFormat : user -> string -> port -> string -> string -> string -> string

          See the <link xlink:href="https://www.postgresql.org/docs/14/libpq-pgpass.html">
          pgpass documentation</link>.
        '';
      };
    };

    config = lib.mkMerge [
      (lib.mkIf cfg.enable {
        inherit
          nix
          nixpkgs
          services
          system
          systemd
          users
          ;
      })

      (lib.mkIf (!postgresql.enableTCPIP) {
        nix.remote-build-queue.postgres = {
          # FIXME: Get $RUNTIME_DIRECTORY programmatically
          host = lib.mkDefault "/run/postgresql";

          authFormat = lib.mkDefault (dbname: user:
            "local ${dbname} ${user} scram-sha-256");

          pgPassFormat = lib.mkDefault
            (user: host: port: database: username: password:
              "localhost:*:${database}:${username}:${password}");
        };
      })

      (lib.mkIf postgresql.enableTCPIP {
        nix.remote-build-queue.postgres = {
          host = lib.mkDefault "localhost";

          authFormat = lib.mkDefault (dbname: user:
            "host ${dbname} ${user} ::1/0 scram-sha-256");

          pgPassFormat = lib.mkDefault
            (user: host: port: database: username: password:
              "${host}:${toString port}:${database}:${username}:${password}");
        };
      })
    ];
  };

  cfg = config.nix.remote-build-queue.postgres;

  postgresql = config.services.postgresql;

  nix.extraOptions =
    let
      argv = [
        "${pkgs.remote-build-queue}/libexec/enqueue"
        cfg.builder
        cfg.host
        (toString postgresql.port)
        cfg.database
      ];
    in
    ''
      build-hook = ${lib.concatStringsSep " " argv}
    '';

  nixpkgs.overlays = lib.mkAfter
    [
      (_: prev: {
        remote-build-queue = prev.remote-build-queue.override {
          nix = config.nix.package;

          # TODO: Might be able to use argv, look into it
          # Because the build-hook runs in a sandbox, the db configurations
          # must be compile-time params
          inherit (cfg) database admin builder;
        };
      })
    ];

  services = {
    postgresql = {
      enable = true;

      authentication = ''
        ${cfg.authFormat cfg.database cfg.admin}

        ${cfg.authFormat cfg.database cfg.builder}
      '';
    };

    xserver.displayManager.hiddenUsers = [ cfg.admin ];
  };

  users.users."${cfg.admin}" = {
    description = "Nix' Remote Build Queue Database Administrator";

    isSystemUser = true;

    group = "postgres";

    extraGroups = [ "nixbld" ];

    createHome = true;

    homeMode = "0755";

    home = "/var/lib/remote-build-queue/${cfg.admin}";
  };

  system.activationScripts.remote-build-queue-pgpasses =
    let
      # See the postgresql password file:
      # https://www.postgresql.org/docs/14/libpq-pgpass.html
      mkPgPass = passFile: dbuser: user: ''
        cat <<EOF > "${user.home}/.pgpass"
        ${cfg.pgPassFormat user cfg.host postgresql.port cfg.database dbuser ''$(cat "${passFile}")''}
        EOF

        chown ${user.name}:${user.group} "${user.home}/.pgpass"

        chmod 0600 "${user.home}/.pgpass"
      '';

      rootPgPassFile =
        mkPgPass cfg.builderPasswordFile cfg.builder config.users.users.root;

      adminPgPassFile =
        mkPgPass cfg.adminPasswordFile cfg.admin config.users.users."${cfg.admin}";
    in

    lib.stringAfter [ "users" ] ''
      chmod 0775 ${config.users.users."${cfg.admin}".home}

      ${rootPgPassFile}

      ${adminPgPassFile}
    '';

  systemd.services = {
    postgresql = {
      postStart = ''
        if ! ($PSQL -tAc "
              SELECT 1
              FROM pg_database
              WHERE datname = '${cfg.database}'" \
          | grep -q 1)
        then
          echo Setting up remote build queue database and roles

          $PSQL -f ${pkgs.remote-build-queue}/libexec/sql/db.sql

          $PSQL -c '\password ${cfg.admin}' <<EOF
        $(cat "${cfg.adminPasswordFile}")
        $(cat "${cfg.adminPasswordFile}")
        EOF

          $PSQL -c '\password ${cfg.builder}' <<EOF
        $(cat "${cfg.builderPasswordFile}")
        $(cat "${cfg.builderPasswordFile}")
        EOF

          # See remote-build-queue-init-db script
          touch ${config.users.users."${cfg.admin}".home}/.remote-build-queue-init

          chmod g+rw ${config.users.users."${cfg.admin}".home}/.remote-build-queue-init

          echo Finished setting up remote build queue database and roles
        fi
      '';
    };

    remote-build-queue-init-db = {
      description = "remote build queue database setup";

      wants = [ "postgresql.service" ];

      after = [ "postgresql.service" ];

      wantedBy = [ "remote-build-queue.service" ];

      before = [ "remote-build-queue.service" ];

      # This is because this comes before/is part of the nix-daemon service.
      unitConfig.RequiresMountsFor = "/nix/store";

      serviceConfig = {
        User = cfg.admin;

        Group = users.users."${cfg.admin}".group;

        Type = "oneshot";

        # This is because we want to be restarted if postgresql is restarted.
        RemainAfterExit = "yes";
      };

      environment.PSQL =
        "${postgresql.package}/bin/psql -w -U ${cfg.admin} -h ${cfg.host} -d ${cfg.database}";

      script = ''
        if test -e ${config.users.users."${cfg.admin}".home}/.remote-build-queue-init
        then
          $PSQL -f ${pkgs.remote-build-queue}/libexec/sql/schema.sql

          $PSQL -f ${pkgs.remote-build-queue}/libexec/sql/job.sql

          $PSQL -f ${pkgs.remote-build-queue}/libexec/sql/api.sql

          rm -f ${config.users.users."${cfg.admin}".home}/.remote-build-queue-init
        fi
      '';
    };

    remote-build-queue = {
      description = "Nix' remote build queue";

      wants = [ "remote-build-queue-init-db.service" ];

      after = [ "remote-build-queue-init-db.service" ];

      wantedBy = [ "nix-daemon.service" ];

      before = [ "nix-daemon.service" ];

      unitConfig.RequiresMountsFor = "/nix/store";

      environment = {
        PG_USER = cfg.builder;

        PG_HOST = "localhost";

        PG_PORT = toString postgresql.port;

        PG_DBNAME = cfg.database;
      };

      serviceConfig = {
        ExecStart = "${pkgs.remote-build-queue}/bin/remote-build-queue";

        KillMode = "process";
      };
    };
  };
in

self

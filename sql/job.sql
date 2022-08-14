-- A job in @database@
-- To be executed by @admin@ user
-- TODO: Figure out how to make these CREATE DOMAINs equivalent to IF NOT EXISTS

-- https://github.com/NixOS/nix/blob/2.8.1/src/libutil/hash.cc#L83-L84
-- "0123456789abcdfghijklmnpqrsvwxyz" aka 'base32'
CREATE DOMAIN @schema@.base32 AS text CHECK (VALUE ~ '^[0-9a-df-np-sv-z]{32}');

CREATE DOMAIN @schema@.drv_filename AS @schema@.base32 CHECK (VALUE ~ '\.drv$');

CREATE DOMAIN @schema@.output_path AS @schema@.base32 CHECK (VALUE !~ '\.drv$');

CREATE DOMAIN @schema@.textword AS text CHECK (VALUE <> '' AND VALUE !~ '[ \n\t\r]');

CREATE TYPE @schema@.event AS ENUM (
  'start',
  'cancel',
  'no-machine-available',
  'accept',
  'add-inputs-and-outputs',
  'fail'
);

CREATE TABLE IF NOT EXISTS @schema@.drvs(
  id uuid DEFAULT @schema@.uuid_generate_v4() PRIMARY KEY,
  filename @schema@.drv_filename NOT NULL,
  UNIQUE (filename)
);

CREATE TABLE IF NOT EXISTS @schema@.inputs(
  id uuid DEFAULT @schema@.uuid_generate_v4() PRIMARY KEY,
  filename @schema@.output_path NOT NULL,
  UNIQUE(filename)
);

CREATE TABLE IF NOT EXISTS @schema@.outputs(
  id uuid DEFAULT @schema@.uuid_generate_v4() PRIMARY KEY,
  name @schema@.textword NOT NULL,
  UNIQUE(name)
);

CREATE TABLE IF NOT EXISTS @schema@.systems(
  id uuid DEFAULT @schema@.uuid_generate_v4() PRIMARY KEY,
  name @schema@.textword NOT NULL,
  UNIQUE (name) 
);

CREATE TABLE IF NOT EXISTS @schema@.system_features(
  id uuid DEFAULT @schema@.uuid_generate_v4() PRIMARY KEY,
  name @schema@.textword NOT NULL,
  UNIQUE (name)
);

CREATE TABLE IF NOT EXISTS @schema@.jobs(
  id uuid DEFAULT @schema@.uuid_generate_v4() PRIMARY KEY,
  drv uuid NOT NULL REFERENCES @schema@.drvs(id),
  system uuid NOT NULL REFERENCES @schema@.systems(id)
);

CREATE INDEX jobs_drvs ON @schema@.jobs (drv);

CREATE INDEX jobs_systems ON @schema@.jobs (system);

CREATE TABLE IF NOT EXISTS @schema@.input_drvs(
  drv uuid NOT NULL REFERENCES @schema@.drvs(id),
  input uuid NOT NULL CHECK (input <> drv) REFERENCES @schema@.drvs(id)
);

CREATE INDEX input_drvs_drv ON @schema@.input_drvs (drv);

CREATE INDEX input_drvs_input ON @schema@.input_drvs (input);

CREATE TABLE IF NOT EXISTS @schema@.job_system_features(
  job uuid NOT NULL REFERENCES @schema@.jobs(id),
  feature uuid NOT NULL REFERENCES @schema@.system_features(id)
);

CREATE INDEX job_system_features_job ON @schema@.job_system_features (job);

CREATE INDEX job_system_features_job ON @schema@.job_system_features (feature);

CREATE TABLE IF NOT EXISTS @schema@.machines(
  id uuid DEFAULT @schema@.uuid_generate_v4() PRIMARY KEY,
  uri @schema@.textword NOT NULL,
  UNIQUE(uri)
);

CREATE TABLE IF NOT EXISTS @schema@.job_machines(
  job uuid NOT NULL REFERENCES @schema@.jobs(id),
  machine uuid NOT NULL REFERENCES @schema@.machines(id)
);

CREATE INDEX job_machines_job ON @schema@.job_machines (job);

CREATE INDEX job_machines_machine ON @schema@.job_machines (machine);

CREATE TABLE IF NOT EXISTS @schema@.events(
  ts timestamptz NOT NULL DEFAULT now(),
  name @schema@.event NOT NULL,
  job uuid NOT NULL REFERENCES @schema@.jobs(id)
);

CREATE INDEX events_ts ON @schema@.events (ts);

CREATE INDEX events_job ON @schema@.events (job);

CREATE TABLE IF NOT EXISTS @schema@.job_inputs(
  job uuid NOT NULL REFERENCES @schema@.jobs(id),
  input uuid NOT NULL REFERENCES @schema@.inputs(id)
);

CREATE INDEX job_inputs_job ON @schema@.job_inputs (job);

CREATE INDEX job_inputs_input ON @schema@.job_inputs (input);

CREATE TABLE IF NOT EXISTS @schema@.job_outputs(
  job uuid NOT NULL REFERENCES @schema@.jobs(id),
  output uuid NOT NULL REFERENCES @schema@.outputs(id)
);

CREATE INDEX job_outputs_job ON @schema@.job_outputs (job);

CREATE INDEX job_outputs_output ON @schema@.job_outputs (output);

CREATE TABLE IF NOT EXISTS @schema@.errors(
  id uuid DEFAULT @schema@.uuid_generate_v4() PRIMARY KEY,
  msg text NOT NULL
);

CREATE TABLE @schema@.job_errors(
  job uuid NOT NULL REFERENCES @schema@.jobs(id),
  error uuid NOT NULL REFERENCES @schema@.errors(id)
);

CREATE INDEX job_errors_job ON @schema@.job_errors(job);

CREATE INDEX job_errors_error ON @schema@.job_errors(error);

GRANT SELECT ON ALL TABLES IN SCHEMA @schema@ TO @builder@;

GRANT INSERT ON ALL TABLES IN SCHEMA @schema@ TO @builder@;

GRANT UPDATE ON ALL TABLES IN SCHEMA @schema@ TO @builder@;


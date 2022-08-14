-- User-facing api to @database@
-- To be run as @admin@

DROP VIEW IF EXISTS @schema@.view_jobs;

CREATE TYPE @schema@.job_event_ts AS (name @schema@.event, ts timestamptz);

CREATE VIEW @schema@.view_jobs AS
SELECT
  @schema@.jobs.id AS id,
  @schema@.drvs.filename AS drv,
  @schema@.systems.name AS system,
  array_agg(DISTINCT @schema@.system_features.name)
    FILTER (WHERE @schema@.system_features.name IS NOT NULL)
    AS system_features,
  array_agg(DISTINCT @schema@.outputs.name) AS outputs,
  array_agg(DISTINCT @schema@.inputs.filename)
    FILTER (WHERE @schema@.inputs.filename IS NOT NULL)
    AS inputs,
  @schema@.machines.uri AS machine,
  @schema@.errors.msg AS error,
  ARRAY(
    SELECT ROW(@schema@.events.name, @schema@.events.ts)::@schema@.job_event_ts
    FROM @schema@.events
    WHERE @schema@.events.job = @schema@.jobs.id
  ) AS events
FROM
  @schema@.jobs
  INNER JOIN @schema@.drvs ON @schema@.drvs.id = @schema@.jobs.drv
  INNER JOIN @schema@.systems ON @schema@.systems.id = @schema@.jobs.system
  LEFT JOIN @schema@.job_system_features related_system_features
  ON @schema@.jobs.id = related_system_features.job
  LEFT JOIN @schema@.system_features
  ON @schema@.system_features.id = related_system_features.feature
  INNER JOIN @schema@.job_outputs related_outputs
  ON @schema@.jobs.id = related_outputs.job
  INNER JOIN @schema@.outputs
  ON @schema@.outputs.id = related_outputs.output
  LEFT JOIN @schema@.job_inputs related_inputs
  ON @schema@.jobs.id = related_inputs.job
  LEFT JOIN @schema@.inputs
  ON @schema@.inputs.id = related_inputs.input
  LEFT JOIN @schema@.job_machines related_machines
  ON @schema@.jobs.id = related_machines.job
  LEFT JOIN @schema@.machines
  ON @schema@.machines.id = related_machines.machine
  LEFT JOIN @schema@.job_errors related_errors
  ON @schema@.jobs.id = related_errors.job
  LEFT JOIN @schema@.errors
  ON @schema@.errors.id = related_errors.error
GROUP BY
  @schema@.jobs.id,
  @schema@.drvs.filename,
  @schema@.systems.name,
  @schema@.machines.uri,
  @schema@.errors.msg;

DROP VIEW IF EXISTS @schema@.view_events;

CREATE VIEW @schema@.view_events AS
SELECT
  @schema@.events.*,
  @schema@.get_payload(@schema@.events.job, @schema@.events.name) AS payload
FROM @schema@.events;

DROP FUNCTION IF EXISTS @schema@.mk_job;

CREATE FUNCTION @schema@.mk_job(
  IN drv @schema@.drvs.filename%TYPE,
  IN system @schema@.systems.name%TYPE,
  -- aka @schema@.system_features.%TYPE[] but that is not allowed, it seems
  IN system_features @schema@.textword[],
--
  OUT id @schema@.jobs.id%TYPE
) AS $$
WITH new_drv AS (
  INSERT INTO @schema@.drvs (filename) VALUES ($1)
  ON CONFLICT (filename) DO UPDATE SET filename = @schema@.drvs.filename
  RETURNING *
)
, new_system AS (
  INSERT INTO @schema@.systems (name) VALUES ($2)
  ON CONFLICT (name) DO UPDATE SET name = @schema@.systems.name
  RETURNING *
)
, new_system_features AS (
  INSERT INTO @schema@.system_features (name)
  SELECT feat.name FROM ROWS FROM (unnest($3)) AS feat(name)
  ON CONFLICT (name) DO UPDATE SET name = @schema@.system_features.name
  RETURNING *
)
, new_job AS (
  INSERT INTO @schema@.jobs (drv, system)
  SELECT new_drv.id, new_system.id
  FROM new_drv, new_system
  RETURNING *
)
, new_job_system_features AS (
  INSERT INTO @schema@.job_system_features (job, feature)
  SELECT new_job.id, new_system_features.id
  FROM new_job, new_system_features
  RETURNING *
)
SELECT new_job.id FROM new_job;
$$ LANGUAGE SQL VOLATILE STRICT;

DROP FUNCTION IF EXISTS @schema@.get_job;

CREATE FUNCTION @schema@.get_job(
  IN id @schema@.jobs.id%TYPE,
--
  OUT drv @schema@.drvs.filename%TYPE,
  OUT system @schema@.systems.name%TYPE,
  -- aka schema@.system_features.name%TYPE[], but that is not valid, it seems
  OUT system_features @schema@.textword[]
) AS $$
WITH job AS (
  SELECT *
  FROM @schema@.jobs
  WHERE @schema@.jobs.id = $1
)
, system_features AS (
  SELECT array_agg(@schema@.system_features.name) AS names
  FROM @schema@.job_system_features related
  INNER JOIN job
  ON job.id = related.job
  INNER JOIN @schema@.system_features
  ON @schema@.system_features.id = related.feature
)
SELECT
  (SELECT filename FROM @schema@.drvs WHERE id = job.drv) AS drv,
  (SELECT name FROM @schema@.systems WHERE id = job.system) AS system,
  COALESCE(system_features.names, '{}') AS system_features
FROM job, system_features;
$$
LANGUAGE SQL
STABLE
STRICT
PARALLEL SAFE;

DROP FUNCTION IF EXISTS @schema@.enqueue_job;

CREATE FUNCTION @schema@.enqueue_job(
  IN drv @schema@.drvs.filename%TYPE,
  IN system @schema@.systems.name%TYPE,
  IN system_features @schema@.textword[],
--
  OUT job @schema@.events.job%TYPE
) AS $$
INSERT INTO @schema@.events (name, job)
SELECT 'start'::@schema@.event, @schema@.mk_job(
  $1::@schema@.drv_filename,
  $2::@schema@.textword,
  $3::@schema@.textword[]
)
RETURNING job;
$$ LANGUAGE SQL VOLATILE STRICT;

DROP FUNCTION IF EXISTS @schema@.cancel_job;

CREATE FUNCTION @schema@.cancel_job(
  IN job @schema@.jobs.id%TYPE
) RETURNS VOID AS $$
INSERT INTO @schema@.events (name, job)
VALUES ('cancel'::@schema@.event, $1)
$$ LANGUAGE SQL VOLATILE STRICT;

DROP FUNCTION IF EXISTS @schema@.accept_job;

CREATE FUNCTION @schema@.accept_job(
  IN job @schema@.jobs.id%TYPE,
  IN uri @schema@.machines.uri%TYPE
) RETURNS VOID AS $$
WITH new_machine AS (
  INSERT INTO @schema@.machines (uri) VALUES ($2)
  ON CONFLICT (uri) DO UPDATE SET uri = @schema@.machines.uri
  RETURNING *
)
, job_machine AS (
  INSERT INTO @schema@.job_machines (job, machine)
  SELECT $1, new_machine.id
  FROM new_machine
)
INSERT INTO @schema@.events (name, job)
VALUES ('accept'::@schema@.event, $1);
$$ LANGUAGE SQL VOLATILE STRICT;

DROP FUNCTION IF EXISTS @schema@.add_inputs_and_outputs;

CREATE FUNCTION @schema@.add_inputs_and_outputs(
  IN job_id @schema@.jobs.id%TYPE,
  -- Should be inputs @schema@.outputs.name%TYPE[],
  -- But doesn't quite work.
  IN inputs @schema@.output_path[],
  IN outputs @schema@.textword[]
) RETURNS VOID AS $$
WITH new_inputs AS (
  INSERT INTO @schema@.inputs (filename)
  SELECT input.filename FROM ROWS FROM (unnest($2)) AS input(filename)
  ON CONFLICT (filename) DO UPDATE SET filename = @schema@.inputs.filename
  RETURNING *
), new_outputs AS (
  INSERT INTO @schema@.outputs (name)
  SELECT output.name FROM ROWS FROM (unnest($3)) AS output(name)
  ON CONFLICT (name) DO UPDATE SET name = @schema@.outputs.name
  RETURNING *
), new_job_inputs AS (
  INSERT INTO @schema@.job_inputs (job, input)
  SELECT $1, new_inputs.id FROM new_inputs
  RETURNING *
), new_job_output AS (
  INSERT INTO @schema@.job_outputs (job, output)
  SELECT $1, new_outputs.id FROM new_outputs
  RETURNING *
)
INSERT INTO @schema@.events (name, job)
VALUES ('add-inputs-and-outputs'::@schema@.event, $1)
$$ LANGUAGE SQL VOLATILE STRICT;

DROP FUNCTION IF EXISTS @schema@.fail_job;

CREATE FUNCTION @schema@.fail_job(
  IN job @schema@.jobs.id%TYPE,
  IN error text
) RETURNS VOID AS $$
WITH new_error AS (
  INSERT INTO @schema@.errors (msg) VALUES ($2)
  RETURNING *
)
INSERT INTO @schema@.job_errors (job, error)
SELECT $1, new_error.id FROM new_error;
$$ LANGUAGE SQL VOLATILE STRICT;

CREATE OR REPLACE FUNCTION @schema@.get_machine(
  IN job @schema@.jobs.id%TYPE,
--
  OUT uri @schema@.machines.uri%TYPE
) AS $$
SELECT @schema@.machines.uri
FROM @schema@.job_machines
INNER JOIN @schema@.machines
ON @schema@.job_machines.machine = @schema@.machines.id
WHERE @schema@.job_machines.job = $1
$$
LANGUAGE SQL
STABLE
STRICT
PARALLEL SAFE;

CREATE OR REPLACE FUNCTION @schema@.get_inputs_and_outputs(
  IN job @schema@.jobs.id%TYPE,
--
  -- Should be @schema@.inputs.filename%TYPE[]
  OUT inputs @schema@.output_path[],
  -- Should be @schema@.outputs.filename%TYPE[]
  OUT wanted_outputs @schema@.textword[]
) AS $$
WITH inputs AS (
  SELECT array_agg(@schema@.inputs.filename) AS filenames
  FROM @schema@.job_inputs
  INNER JOIN @schema@.inputs
  ON @schema@.job_inputs.input = @schema@.inputs.id
  WHERE @schema@.job_inputs.job = $1
)
, outputs AS (
  SELECT array_agg(@schema@.outputs.name) AS names
  FROM @schema@.job_outputs
  INNER JOIN @schema@.outputs
  ON @schema@.job_outputs.output = @schema@.outputs.id
  WHERE @schema@.job_outputs.job = $1
)
SELECT COALESCE(inputs.filenames, '{}'), outputs.names
FROM inputs, outputs
$$
LANGUAGE SQL
STABLE
STRICT
PARALLEL SAFE;

CREATE OR REPLACE FUNCTION @schema@.get_error(
  IN job @schema@.jobs.id%TYPE,
--
  OUT msg text
) AS $$
SELECT @schema@.errors.msg
FROM @schema@.job_errors
INNER JOIN @schema@.errors
ON @schema@.job_errors.error = @schema@.errors.id
WHERE @schema@.job_errors.job = $1
$$
LANGUAGE SQL
STABLE
STRICT
PARALLEL SAFE;

CREATE OR REPLACE FUNCTION @schema@.get_payload(
  IN job @schema@.events.job%TYPE,
  IN name @schema@.events.name%TYPE,
--
  OUT payload jsonb
) AS $$
SELECT CASE $2
  WHEN 'start' THEN row_to_json(@schema@.get_job($1))::jsonb
  WHEN 'cancel' THEN '{}'::jsonb
  WHEN 'no-machine-available' THEN '{}'::jsonb
  WHEN 'accept' THEN jsonb_build_object('uri', @schema@.get_machine($1))
  WHEN 'add-inputs-and-outputs' THEN row_to_json(@schema@.get_inputs_and_outputs($1))::jsonb
  WHEN 'fail' THEN jsonb_build_object('msg', @schema@.get_error($1))
END 
$$
LANGUAGE SQL
STABLE
STRICT
PARALLEL SAFE;

CREATE OR REPLACE FUNCTION @schema@.notify_events()
RETURNS TRIGGER AS $$
DECLARE
  payload jsonb;
BEGIN
  SELECT @schema@.get_payload(NEW.job, NEW.name) INTO payload;
  PERFORM pg_notify('events', (row_to_json(NEW)::jsonb || jsonb_build_object('payload', payload))::text);
  EXECUTE format('SELECT pg_notify(%L, (row_to_json($1)::jsonb || jsonb_build_object(%L, $2))::text)', NEW.job, 'payload') USING NEW, payload;
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE TRIGGER event_trigger
AFTER INSERT ON @schema@.events
FOR EACH ROW EXECUTE FUNCTION @schema@.notify_events();

CREATE OR REPLACE FUNCTION @schema@.get_events(
  IN id @schema@.events.job%TYPE,
--
  OUT ts @schema@.events.ts%TYPE,
  OUT name @schema@.events.name%TYPE,
  OUT job @schema@.events.job%TYPE,
  OUT payload jsonb
) AS $$
SELECT
  @schema@.events.ts,
  @schema@.events.name,
  @schema@.events.job,
  @schema@.get_payload(@schema@.events.job, @schema@.events.name)
FROM @schema@.events
WHERE @schema@.events.job = $1
$$
LANGUAGE SQL
STABLE
STRICT
PARALLEL SAFE;

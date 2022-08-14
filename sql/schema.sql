-- Schema/database specific setup specific to the @database@ database
-- To be executed by superuser
CREATE SCHEMA IF NOT EXISTS AUTHORIZATION @schema@;

GRANT CREATE, USAGE ON SCHEMA @schema@ TO @schema@;

GRANT USAGE ON SCHEMA @schema@ TO @builder@;

CREATE EXTENSION IF NOT EXISTS "uuid-ossp" WITH SCHEMA @schema@ CASCADE;

ALTER USER @admin@ WITH NOSUPERUSER;

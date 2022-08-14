-- Setup admin role and create database.
-- To be executed by superuser
CREATE ROLE @admin@ WITH LOGIN CREATEDB CREATEROLE SUPERUSER;

ALTER ROLE @admin@ IN DATABASE @database@ SET search_path='@admin@, "$user"';

CREATE DATABASE @database@ WITH OWNER nix;

CREATE ROLE @builder@ WITH LOGIN;

ALTER ROLE @builder@ IN DATABASE @database@ SET search_path='@admin@, "$user"';

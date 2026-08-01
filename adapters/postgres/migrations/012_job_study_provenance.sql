BEGIN;

-- Early-development contract reset: job v7 requires explicit Study
-- provenance for every run-configuration-backed execution.
DELETE FROM thermox_simulation_jobs;

COMMIT;

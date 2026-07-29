#pragma once

#include "thermox/service/simulation_jobs.hpp"

#include <memory>
#include <string>

namespace thermox::postgres {

// Creates a thread-safe job repository backed by PostgreSQL. The schema
// migration must be applied before the repository is used.
std::shared_ptr<service::SimulationJobRepository>
make_postgres_job_repository(std::string connection_string);

}  // namespace thermox::postgres

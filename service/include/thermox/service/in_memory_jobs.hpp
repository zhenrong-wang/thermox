#pragma once

#include "thermox/service/simulation_jobs.hpp"

#include <memory>

namespace thermox::service {

std::shared_ptr<SimulationJobRepository>
make_in_memory_job_repository();

std::shared_ptr<ResultArtifactStore>
make_in_memory_result_artifact_store();

}  // namespace thermox::service

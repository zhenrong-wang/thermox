#pragma once

#include "thermox/service/projects.hpp"

#include <memory>

namespace thermox::service {

std::shared_ptr<ProjectRepository>
make_in_memory_project_repository();
std::shared_ptr<EngineeringArtifactContentStore>
make_in_memory_engineering_artifact_content_store();

}  // namespace thermox::service

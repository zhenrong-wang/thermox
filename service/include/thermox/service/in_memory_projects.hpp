#pragma once

#include "thermox/service/projects.hpp"

#include <memory>

namespace thermox::service {

std::shared_ptr<ProjectRepository>
make_in_memory_project_repository();

}  // namespace thermox::service

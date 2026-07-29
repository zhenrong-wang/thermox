#pragma once

#include "thermox/service/projects.hpp"

#include <memory>
#include <string>

namespace thermox::postgres {

std::shared_ptr<service::ProjectRepository>
make_postgres_project_repository(std::string connection_string);

}  // namespace thermox::postgres

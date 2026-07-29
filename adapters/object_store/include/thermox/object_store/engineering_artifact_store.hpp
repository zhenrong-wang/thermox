#pragma once

#include "thermox/object_store/object_store.hpp"
#include "thermox/service/projects.hpp"

#include <memory>
#include <string>

namespace thermox::object_store {

std::shared_ptr<service::EngineeringArtifactContentStore>
make_object_engineering_artifact_content_store(
    std::shared_ptr<ObjectStore> objects,
    std::string key_prefix = "engineering-artifacts");

}  // namespace thermox::object_store

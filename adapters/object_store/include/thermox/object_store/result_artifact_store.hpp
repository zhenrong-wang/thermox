#pragma once

#include "thermox/object_store/object_store.hpp"
#include "thermox/service/simulation_jobs.hpp"

#include <memory>
#include <string>

namespace thermox::object_store {

// Adapts provider-neutral object bytes to the service's immutable,
// checksummed result-artifact contract.
std::shared_ptr<service::ResultArtifactStore>
make_object_result_artifact_store(
    std::shared_ptr<ObjectStore> objects,
    std::string key_prefix = "results");

}  // namespace thermox::object_store

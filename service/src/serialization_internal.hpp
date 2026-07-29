#pragma once

#include "thermox/platform/model_document.hpp"

#include <string>

namespace thermox::service::detail {

std::string serialize_model_document_json(
    const platform::ModelDocument& document);
std::string serialize_topology_document_json(
    const platform::ModelDocument& document);
std::string serialize_case_document_json(
    const platform::CaseDefinition& simulation_case);

}  // namespace thermox::service::detail

#pragma once

#include "thermox/platform/model_document.hpp"

#include <string>

namespace thermox::service::detail {

std::string serialize_model_document_json(
    const platform::ModelDocument& document);

}  // namespace thermox::service::detail

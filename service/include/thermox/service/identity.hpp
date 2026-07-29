#pragma once

#include <string>

namespace thermox::service {

// Supplied by a trusted caller after authentication. Application workflows
// may use the Team scope for ownership and authorization. Platform, physics,
// and numerical layers never interpret this context.
struct IdentityContext {
    std::string user_id;
    std::string team_id;
    std::string request_id;
};

}  // namespace thermox::service

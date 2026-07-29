#pragma once

#include <string>

namespace thermox::service {

enum class TeamRole {
    regular,
    admin,
};

// Supplied by a trusted caller after authentication. Application workflows
// use the Team scope for tenant isolation and may use the membership role
// for authorization. A user can therefore have a different role in each
// Team. Platform, physics, and numerical layers never interpret this context.
struct IdentityContext {
    std::string user_id;
    std::string team_id;
    std::string request_id;
    TeamRole team_role{TeamRole::regular};
};

}  // namespace thermox::service

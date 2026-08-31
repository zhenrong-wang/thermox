#pragma once

#include "thermox/service/simulation_jobs.hpp"

#include <string>
#include <string_view>

namespace thermox::service {

inline constexpr char balance_report_request_schema_v1[] =
    "thermox.balance_report_request/v1";
inline constexpr char balance_report_schema_v2[] =
    "thermox.balance_report/v2";

struct BalanceReportRequest {
    std::string schema_version{balance_report_request_schema_v1};
    std::string accounting_basis{"energy"};
    std::string system_boundary{"whole_system"};
    std::string diagram_profile{"iso-14084-1:2015"};
    std::string calculation_profile{"none"};
};

// Produces an informative, standards-profiled report from an immutable result
// and its exact canonical topology. It does not claim clause-level conformity.
[[nodiscard]] std::string build_balance_report_json(
    const SimulationJobRecord& job,
    const ResultArtifact& result,
    const std::string& canonical_topology_json,
    const BalanceReportRequest& request);

// Server-owned representations keep report semantics consistent across HTTP,
// CLI, and future graphical clients.
[[nodiscard]] std::string serialize_balance_report_markdown(
    std::string_view report_json);
[[nodiscard]] std::string serialize_balance_report_csv(
    std::string_view report_json);

}  // namespace thermox::service

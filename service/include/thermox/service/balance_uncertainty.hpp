#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thermox::service {

inline constexpr char balance_uncertainty_artifact_type[] =
    "thermox.balance_uncertainty";
inline constexpr char balance_uncertainty_schema_v1[] =
    "thermox.balance_uncertainty/v1";

struct BoundaryStreamUncertainty {
    std::string component_id;
    std::string port_name;
    std::optional<double> mass_flow_standard_uncertainty_si;
    std::optional<double> specific_enthalpy_standard_uncertainty_si;
    std::optional<double> energy_flow_standard_uncertainty_si;
    double mass_enthalpy_correlation{0.0};
};

struct BoundaryUncertaintyCorrelation {
    std::string quantity;
    std::string first_component_id;
    std::string first_port_name;
    std::string second_component_id;
    std::string second_port_name;
    double coefficient{0.0};
};

struct BalanceUncertaintyModel {
    std::string schema_version{balance_uncertainty_schema_v1};
    std::string id;
    std::string source_reference;
    std::string source_checksum_sha256;
    std::string note;
    std::vector<std::string> limitations;
    std::vector<BoundaryStreamUncertainty> streams;
    std::vector<BoundaryUncertaintyCorrelation> correlations;
};

[[nodiscard]] BalanceUncertaintyModel parse_balance_uncertainty_model_json(
    std::string_view model_json);
[[nodiscard]] std::string serialize_balance_uncertainty_model_json(
    const BalanceUncertaintyModel& model);

}  // namespace thermox::service

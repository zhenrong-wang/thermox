#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char gas_turbine_performance_test_schema_v1[] =
    "thermox.gas_turbine_performance_test/v1";

enum class StabilityStatistic {
    maximum_deviation_from_mean,
    standard_deviation,
    reported_compliant,
};

std::string to_string(StabilityStatistic statistic);

enum class ComplianceDisposition {
    passed,
    failed,
    accepted_deviation,
    not_demonstrated,
    not_applicable,
};

std::string to_string(ComplianceDisposition disposition);

enum class CorrectionEvidenceBasis {
    independently_derived,
    manufacturer_curve,
    reported_factor,
    assumption,
};

std::string to_string(CorrectionEvidenceBasis basis);

struct PerformanceMeasurement {
    double value_si{0.0};
    std::optional<double> standard_uncertainty_si;
};

struct PerformanceCorrectionFactor {
    std::string id;
    double factor{1.0};
    CorrectionEvidenceBasis basis{
        CorrectionEvidenceBasis::reported_factor};
    std::string source_reference;
    std::optional<double> relative_standard_uncertainty;
};

struct PerformanceStabilityObservation {
    std::string parameter;
    double observed_variation{0.0};
    double allowed_variation{0.0};
    std::string dimension;
    StabilityStatistic statistic{
        StabilityStatistic::maximum_deviation_from_mean};
};

struct GasTurbinePerformanceRun {
    std::string id;
    double duration_seconds{0.0};
    PerformanceMeasurement gross_generator_power_w;
    PerformanceMeasurement generator_losses_w;
    PerformanceMeasurement fuel_mass_flow_kg_s;
    PerformanceMeasurement fuel_lhv_j_kg;
    PerformanceMeasurement fuel_sensible_enthalpy_j_kg;
    bool include_fuel_sensible_heat{true};
    std::vector<PerformanceCorrectionFactor> output_corrections;
    std::vector<PerformanceCorrectionFactor> heat_rate_corrections;
    std::vector<PerformanceStabilityObservation> stability;
};

struct Iso2314CampaignEvidence {
    bool test_boundary_defined{false};
    bool reference_conditions_defined{false};
    bool raw_time_series_available{false};
    bool instrument_calibrations_available{false};
    bool contemporaneous_fuel_sample_available{false};
    bool correction_curves_available{false};
    bool correction_curve_range_confirmed{false};
    bool uncertainty_analysis_available{false};
    bool deviations_documented_and_approved{false};
};

struct GasTurbinePerformanceTestRequest {
    std::string id;
    std::string equipment_id;
    std::string standard_reference{"ISO 2314:2009"};
    std::vector<GasTurbinePerformanceRun> runs;
    Iso2314CampaignEvidence evidence;
};

struct DerivedPerformanceValue {
    double value_si{0.0};
    std::optional<double> standard_uncertainty_si;
    std::optional<double> expanded_uncertainty_si_k2;
};

struct PerformanceCorrectionResult {
    std::string id;
    double factor{1.0};
    CorrectionEvidenceBasis basis{
        CorrectionEvidenceBasis::reported_factor};
    std::string source_reference;
};

struct PerformanceStabilityResult {
    std::string parameter;
    double observed_variation{0.0};
    double allowed_variation{0.0};
    std::string dimension;
    StabilityStatistic statistic{
        StabilityStatistic::maximum_deviation_from_mean};
    ComplianceDisposition disposition{
        ComplianceDisposition::not_demonstrated};
    std::string note;
};

struct GasTurbinePerformanceRunResult {
    std::string id;
    DerivedPerformanceValue net_generator_power_w;
    DerivedPerformanceValue fuel_thermal_input_w;
    DerivedPerformanceValue thermal_efficiency;
    DerivedPerformanceValue heat_rate_j_per_kwh;
    double output_correction_product{1.0};
    double heat_rate_correction_product{1.0};
    DerivedPerformanceValue corrected_net_generator_power_w;
    DerivedPerformanceValue corrected_thermal_efficiency;
    DerivedPerformanceValue corrected_heat_rate_j_per_kwh;
    std::vector<PerformanceCorrectionResult> output_corrections;
    std::vector<PerformanceCorrectionResult> heat_rate_corrections;
    std::vector<PerformanceStabilityResult> stability;
    ComplianceDisposition duration_disposition{
        ComplianceDisposition::not_demonstrated};
    ComplianceDisposition stability_disposition{
        ComplianceDisposition::not_demonstrated};
    std::vector<std::string> uncertainty_limitations;
};

struct ComplianceCheckResult {
    std::string id;
    ComplianceDisposition disposition{
        ComplianceDisposition::not_demonstrated};
    std::string standard_reference;
    std::string note;
};

struct GasTurbinePerformanceTestResult {
    std::string schema_version{
        gas_turbine_performance_test_schema_v1};
    std::string id;
    std::string equipment_id;
    std::string standard_reference;
    std::vector<GasTurbinePerformanceRunResult> runs;
    double average_corrected_net_generator_power_w{0.0};
    double average_corrected_heat_rate_j_per_kwh{0.0};
    double average_corrected_thermal_efficiency{0.0};
    std::vector<ComplianceCheckResult> compliance;
    bool iso_conformity_demonstrated{false};
    std::size_t passed_count{0};
    std::size_t accepted_deviation_count{0};
    std::size_t failed_count{0};
    std::size_t not_demonstrated_count{0};
    std::vector<std::string> limitations;
};

class PerformanceTestError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

GasTurbinePerformanceTestResult evaluate_gas_turbine_performance_test(
    const GasTurbinePerformanceTestRequest& request);

GasTurbinePerformanceTestRequest
parse_gas_turbine_performance_test_request_json(
    const std::string& text);

GasTurbinePerformanceTestResult
evaluate_gas_turbine_performance_test_json(
    const std::string& text);

std::string serialize_gas_turbine_performance_test_result_json(
    const GasTurbinePerformanceTestResult& result);

}  // namespace thermox::service

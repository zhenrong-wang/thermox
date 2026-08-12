#include "thermox/service/performance_test.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>

namespace thermox::service {

namespace {

constexpr double iso_recommended_run_duration_s = 1800.0;

using Json = nlohmann::json;

PerformanceMeasurement parse_measurement(
    const Json& value,
    const std::string& field) {
    if (!value.is_object() || !value.contains("value_si")) {
        throw PerformanceTestError(field + " must contain value_si");
    }
    PerformanceMeasurement result;
    result.value_si = value.at("value_si").get<double>();
    if (value.contains("standard_uncertainty_si") &&
        !value.at("standard_uncertainty_si").is_null()) {
        result.standard_uncertainty_si =
            value.at("standard_uncertainty_si").get<double>();
    }
    return result;
}

CorrectionEvidenceBasis correction_basis_from_string(
    const std::string& value) {
    if (value == "independently_derived") {
        return CorrectionEvidenceBasis::independently_derived;
    }
    if (value == "manufacturer_curve") {
        return CorrectionEvidenceBasis::manufacturer_curve;
    }
    if (value == "reported_factor") {
        return CorrectionEvidenceBasis::reported_factor;
    }
    if (value == "assumption") {
        return CorrectionEvidenceBasis::assumption;
    }
    throw PerformanceTestError(
        "unknown correction evidence basis: " + value);
}

StabilityStatistic stability_statistic_from_string(
    const std::string& value) {
    if (value == "maximum_deviation_from_mean") {
        return StabilityStatistic::maximum_deviation_from_mean;
    }
    if (value == "standard_deviation") {
        return StabilityStatistic::standard_deviation;
    }
    if (value == "reported_compliant") {
        return StabilityStatistic::reported_compliant;
    }
    throw PerformanceTestError("unknown stability statistic: " + value);
}

std::vector<PerformanceCorrectionFactor> parse_corrections(
    const Json& run,
    const char* key) {
    std::vector<PerformanceCorrectionFactor> result;
    if (!run.contains(key)) return result;
    for (const auto& value : run.at(key)) {
        PerformanceCorrectionFactor correction;
        correction.id = value.at("id").get<std::string>();
        correction.factor = value.at("factor").get<double>();
        correction.basis = correction_basis_from_string(
            value.value("basis", "reported_factor"));
        correction.source_reference =
            value.value("source_reference", "");
        if (value.contains("relative_standard_uncertainty") &&
            !value.at("relative_standard_uncertainty").is_null()) {
            correction.relative_standard_uncertainty =
                value.at("relative_standard_uncertainty").get<double>();
        }
        result.push_back(std::move(correction));
    }
    return result;
}

void require_finite(double value, const std::string& field) {
    if (!std::isfinite(value)) {
        throw PerformanceTestError(field + " must be finite");
    }
}

void validate_measurement(
    const PerformanceMeasurement& measurement,
    const std::string& field,
    bool allow_zero = false) {
    require_finite(measurement.value_si, field);
    if ((!allow_zero && measurement.value_si <= 0.0) ||
        (allow_zero && measurement.value_si < 0.0)) {
        throw PerformanceTestError(
            field + (allow_zero ? " must be non-negative" :
                                  " must be positive"));
    }
    if (measurement.standard_uncertainty_si.has_value()) {
        require_finite(
            *measurement.standard_uncertainty_si,
            field + ".standard_uncertainty");
        if (*measurement.standard_uncertainty_si < 0.0) {
            throw PerformanceTestError(
                field + ".standard_uncertainty must be non-negative");
        }
    }
}

std::optional<double> rss(std::initializer_list<std::optional<double>> terms) {
    double sum = 0.0;
    for (const auto& term : terms) {
        if (!term.has_value()) return std::nullopt;
        sum += *term * *term;
    }
    return std::sqrt(sum);
}

std::optional<double> relative_uncertainty(
    const PerformanceMeasurement& value) {
    if (!value.standard_uncertainty_si.has_value()) return std::nullopt;
    return *value.standard_uncertainty_si / value.value_si;
}

DerivedPerformanceValue derived(
    double value,
    std::optional<double> standard_uncertainty) {
    return {
        value,
        standard_uncertainty,
        standard_uncertainty.has_value()
            ? std::optional<double>{2.0 * *standard_uncertainty}
            : std::nullopt,
    };
}

struct FactorProduct {
    double value{1.0};
    std::optional<double> relative_uncertainty{0.0};
};

FactorProduct factor_product(
    const std::vector<PerformanceCorrectionFactor>& corrections,
    const std::string& field) {
    FactorProduct product;
    double relative_variance = 0.0;
    for (const auto& correction : corrections) {
        if (correction.id.empty()) {
            throw PerformanceTestError(field + " correction id is required");
        }
        require_finite(correction.factor, field + "." + correction.id);
        if (correction.factor <= 0.0) {
            throw PerformanceTestError(
                field + "." + correction.id + " must be positive");
        }
        product.value *= correction.factor;
        if (correction.relative_standard_uncertainty.has_value()) {
            const double uncertainty =
                *correction.relative_standard_uncertainty;
            require_finite(
                uncertainty,
                field + "." + correction.id + ".uncertainty");
            if (uncertainty < 0.0) {
                throw PerformanceTestError(
                    field + "." + correction.id +
                    ".uncertainty must be non-negative");
            }
            relative_variance += uncertainty * uncertainty;
        } else {
            product.relative_uncertainty = std::nullopt;
        }
    }
    if (product.relative_uncertainty.has_value()) {
        product.relative_uncertainty = std::sqrt(relative_variance);
    }
    return product;
}

void json_string(std::ostream& out, std::string_view value) {
    out << '"';
    for (const unsigned char character : value) {
        switch (character) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << static_cast<char>(character);
        }
    }
    out << '"';
}

void optional_number(std::ostream& out, const std::optional<double>& value) {
    if (value.has_value()) out << std::setprecision(17) << *value;
    else out << "null";
}

}  // namespace

std::string to_string(StabilityStatistic statistic) {
    switch (statistic) {
        case StabilityStatistic::maximum_deviation_from_mean:
            return "maximum_deviation_from_mean";
        case StabilityStatistic::standard_deviation:
            return "standard_deviation";
        case StabilityStatistic::reported_compliant:
            return "reported_compliant";
    }
    throw PerformanceTestError("unknown stability statistic");
}

std::string to_string(ComplianceDisposition disposition) {
    switch (disposition) {
        case ComplianceDisposition::passed: return "passed";
        case ComplianceDisposition::failed: return "failed";
        case ComplianceDisposition::accepted_deviation:
            return "accepted_deviation";
        case ComplianceDisposition::not_demonstrated:
            return "not_demonstrated";
        case ComplianceDisposition::not_applicable:
            return "not_applicable";
    }
    throw PerformanceTestError("unknown compliance disposition");
}

std::string to_string(CorrectionEvidenceBasis basis) {
    switch (basis) {
        case CorrectionEvidenceBasis::independently_derived:
            return "independently_derived";
        case CorrectionEvidenceBasis::manufacturer_curve:
            return "manufacturer_curve";
        case CorrectionEvidenceBasis::reported_factor:
            return "reported_factor";
        case CorrectionEvidenceBasis::assumption:
            return "assumption";
    }
    throw PerformanceTestError("unknown correction evidence basis");
}

GasTurbinePerformanceTestRequest
parse_gas_turbine_performance_test_request_json(
    const std::string& text) {
    try {
        const auto root = Json::parse(text);
        if (!root.is_object()) {
            throw PerformanceTestError(
                "performance-test request root must be an object");
        }
        const std::string schema = root.value("schema_version", "");
        if (schema != gas_turbine_performance_test_schema_v1) {
            throw PerformanceTestError(
                "schema_version must be " +
                std::string(gas_turbine_performance_test_schema_v1));
        }
        GasTurbinePerformanceTestRequest request;
        request.id = root.at("id").get<std::string>();
        request.equipment_id = root.at("equipment_id").get<std::string>();
        request.standard_reference =
            root.value("standard_reference", "ISO 2314:2009");
        if (root.contains("evidence")) {
            const auto& evidence = root.at("evidence");
            request.evidence.test_boundary_defined =
                evidence.value("test_boundary_defined", false);
            request.evidence.reference_conditions_defined =
                evidence.value("reference_conditions_defined", false);
            request.evidence.raw_time_series_available =
                evidence.value("raw_time_series_available", false);
            request.evidence.instrument_calibrations_available =
                evidence.value(
                    "instrument_calibrations_available", false);
            request.evidence.contemporaneous_fuel_sample_available =
                evidence.value(
                    "contemporaneous_fuel_sample_available", false);
            request.evidence.correction_curves_available =
                evidence.value("correction_curves_available", false);
            request.evidence.correction_curve_range_confirmed =
                evidence.value(
                    "correction_curve_range_confirmed", false);
            request.evidence.uncertainty_analysis_available =
                evidence.value("uncertainty_analysis_available", false);
            request.evidence.deviations_documented_and_approved =
                evidence.value(
                    "deviations_documented_and_approved", false);
        }
        for (const auto& value : root.at("runs")) {
            GasTurbinePerformanceRun run;
            run.id = value.at("id").get<std::string>();
            run.duration_seconds =
                value.at("duration_seconds").get<double>();
            run.gross_generator_power_w = parse_measurement(
                value.at("gross_generator_power_w"),
                run.id + ".gross_generator_power_w");
            run.generator_losses_w = parse_measurement(
                value.at("generator_losses_w"),
                run.id + ".generator_losses_w");
            run.fuel_mass_flow_kg_s = parse_measurement(
                value.at("fuel_mass_flow_kg_s"),
                run.id + ".fuel_mass_flow_kg_s");
            run.fuel_lhv_j_kg = parse_measurement(
                value.at("fuel_lhv_j_kg"),
                run.id + ".fuel_lhv_j_kg");
            run.fuel_sensible_enthalpy_j_kg = value.contains(
                    "fuel_sensible_enthalpy_j_kg")
                ? parse_measurement(
                      value.at("fuel_sensible_enthalpy_j_kg"),
                      run.id + ".fuel_sensible_enthalpy_j_kg")
                : PerformanceMeasurement{0.0, std::nullopt};
            run.include_fuel_sensible_heat =
                value.value("include_fuel_sensible_heat", true);
            run.output_corrections =
                parse_corrections(value, "output_corrections");
            run.heat_rate_corrections =
                parse_corrections(value, "heat_rate_corrections");
            if (value.contains("stability")) {
                for (const auto& item : value.at("stability")) {
                    run.stability.push_back({
                        item.at("parameter").get<std::string>(),
                        item.at("observed_variation").get<double>(),
                        item.at("allowed_variation").get<double>(),
                        item.at("dimension").get<std::string>(),
                        stability_statistic_from_string(item.value(
                            "statistic",
                            "maximum_deviation_from_mean")),
                    });
                }
            }
            request.runs.push_back(std::move(run));
        }
        return request;
    } catch (const PerformanceTestError&) {
        throw;
    } catch (const std::exception& ex) {
        throw PerformanceTestError(
            std::string("invalid performance-test request: ") +
            ex.what());
    }
}

GasTurbinePerformanceTestResult
evaluate_gas_turbine_performance_test_json(
    const std::string& text) {
    return evaluate_gas_turbine_performance_test(
        parse_gas_turbine_performance_test_request_json(text));
}

GasTurbinePerformanceTestResult evaluate_gas_turbine_performance_test(
    const GasTurbinePerformanceTestRequest& request) {
    if (request.id.empty() || request.equipment_id.empty() ||
        request.standard_reference.empty() || request.runs.empty()) {
        throw PerformanceTestError(
            "campaign id, equipment id, standard reference, and runs "
            "are required");
    }

    GasTurbinePerformanceTestResult result;
    result.id = request.id;
    result.equipment_id = request.equipment_id;
    result.standard_reference = request.standard_reference;
    std::set<std::string> run_ids;
    double corrected_power_sum = 0.0;
    double corrected_heat_rate_sum = 0.0;

    for (const auto& run : request.runs) {
        if (run.id.empty() || !run_ids.insert(run.id).second) {
            throw PerformanceTestError(
                "run ids must be non-empty and unique");
        }
        require_finite(run.duration_seconds, run.id + ".duration");
        if (run.duration_seconds <= 0.0) {
            throw PerformanceTestError(run.id + ".duration must be positive");
        }
        validate_measurement(
            run.gross_generator_power_w, run.id + ".gross_power");
        validate_measurement(
            run.generator_losses_w, run.id + ".generator_losses", true);
        validate_measurement(
            run.fuel_mass_flow_kg_s, run.id + ".fuel_mass_flow");
        validate_measurement(run.fuel_lhv_j_kg, run.id + ".fuel_lhv");
        validate_measurement(
            run.fuel_sensible_enthalpy_j_kg,
            run.id + ".fuel_sensible_enthalpy", true);

        const double net_power =
            run.gross_generator_power_w.value_si -
            run.generator_losses_w.value_si;
        if (net_power <= 0.0) {
            throw PerformanceTestError(
                run.id + ".net generator power must be positive");
        }
        const double fuel_energy_per_mass =
            run.fuel_lhv_j_kg.value_si +
            (run.include_fuel_sensible_heat
                 ? run.fuel_sensible_enthalpy_j_kg.value_si
                 : 0.0);
        const double fuel_input =
            run.fuel_mass_flow_kg_s.value_si * fuel_energy_per_mass;
        const double efficiency = net_power / fuel_input;
        const double heat_rate = 3.6e6 / efficiency;

        const auto net_power_uncertainty = rss({
            run.gross_generator_power_w.standard_uncertainty_si,
            run.generator_losses_w.standard_uncertainty_si,
        });
        std::optional<double> fuel_specific_relative;
        if (run.fuel_lhv_j_kg.standard_uncertainty_si.has_value() &&
            (!run.include_fuel_sensible_heat ||
             run.fuel_sensible_enthalpy_j_kg.standard_uncertainty_si
                 .has_value())) {
            const double sensible_uncertainty =
                run.include_fuel_sensible_heat
                    ? *run.fuel_sensible_enthalpy_j_kg
                           .standard_uncertainty_si
                    : 0.0;
            fuel_specific_relative = std::sqrt(
                std::pow(*run.fuel_lhv_j_kg.standard_uncertainty_si, 2) +
                sensible_uncertainty * sensible_uncertainty) /
                fuel_energy_per_mass;
        }
        const auto fuel_input_relative = rss({
            relative_uncertainty(run.fuel_mass_flow_kg_s),
            fuel_specific_relative,
        });
        const std::optional<double> fuel_input_uncertainty =
            fuel_input_relative.has_value()
                ? std::optional<double>{fuel_input * *fuel_input_relative}
                : std::nullopt;
        const auto result_relative = rss({
            net_power_uncertainty.has_value()
                ? std::optional<double>{*net_power_uncertainty / net_power}
                : std::nullopt,
            fuel_input_relative,
        });
        const std::optional<double> efficiency_uncertainty =
            result_relative.has_value()
                ? std::optional<double>{efficiency * *result_relative}
                : std::nullopt;
        const std::optional<double> heat_rate_uncertainty =
            result_relative.has_value()
                ? std::optional<double>{heat_rate * *result_relative}
                : std::nullopt;

        const auto output_product = factor_product(
            run.output_corrections, run.id + ".output_corrections");
        const auto heat_rate_product = factor_product(
            run.heat_rate_corrections,
            run.id + ".heat_rate_corrections");
        const double corrected_power = net_power * output_product.value;
        const double corrected_heat_rate =
            heat_rate * heat_rate_product.value;
        const double corrected_efficiency = 3.6e6 / corrected_heat_rate;
        const auto corrected_power_relative = rss({
            net_power_uncertainty.has_value()
                ? std::optional<double>{*net_power_uncertainty / net_power}
                : std::nullopt,
            output_product.relative_uncertainty,
        });
        const auto corrected_heat_rate_relative = rss({
            result_relative,
            heat_rate_product.relative_uncertainty,
        });

        GasTurbinePerformanceRunResult run_result;
        run_result.id = run.id;
        run_result.net_generator_power_w =
            derived(net_power, net_power_uncertainty);
        run_result.fuel_thermal_input_w =
            derived(fuel_input, fuel_input_uncertainty);
        run_result.thermal_efficiency =
            derived(efficiency, efficiency_uncertainty);
        run_result.heat_rate_j_per_kwh =
            derived(heat_rate, heat_rate_uncertainty);
        run_result.output_correction_product = output_product.value;
        run_result.heat_rate_correction_product = heat_rate_product.value;
        run_result.corrected_net_generator_power_w = derived(
            corrected_power,
            corrected_power_relative.has_value()
                ? std::optional<double>{
                      corrected_power * *corrected_power_relative}
                : std::nullopt);
        run_result.corrected_heat_rate_j_per_kwh = derived(
            corrected_heat_rate,
            corrected_heat_rate_relative.has_value()
                ? std::optional<double>{
                      corrected_heat_rate *
                      *corrected_heat_rate_relative}
                : std::nullopt);
        run_result.corrected_thermal_efficiency = derived(
            corrected_efficiency,
            corrected_heat_rate_relative.has_value()
                ? std::optional<double>{
                      corrected_efficiency *
                      *corrected_heat_rate_relative}
                : std::nullopt);
        for (const auto& correction : run.output_corrections) {
            run_result.output_corrections.push_back({
                correction.id, correction.factor, correction.basis,
                correction.source_reference});
        }
        for (const auto& correction : run.heat_rate_corrections) {
            run_result.heat_rate_corrections.push_back({
                correction.id, correction.factor, correction.basis,
                correction.source_reference});
        }

        run_result.duration_disposition =
            run.duration_seconds >= iso_recommended_run_duration_s
                ? ComplianceDisposition::passed
                : ComplianceDisposition::not_demonstrated;
        bool stability_failed = false;
        bool stability_not_demonstrated = run.stability.empty();
        for (const auto& observation : run.stability) {
            require_finite(
                observation.observed_variation,
                run.id + ".stability.observed");
            require_finite(
                observation.allowed_variation,
                run.id + ".stability.allowed");
            if (observation.parameter.empty() ||
                observation.dimension.empty() ||
                observation.observed_variation < 0.0 ||
                observation.allowed_variation < 0.0) {
                throw PerformanceTestError(
                    run.id + " stability observation is invalid");
            }
            PerformanceStabilityResult stability_result{
                observation.parameter,
                observation.observed_variation,
                observation.allowed_variation,
                observation.dimension,
                observation.statistic,
                ComplianceDisposition::not_demonstrated,
                {},
            };
            if (observation.statistic ==
                StabilityStatistic::maximum_deviation_from_mean) {
                stability_result.disposition =
                    observation.observed_variation <=
                            observation.allowed_variation
                        ? ComplianceDisposition::passed
                        : ComplianceDisposition::failed;
                stability_result.note =
                    "direct ISO 2314 Table 9 maximum-deviation check";
            } else if (observation.statistic ==
                       StabilityStatistic::reported_compliant) {
                stability_result.disposition =
                    ComplianceDisposition::not_demonstrated;
                stability_result.note =
                    "report assertion is retained, but raw readings are "
                    "required for independent verification";
            } else {
                stability_result.disposition =
                    ComplianceDisposition::not_demonstrated;
                stability_result.note =
                    "standard deviation is not equivalent to ISO 2314's "
                    "maximum deviation from the reported mean";
            }
            stability_failed = stability_failed ||
                stability_result.disposition ==
                    ComplianceDisposition::failed;
            stability_not_demonstrated = stability_not_demonstrated ||
                stability_result.disposition ==
                    ComplianceDisposition::not_demonstrated;
            run_result.stability.push_back(std::move(stability_result));
        }
        run_result.stability_disposition = stability_failed
            ? ComplianceDisposition::failed
            : (stability_not_demonstrated
                   ? ComplianceDisposition::not_demonstrated
                   : ComplianceDisposition::passed);
        if (!net_power_uncertainty.has_value()) {
            run_result.uncertainty_limitations.push_back(
                "generator power/loss standard uncertainties are incomplete");
        }
        if (!fuel_input_uncertainty.has_value()) {
            run_result.uncertainty_limitations.push_back(
                "fuel-flow/LHV standard uncertainties are incomplete");
        }
        if (!output_product.relative_uncertainty.has_value() ||
            !heat_rate_product.relative_uncertainty.has_value()) {
            run_result.uncertainty_limitations.push_back(
                "correction-factor uncertainties are incomplete");
        }
        corrected_power_sum += corrected_power;
        corrected_heat_rate_sum += corrected_heat_rate;
        result.runs.push_back(std::move(run_result));
    }

    result.average_corrected_net_generator_power_w =
        corrected_power_sum / static_cast<double>(result.runs.size());
    result.average_corrected_heat_rate_j_per_kwh =
        corrected_heat_rate_sum / static_cast<double>(result.runs.size());
    result.average_corrected_thermal_efficiency =
        3.6e6 / result.average_corrected_heat_rate_j_per_kwh;

    const auto add_check = [&](
        std::string id,
        ComplianceDisposition disposition,
        std::string note) {
        result.compliance.push_back({
            std::move(id), disposition, request.standard_reference,
            std::move(note)});
    };
    add_check(
        "test_boundary",
        request.evidence.test_boundary_defined
            ? ComplianceDisposition::passed
            : ComplianceDisposition::not_demonstrated,
        "ISO 2314 Clauses 4 and 8.2 require the test boundary and all "
        "crossing process streams to be defined.");
    add_check(
        "reference_conditions",
        request.evidence.reference_conditions_defined
            ? ComplianceDisposition::passed
            : ComplianceDisposition::not_demonstrated,
        "ISO 2314 Clauses 7.1 and 8.2 require declared reference "
        "conditions.");
    const bool durations_pass = std::all_of(
        result.runs.begin(), result.runs.end(), [](const auto& run) {
            return run.duration_disposition == ComplianceDisposition::passed;
        });
    add_check(
        "run_duration",
        durations_pass ? ComplianceDisposition::passed
                       : ComplianceDisposition::not_demonstrated,
        "ISO 2314 Clause 7.5 recommends 30-minute runs.");
    const bool any_stability_failed = std::any_of(
        result.runs.begin(), result.runs.end(), [](const auto& run) {
            return run.stability_disposition ==
                ComplianceDisposition::failed;
        });
    const bool all_stability_pass = std::all_of(
        result.runs.begin(), result.runs.end(), [](const auto& run) {
            return run.stability_disposition ==
                ComplianceDisposition::passed;
        });
    add_check(
        "run_stability",
        any_stability_failed
            ? ComplianceDisposition::failed
            : (all_stability_pass
                   ? ComplianceDisposition::passed
                   : ComplianceDisposition::not_demonstrated),
        "ISO 2314 Clauses 7.6-7.8 require each raw reading to remain "
        "within Table 9 limits unless an agreed exception applies.");
    add_check(
        "raw_test_record",
        request.evidence.raw_time_series_available
            ? ComplianceDisposition::passed
            : ComplianceDisposition::not_demonstrated,
        "ISO 2314 Clause 7.7 requires the complete unmodified test "
        "record to be available to the parties.");
    add_check(
        "instrument_calibration",
        request.evidence.instrument_calibrations_available
            ? ComplianceDisposition::passed
            : ComplianceDisposition::not_demonstrated,
        "ISO 2314 Clause 6.4 requires calibrated instruments and "
        "traceable uncertainty evidence.");
    add_check(
        "fuel_sampling",
        request.evidence.contemporaneous_fuel_sample_available
            ? ComplianceDisposition::passed
            : (request.evidence.deviations_documented_and_approved
                   ? ComplianceDisposition::accepted_deviation
                   : ComplianceDisposition::not_demonstrated),
        "ISO 2314 Clauses 6.2-6.3 require the agreed fuel sampling and "
        "analysis procedure; deviations require agreement.");
    add_check(
        "correction_method",
        request.evidence.correction_curves_available &&
                request.evidence.correction_curve_range_confirmed
            ? ComplianceDisposition::passed
            : ComplianceDisposition::not_demonstrated,
        "ISO 2314 Clause 8.2 requires applicable correction curves or "
        "algorithms and their valid ranges.");
    add_check(
        "uncertainty_analysis",
        request.evidence.uncertainty_analysis_available
            ? ComplianceDisposition::passed
            : ComplianceDisposition::not_demonstrated,
        "ISO 2314 Annex A describes Type A, Type B, combined, and "
        "expanded uncertainty evaluation.");
    add_check(
        "deviation_record",
        request.evidence.deviations_documented_and_approved
            ? ComplianceDisposition::passed
            : ComplianceDisposition::not_applicable,
        "Documented deviations must be agreed by the test parties.");

    for (const auto& check : result.compliance) {
        switch (check.disposition) {
            case ComplianceDisposition::passed: ++result.passed_count; break;
            case ComplianceDisposition::accepted_deviation:
                ++result.accepted_deviation_count;
                break;
            case ComplianceDisposition::failed: ++result.failed_count; break;
            case ComplianceDisposition::not_demonstrated:
                ++result.not_demonstrated_count;
                break;
            case ComplianceDisposition::not_applicable: break;
        }
    }
    result.iso_conformity_demonstrated =
        result.failed_count == 0U && result.not_demonstrated_count == 0U;
    if (!request.evidence.correction_curves_available) {
        result.limitations.push_back(
            "correction factors can be reproduced but not independently "
            "derived without the manufacturer curves or algorithms");
    }
    if (!request.evidence.raw_time_series_available) {
        result.limitations.push_back(
            "reported stability statistics do not independently prove "
            "ISO 2314 Table 9 maximum-deviation compliance");
    }
    if (!request.evidence.uncertainty_analysis_available) {
        result.limitations.push_back(
            "a complete ISO 2314 Annex A uncertainty statement is not "
            "available");
    }
    return result;
}

std::string serialize_gas_turbine_performance_test_result_json(
    const GasTurbinePerformanceTestResult& result) {
    std::ostringstream out;
    out << "{\"schema_version\":";
    json_string(out, result.schema_version);
    out << ",\"id\":";
    json_string(out, result.id);
    out << ",\"equipment_id\":";
    json_string(out, result.equipment_id);
    out << ",\"standard_reference\":";
    json_string(out, result.standard_reference);
    out << ",\"iso_conformity_demonstrated\":"
        << (result.iso_conformity_demonstrated ? "true" : "false")
        << ",\"summary\":{\"average_corrected_net_generator_power_w\":"
        << std::setprecision(17)
        << result.average_corrected_net_generator_power_w
        << ",\"average_corrected_heat_rate_j_per_kwh\":"
        << result.average_corrected_heat_rate_j_per_kwh
        << ",\"average_corrected_thermal_efficiency\":"
        << result.average_corrected_thermal_efficiency
        << ",\"passed_count\":" << result.passed_count
        << ",\"accepted_deviation_count\":"
        << result.accepted_deviation_count
        << ",\"failed_count\":" << result.failed_count
        << ",\"not_demonstrated_count\":"
        << result.not_demonstrated_count << "},\"runs\":[";
    for (std::size_t i = 0; i < result.runs.size(); ++i) {
        const auto& run = result.runs[i];
        if (i != 0U) out << ',';
        out << "{\"id\":";
        json_string(out, run.id);
        const auto value_json = [&](
            std::string_view name,
            const DerivedPerformanceValue& value) {
            out << ",\"" << name << "\":{\"value_si\":"
                << std::setprecision(17) << value.value_si
                << ",\"standard_uncertainty_si\":";
            optional_number(out, value.standard_uncertainty_si);
            out << ",\"expanded_uncertainty_si_k2\":";
            optional_number(out, value.expanded_uncertainty_si_k2);
            out << '}';
        };
        value_json("net_generator_power", run.net_generator_power_w);
        value_json("fuel_thermal_input", run.fuel_thermal_input_w);
        value_json("thermal_efficiency", run.thermal_efficiency);
        value_json("heat_rate", run.heat_rate_j_per_kwh);
        out << ",\"output_correction_product\":"
            << run.output_correction_product
            << ",\"heat_rate_correction_product\":"
            << run.heat_rate_correction_product;
        value_json(
            "corrected_net_generator_power",
            run.corrected_net_generator_power_w);
        value_json(
            "corrected_thermal_efficiency",
            run.corrected_thermal_efficiency);
        value_json(
            "corrected_heat_rate", run.corrected_heat_rate_j_per_kwh);
        const auto corrections_json = [&](
            std::string_view name,
            const std::vector<PerformanceCorrectionResult>& corrections) {
            out << ",\"" << name << "\":[";
            for (std::size_t index = 0; index < corrections.size();
                 ++index) {
                if (index != 0U) out << ',';
                const auto& correction = corrections[index];
                out << "{\"id\":";
                json_string(out, correction.id);
                out << ",\"factor\":" << correction.factor
                    << ",\"basis\":";
                json_string(out, to_string(correction.basis));
                out << ",\"source_reference\":";
                json_string(out, correction.source_reference);
                out << '}';
            }
            out << ']';
        };
        corrections_json("output_corrections", run.output_corrections);
        corrections_json(
            "heat_rate_corrections", run.heat_rate_corrections);
        out << ",\"stability\":[";
        for (std::size_t index = 0; index < run.stability.size();
             ++index) {
            if (index != 0U) out << ',';
            const auto& stability = run.stability[index];
            out << "{\"parameter\":";
            json_string(out, stability.parameter);
            out << ",\"observed_variation\":"
                << stability.observed_variation
                << ",\"allowed_variation\":"
                << stability.allowed_variation << ",\"dimension\":";
            json_string(out, stability.dimension);
            out << ",\"statistic\":";
            json_string(out, to_string(stability.statistic));
            out << ",\"disposition\":";
            json_string(out, to_string(stability.disposition));
            out << ",\"note\":";
            json_string(out, stability.note);
            out << '}';
        }
        out << "],\"uncertainty_limitations\":[";
        for (std::size_t index = 0;
             index < run.uncertainty_limitations.size(); ++index) {
            if (index != 0U) out << ',';
            json_string(out, run.uncertainty_limitations[index]);
        }
        out << ']';
        out << ",\"duration_disposition\":";
        json_string(out, to_string(run.duration_disposition));
        out << ",\"stability_disposition\":";
        json_string(out, to_string(run.stability_disposition));
        out << '}';
    }
    out << "],\"compliance\":[";
    for (std::size_t i = 0; i < result.compliance.size(); ++i) {
        const auto& check = result.compliance[i];
        if (i != 0U) out << ',';
        out << "{\"id\":";
        json_string(out, check.id);
        out << ",\"disposition\":";
        json_string(out, to_string(check.disposition));
        out << ",\"standard_reference\":";
        json_string(out, check.standard_reference);
        out << ",\"note\":";
        json_string(out, check.note);
        out << '}';
    }
    out << "],\"limitations\":[";
    for (std::size_t i = 0; i < result.limitations.size(); ++i) {
        if (i != 0U) out << ',';
        json_string(out, result.limitations[i]);
    }
    out << "]}";
    return out.str();
}

}  // namespace thermox::service

#include "thermox/service/balance_report.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thermox::service {
namespace {

using Json = nlohmann::json;

struct Endpoint {
    std::string component;
    std::string port;
};

Endpoint endpoint(const std::string& value) {
    const auto separator = value.rfind('.');
    if (separator == std::string::npos) return {value, {}};
    return {value.substr(0, separator), value.substr(separator + 1)};
}

double named_value(
    const Json& values, const std::string& name,
    double fallback = 0.0) {
    if (!values.is_array()) return fallback;
    for (const auto& value : values) {
        if (value.value("name", std::string{}) == name) {
            return value.value("value_si", fallback);
        }
    }
    return fallback;
}

double material_mass_flow(const Json& port) {
    const double total = named_value(
        port.value("derived_values", Json::array()), "m_dot_total",
        std::numeric_limits<double>::quiet_NaN());
    if (std::isfinite(total)) return total;
    double result = 0.0;
    for (const auto& value :
         port.value("primary_values", Json::array())) {
        const auto name = value.value("name", std::string{});
        if (name.starts_with("m_dot[")) {
            result += value.value("value_si", 0.0);
        }
    }
    return result;
}

std::pair<double, double> port_flows(const Json& port) {
    const auto domain = port.value("domain", std::string{});
    const auto primary = port.value("primary_values", Json::array());
    if (domain == "fluid") {
        const double mass = named_value(primary, "m_dot");
        return {mass, mass * named_value(primary, "h")};
    }
    if (domain == "material") {
        const double mass = material_mass_flow(port);
        return {mass, mass * named_value(primary, "h")};
    }
    if (domain == "heat") return {0.0, named_value(primary, "Q_dot")};
    if (domain == "shaft") return {0.0, named_value(primary, "W_dot")};
    if (domain == "electrical") return {0.0, named_value(primary, "P")};
    return {0.0, 0.0};
}

const Json& selected_graph(const Json& result, double& sample_time) {
    if (result.contains("graph")) return result.at("graph");
    const auto& trajectory = result.at("trajectory");
    if (!trajectory.is_array() || trajectory.empty()) {
        throw std::invalid_argument(
            "transient result has no graph samples for balance reporting");
    }
    const auto& sample = trajectory.back();
    sample_time = sample.value("time", 0.0);
    return sample.at("graph");
}

std::string csv_field(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string escaped{"\""};
    for (const char character : value) {
        if (character == '"') escaped.push_back('"');
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

std::string markdown_field(std::string value) {
    for (char& character : value) {
        if (character == '|' || character == '\r' || character == '\n') {
            character = ' ';
        }
    }
    return value;
}

std::string number(double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}

void require_fields(
    const Json& value,
    const std::set<std::string>& allowed,
    const std::string& context) {
    if (!value.is_object()) {
        throw std::invalid_argument(context + " must be an object");
    }
    for (const auto& [name, ignored] : value.items()) {
        (void)ignored;
        if (!allowed.contains(name)) {
            throw std::invalid_argument(
                context + " contains unknown field: " + name);
        }
    }
}

std::optional<double> optional_uncertainty(
    const Json& value,
    const std::string& name,
    const std::string& context) {
    if (!value.contains(name) || value.at(name).is_null()) return std::nullopt;
    if (!value.at(name).is_number()) {
        throw std::invalid_argument(context + "." + name + " must be numeric");
    }
    const double result = value.at(name).get<double>();
    if (!std::isfinite(result) || result < 0.0) {
        throw std::invalid_argument(
            context + "." + name + " must be finite and non-negative");
    }
    return result;
}

bool valid_sha256(const std::string& value) {
    return value.size() == 64U &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f');
        });
}

std::string stream_key(
    const std::string& component_id,
    const std::string& port_name) {
    return component_id + '\x1f' + port_name;
}

void validate_uncertainty_model(const BalanceUncertaintyModel& model) {
    if (model.schema_version != balance_uncertainty_schema_v1 ||
        model.id.empty() || model.source_reference.empty() ||
        !valid_sha256(model.source_checksum_sha256) ||
        model.streams.empty() || model.streams.size() > 512U ||
        model.correlations.size() > 2048U) {
        throw std::invalid_argument(
            "balance uncertainty model requires schema v1, identity, "
            "source reference/checksum, and 1..512 stream declarations");
    }
    std::set<std::string> limitation_ids;
    for (const auto& limitation : model.limitations) {
        if (limitation.empty() || !limitation_ids.insert(limitation).second) {
            throw std::invalid_argument(
                "balance uncertainty limitations must be non-empty and unique");
        }
    }
    std::set<std::string> stream_ids;
    for (const auto& stream : model.streams) {
        const auto id = stream_key(stream.component_id, stream.port_name);
        if (stream.component_id.empty() || stream.port_name.empty() ||
            !stream_ids.insert(id).second ||
            (!stream.mass_flow_standard_uncertainty_si &&
             !stream.specific_enthalpy_standard_uncertainty_si &&
             !stream.energy_flow_standard_uncertainty_si) ||
            !std::isfinite(stream.mass_enthalpy_correlation) ||
            std::abs(stream.mass_enthalpy_correlation) > 1.0) {
            throw std::invalid_argument(
                "balance uncertainty streams require unique endpoints, at "
                "least one uncertainty, and valid correlations");
        }
        if (stream.specific_enthalpy_standard_uncertainty_si &&
            !stream.mass_flow_standard_uncertainty_si) {
            throw std::invalid_argument(
                "specific-enthalpy propagation requires mass-flow uncertainty");
        }
        if (stream.energy_flow_standard_uncertainty_si &&
            stream.specific_enthalpy_standard_uncertainty_si) {
            throw std::invalid_argument(
                "declare direct energy-flow uncertainty or mass/enthalpy "
                "uncertainties, not both");
        }
    }
    std::set<std::string> correlation_ids;
    for (const auto& correlation : model.correlations) {
        const auto first = stream_key(
            correlation.first_component_id, correlation.first_port_name);
        const auto second = stream_key(
            correlation.second_component_id, correlation.second_port_name);
        const auto low = std::min(first, second);
        const auto high = std::max(first, second);
        const auto id = correlation.quantity + '\x1f' + low + '\x1f' + high;
        if ((correlation.quantity != "mass_flow" &&
             correlation.quantity != "energy_flow") ||
            first == second || !stream_ids.contains(first) ||
            !stream_ids.contains(second) ||
            !std::isfinite(correlation.coefficient) ||
            std::abs(correlation.coefficient) > 1.0 ||
            !correlation_ids.insert(id).second) {
            throw std::invalid_argument(
                "balance uncertainty correlations require unique declared "
                "endpoint pairs, one quantity, and coefficients in [-1,1]");
        }
    }
}

Json closure_acceptance(
    const SimulationJobRecord& job,
    bool transient) {
    Json criteria = Json::array();
    bool energy_declared = false;
    bool mass_declared = false;
    bool declared_passed = true;
    if (!transient && job.result_summary &&
        job.result_summary->engineering_acceptance) {
        for (const auto& result :
             job.result_summary->engineering_acceptance->criteria) {
            const auto projection = std::find_if(
                job.request.result_projections.begin(),
                job.request.result_projections.end(),
                [&](const ResultProjection& candidate) {
                    return candidate.id == result.projection_id;
                });
            if (projection == job.request.result_projections.end() ||
                projection->scope != ResultValueScope::system_balance ||
                (projection->value_name != "net_boundary_energy_flow" &&
                 projection->value_name != "net_boundary_mass_flow")) {
                continue;
            }
            const bool energy =
                projection->value_name == "net_boundary_energy_flow";
            energy_declared = energy_declared || energy;
            mass_declared = mass_declared || !energy;
            declared_passed = declared_passed && result.passed;
            criteria.push_back({
                {"criterion_id", result.criterion_id},
                {"projection_id", result.projection_id},
                {"balance", energy ? "energy" : "mass"},
                {"dimension", result.dimension},
                {"actual_value_si", result.actual_value_si},
                {"lower_bound_si", result.lower_bound_si
                    ? Json(*result.lower_bound_si) : Json(nullptr)},
                {"upper_bound_si", result.upper_bound_si
                    ? Json(*result.upper_bound_si) : Json(nullptr)},
                {"passed", result.passed},
            });
        }
    }

    std::string status;
    std::string interpretation;
    if (transient) {
        status = "not_evaluated";
        interpretation =
            "transient closure requires stored mass and energy accumulation";
    } else if (!energy_declared && !mass_declared) {
        status = "not_declared";
        interpretation =
            "no run-configuration acceptance criteria target system closure";
    } else if (!energy_declared || !mass_declared) {
        status = declared_passed ? "partial_pass" : "partial_fail";
        interpretation =
            "only declared closure dimensions were evaluated";
    } else {
        status = declared_passed ? "passed" : "failed";
        interpretation =
            "mass and energy closure criteria were evaluated";
    }
    return {
        {"source", "run_configuration_engineering_acceptance"},
        {"status", status},
        {"complete", energy_declared && mass_declared && !transient},
        {"energy_declared", energy_declared},
        {"mass_declared", mass_declared},
        {"interpretation", interpretation},
        {"criteria", std::move(criteria)},
    };
}

struct UncertainBoundaryTerm {
    std::string key;
    double standard_uncertainty{0.0};
    double orientation{0.0};
};

double combined_standard_uncertainty(
    const std::vector<UncertainBoundaryTerm>& terms,
    const BalanceUncertaintyModel& model,
    const std::string& quantity) {
    if (terms.empty()) return 0.0;
    std::map<std::string, std::size_t> indices;
    for (std::size_t index = 0; index < terms.size(); ++index) {
        indices.emplace(terms[index].key, index);
    }
    std::vector<std::vector<double>> correlation(
        terms.size(), std::vector<double>(terms.size(), 0.0));
    for (std::size_t index = 0; index < terms.size(); ++index) {
        correlation[index][index] = 1.0;
    }
    for (const auto& declared : model.correlations) {
        if (declared.quantity != quantity) continue;
        const auto first = indices.find(stream_key(
            declared.first_component_id, declared.first_port_name));
        const auto second = indices.find(stream_key(
            declared.second_component_id, declared.second_port_name));
        if (first == indices.end() || second == indices.end()) {
            throw std::invalid_argument(
                "balance uncertainty correlation references an endpoint "
                "without declared " + quantity + " uncertainty");
        }
        correlation[first->second][second->second] = declared.coefficient;
        correlation[second->second][first->second] = declared.coefficient;
    }

    // Validate that the declared correlation matrix is positive
    // semidefinite before using it. This Cholesky-like check permits exact
    // singularity while rejecting inconsistent pairwise declarations.
    std::vector<std::vector<double>> factor(
        terms.size(), std::vector<double>(terms.size(), 0.0));
    constexpr double tolerance = 1.0e-12;
    for (std::size_t row = 0; row < terms.size(); ++row) {
        for (std::size_t column = 0; column <= row; ++column) {
            double residual = correlation[row][column];
            for (std::size_t inner = 0; inner < column; ++inner) {
                residual -= factor[row][inner] * factor[column][inner];
            }
            if (row == column) {
                if (residual < -tolerance) {
                    throw std::invalid_argument(
                        "balance uncertainty correlation matrix is not "
                        "positive semidefinite");
                }
                factor[row][column] =
                    residual > tolerance ? std::sqrt(residual) : 0.0;
            } else if (factor[column][column] > tolerance) {
                factor[row][column] = residual / factor[column][column];
            } else if (std::abs(residual) > tolerance) {
                throw std::invalid_argument(
                    "balance uncertainty correlation matrix is singular "
                    "and inconsistent");
            }
        }
    }

    double variance = 0.0;
    for (std::size_t first = 0; first < terms.size(); ++first) {
        for (std::size_t second = 0; second < terms.size(); ++second) {
            variance += terms[first].orientation * terms[second].orientation *
                correlation[first][second] *
                terms[first].standard_uncertainty *
                terms[second].standard_uncertainty;
        }
    }
    if (variance < -tolerance) {
        throw std::invalid_argument(
            "balance uncertainty propagation produced negative variance");
    }
    return std::sqrt(std::max(0.0, variance));
}

Json boundary_uncertainty(
    const std::optional<BalanceUncertaintyModel>& declared_model,
    const std::string& artifact_revision_id,
    const Json& streams,
    double mass_reference,
    double energy_reference,
    bool transient) {
    if (!declared_model) {
        const bool mass_applicable = std::any_of(
            streams.begin(), streams.end(), [](const Json& stream) {
                const auto domain = stream.at("domain").get<std::string>();
                return domain == "fluid" || domain == "material";
            });
        return {
            {"status", "not_declared"},
            {"source", nullptr},
            {"mass_applicable", mass_applicable},
            {"mass", nullptr},
            {"energy", nullptr},
            {"stream_contributions", Json::array()},
            {"correlation_count", 0U},
            {"interpretation",
             "no source-traceable boundary uncertainty model was declared"},
            {"limitations", Json::array({
                "measurement uncertainty propagation was not evaluated"})},
        };
    }
    const auto& model = *declared_model;
    validate_uncertainty_model(model);
    std::map<std::string, const BoundaryStreamUncertainty*> declarations;
    for (const auto& declaration : model.streams) {
        declarations.emplace(
            stream_key(declaration.component_id, declaration.port_name),
            &declaration);
    }
    std::set<std::string> matched;
    std::vector<UncertainBoundaryTerm> mass_terms;
    std::vector<UncertainBoundaryTerm> energy_terms;
    Json contributions = Json::array();
    std::size_t required_mass = 0U;
    std::size_t covered_mass = 0U;
    std::size_t covered_energy = 0U;
    for (const auto& stream : streams) {
        const auto key = stream_key(
            stream.at("component_id").get<std::string>(),
            stream.at("port_name").get<std::string>());
        const auto found = declarations.find(key);
        const auto domain = stream.at("domain").get<std::string>();
        const bool carries_mass = domain == "fluid" || domain == "material";
        if (carries_mass) ++required_mass;
        Json contribution = {
            {"component_id", stream.at("component_id")},
            {"port_name", stream.at("port_name")},
            {"boundary_direction", stream.at("boundary_direction")},
            {"mass_flow_standard_uncertainty_si", nullptr},
            {"energy_flow_standard_uncertainty_si", nullptr},
            {"energy_uncertainty_method", "missing"},
        };
        if (found == declarations.end()) {
            contributions.push_back(std::move(contribution));
            continue;
        }
        matched.insert(key);
        const auto& declaration = *found->second;
        const double orientation =
            stream.at("boundary_direction") == "input" ? 1.0 : -1.0;
        if (!carries_mass &&
            (declaration.mass_flow_standard_uncertainty_si ||
             declaration.specific_enthalpy_standard_uncertainty_si)) {
            throw std::invalid_argument(
                "mass/enthalpy uncertainty was declared for a non-material "
                "boundary port");
        }
        if (carries_mass &&
            declaration.mass_flow_standard_uncertainty_si) {
            const double uncertainty =
                *declaration.mass_flow_standard_uncertainty_si;
            mass_terms.push_back({key, uncertainty, orientation});
            contribution["mass_flow_standard_uncertainty_si"] = uncertainty;
            ++covered_mass;
        }
        std::optional<double> energy_uncertainty =
            declaration.energy_flow_standard_uncertainty_si;
        std::string energy_method = energy_uncertainty
            ? "direct_energy_flow" : "missing";
        if (!energy_uncertainty &&
            declaration.mass_flow_standard_uncertainty_si &&
            declaration.specific_enthalpy_standard_uncertainty_si) {
            if (stream.at("specific_enthalpy_si").is_null()) {
                throw std::invalid_argument(
                    "mass/enthalpy uncertainty propagation requires a "
                    "specific-enthalpy boundary value");
            }
            const double mass = stream.at("mass_flow_si").get<double>();
            const double enthalpy =
                stream.at("specific_enthalpy_si").get<double>();
            const double mass_u =
                *declaration.mass_flow_standard_uncertainty_si;
            const double enthalpy_u =
                *declaration.specific_enthalpy_standard_uncertainty_si;
            double variance = enthalpy * enthalpy * mass_u * mass_u +
                mass * mass * enthalpy_u * enthalpy_u +
                2.0 * mass * enthalpy *
                    declaration.mass_enthalpy_correlation * mass_u * enthalpy_u;
            if (variance < -1.0e-12) {
                throw std::invalid_argument(
                    "mass/enthalpy uncertainty propagation produced negative "
                    "variance");
            }
            energy_uncertainty = std::sqrt(std::max(0.0, variance));
            energy_method = "mass_times_specific_enthalpy_first_order";
        }
        if (energy_uncertainty) {
            energy_terms.push_back({key, *energy_uncertainty, orientation});
            contribution["energy_flow_standard_uncertainty_si"] =
                *energy_uncertainty;
            contribution["energy_uncertainty_method"] = energy_method;
            ++covered_energy;
        }
        contributions.push_back(std::move(contribution));
    }
    if (matched.size() != declarations.size()) {
        throw std::invalid_argument(
            "balance uncertainty model declares an endpoint outside the "
            "calculated system boundary");
    }
    const bool mass_applicable = required_mass > 0U;
    const bool mass_complete =
        !mass_applicable || covered_mass == required_mass;
    const bool energy_complete = covered_energy == streams.size();
    const double mass_u = combined_standard_uncertainty(
        mass_terms, model, "mass_flow");
    const double energy_u = combined_standard_uncertainty(
        energy_terms, model, "energy_flow");
    const bool any_coverage = covered_mass > 0U || covered_energy > 0U;
    const std::string status = mass_complete && energy_complete
        ? "complete" : (any_coverage ? "partial" : "not_evaluated");
    Json limitations = model.limitations;
    if (!mass_complete) {
        limitations.push_back("mass-flow uncertainty coverage is incomplete");
    }
    if (!energy_complete) {
        limitations.push_back("energy-flow uncertainty coverage is incomplete");
    }
    if (transient) {
        limitations.push_back(
            "uncertainty applies to net boundary rate, not stored accumulation");
    }
    const auto quantity = [](bool complete, double uncertainty,
                             double reference) -> Json {
        if (!complete) return nullptr;
        return {
            {"standard_uncertainty_si", uncertainty},
            {"expanded_uncertainty_si_k2", 2.0 * uncertainty},
            {"reference_si", reference},
            {"relative_standard_uncertainty",
             reference > 0.0 ? Json(uncertainty / reference) : Json(nullptr)},
        };
    };
    return {
        {"status", status},
        {"source", {
            {"model_id", model.id},
            {"artifact_revision_id", artifact_revision_id},
            {"reference", model.source_reference},
            {"checksum_sha256", model.source_checksum_sha256},
            {"note", model.note},
        }},
        {"mass_applicable", mass_applicable},
        {"mass", quantity(
            mass_applicable && mass_complete, mass_u, mass_reference)},
        {"energy", quantity(energy_complete, energy_u, energy_reference)},
        {"stream_contributions", std::move(contributions)},
        {"correlation_count", model.correlations.size()},
        {"interpretation",
         transient
             ? "first-order uncertainty of the instantaneous net boundary rate"
             : "first-order uncertainty of the steady boundary closure residual"},
        {"limitations", std::move(limitations)},
    };
}

}  // namespace

BalanceUncertaintyModel parse_balance_uncertainty_model_json(
    std::string_view model_json) {
    const Json root = Json::parse(model_json);
    require_fields(
        root,
        {"schema_version", "id", "source", "streams", "correlations"},
        "balance uncertainty model");
    BalanceUncertaintyModel model;
    model.schema_version = root.at("schema_version").get<std::string>();
    model.id = root.at("id").get<std::string>();
    const auto& source = root.at("source");
    require_fields(
        source, {"reference", "checksum_sha256", "note", "limitations"},
        "balance uncertainty source");
    model.source_reference = source.at("reference").get<std::string>();
    model.source_checksum_sha256 =
        source.at("checksum_sha256").get<std::string>();
    model.note = source.value("note", std::string{});
    if (source.contains("limitations")) {
        if (!source.at("limitations").is_array()) {
            throw std::invalid_argument(
                "balance uncertainty source limitations must be an array");
        }
        model.limitations =
            source.at("limitations").get<std::vector<std::string>>();
    }
    if (!root.at("streams").is_array()) {
        throw std::invalid_argument(
            "balance uncertainty streams must be an array");
    }
    for (const auto& encoded : root.at("streams")) {
        require_fields(
            encoded,
            {"component_id", "port_name",
             "mass_flow_standard_uncertainty_si",
             "specific_enthalpy_standard_uncertainty_si",
             "energy_flow_standard_uncertainty_si",
             "mass_enthalpy_correlation"},
            "balance uncertainty stream");
        BoundaryStreamUncertainty stream;
        stream.component_id = encoded.at("component_id").get<std::string>();
        stream.port_name = encoded.at("port_name").get<std::string>();
        stream.mass_flow_standard_uncertainty_si = optional_uncertainty(
            encoded, "mass_flow_standard_uncertainty_si",
            "balance uncertainty stream");
        stream.specific_enthalpy_standard_uncertainty_si =
            optional_uncertainty(
                encoded, "specific_enthalpy_standard_uncertainty_si",
                "balance uncertainty stream");
        stream.energy_flow_standard_uncertainty_si = optional_uncertainty(
            encoded, "energy_flow_standard_uncertainty_si",
            "balance uncertainty stream");
        stream.mass_enthalpy_correlation =
            encoded.value("mass_enthalpy_correlation", 0.0);
        model.streams.push_back(std::move(stream));
    }
    if (root.contains("correlations")) {
        if (!root.at("correlations").is_array()) {
            throw std::invalid_argument(
                "balance uncertainty correlations must be an array");
        }
        for (const auto& encoded : root.at("correlations")) {
            require_fields(
                encoded, {"quantity", "first", "second", "coefficient"},
                "balance uncertainty correlation");
            const auto& first = encoded.at("first");
            const auto& second = encoded.at("second");
            require_fields(
                first, {"component_id", "port_name"},
                "balance uncertainty correlation first endpoint");
            require_fields(
                second, {"component_id", "port_name"},
                "balance uncertainty correlation second endpoint");
            model.correlations.push_back({
                encoded.at("quantity").get<std::string>(),
                first.at("component_id").get<std::string>(),
                first.at("port_name").get<std::string>(),
                second.at("component_id").get<std::string>(),
                second.at("port_name").get<std::string>(),
                encoded.at("coefficient").get<double>(),
            });
        }
    }
    validate_uncertainty_model(model);
    return model;
}

std::string serialize_balance_uncertainty_model_json(
    const BalanceUncertaintyModel& model) {
    validate_uncertainty_model(model);
    Json streams = Json::array();
    for (const auto& stream : model.streams) {
        streams.push_back({
            {"component_id", stream.component_id},
            {"port_name", stream.port_name},
            {"mass_flow_standard_uncertainty_si",
             stream.mass_flow_standard_uncertainty_si
                 ? Json(*stream.mass_flow_standard_uncertainty_si)
                 : Json(nullptr)},
            {"specific_enthalpy_standard_uncertainty_si",
             stream.specific_enthalpy_standard_uncertainty_si
                 ? Json(*stream.specific_enthalpy_standard_uncertainty_si)
                 : Json(nullptr)},
            {"energy_flow_standard_uncertainty_si",
             stream.energy_flow_standard_uncertainty_si
                 ? Json(*stream.energy_flow_standard_uncertainty_si)
                 : Json(nullptr)},
            {"mass_enthalpy_correlation",
             stream.mass_enthalpy_correlation},
        });
    }
    Json correlations = Json::array();
    for (const auto& correlation : model.correlations) {
        correlations.push_back({
            {"quantity", correlation.quantity},
            {"first", {
                {"component_id", correlation.first_component_id},
                {"port_name", correlation.first_port_name},
            }},
            {"second", {
                {"component_id", correlation.second_component_id},
                {"port_name", correlation.second_port_name},
            }},
            {"coefficient", correlation.coefficient},
        });
    }
    return Json({
        {"schema_version", model.schema_version},
        {"id", model.id},
        {"source", {
            {"reference", model.source_reference},
            {"checksum_sha256", model.source_checksum_sha256},
            {"note", model.note},
            {"limitations", model.limitations},
        }},
        {"streams", std::move(streams)},
        {"correlations", std::move(correlations)},
    }).dump();
}

std::string build_balance_report_json(
    const SimulationJobRecord& job,
    const ResultArtifact& result,
    const std::string& canonical_topology_json,
    const BalanceReportRequest& request) {
    if (request.schema_version != balance_report_request_schema_v2 ||
        request.accounting_basis != "energy" ||
        request.system_boundary != "whole_system" ||
        request.diagram_profile != "iso-14084-1:2015" ||
        request.calculation_profile != "none") {
        throw std::invalid_argument(
            "balance report supports request schema v2 whole-system energy accounting "
            "with diagram profile iso-14084-1:2015 and calculation profile none");
    }
    const Json result_json = Json::parse(result.content);
    const Json topology = Json::parse(canonical_topology_json);
    double sample_time = 0.0;
    const Json& graph = selected_graph(result_json, sample_time);
    const bool transient = result_json.contains("trajectory");

    std::map<std::pair<std::string, std::string>, std::string> directions;
    for (const auto& connection :
         topology.at("model").value("connections", Json::array())) {
        const auto from = endpoint(connection.at("from").get<std::string>());
        const auto to = endpoint(connection.at("to").get<std::string>());
        directions[{from.component, from.port}] = "out";
        directions[{to.component, to.port}] = "in";
    }

    Json components = Json::array();
    Json streams = Json::array();
    double boundary_mass_input = 0.0;
    double boundary_mass_output = 0.0;
    double boundary_input = 0.0;
    double boundary_output = 0.0;
    for (const auto& component : graph.value("components", Json::array())) {
        const auto id = component.at("component_id").get<std::string>();
        double net_mass = 0.0;
        double net_energy = 0.0;
        std::set<std::string> component_directions;
        std::vector<Json> observed_ports;
        for (const auto& port : component.value("ports", Json::array())) {
            const auto name = port.at("port_name").get<std::string>();
            const auto domain = port.value("domain", std::string{});
            const auto found = directions.find({id, name});
            if (found == directions.end()) continue;
            const auto [mass, energy] = port_flows(port);
            const double orientation = found->second == "in" ? 1.0 : -1.0;
            net_mass += orientation * mass;
            net_energy += orientation * energy;
            component_directions.insert(found->second);
            observed_ports.push_back({
                {"port_name", name},
                {"domain", domain},
                {"direction", found->second},
                {"mass_flow_si", mass},
                {"energy_flow_si", energy},
                {"specific_enthalpy_si",
                 domain == "fluid" || domain == "material"
                     ? Json(named_value(
                           port.value("primary_values", Json::array()), "h"))
                     : Json(nullptr)},
            });
        }
        components.push_back({
            {"component_id", id},
            {"kind", component.value("kind", std::string{})},
            {"net_mass_flow_si", net_mass},
            {"net_energy_flow_si", net_energy},
        });
        if (component_directions.size() == 1U) {
            const auto direction = *component_directions.begin();
            for (auto& port : observed_ports) {
                port["component_id"] = id;
                port["boundary_direction"] =
                    direction == "out" ? "input" : "output";
                const double magnitude =
                    std::abs(port.at("energy_flow_si").get<double>());
                const double mass_magnitude =
                    std::abs(port.at("mass_flow_si").get<double>());
                if (direction == "out") {
                    boundary_input += magnitude;
                    boundary_mass_input += mass_magnitude;
                } else {
                    boundary_output += magnitude;
                    boundary_mass_output += mass_magnitude;
                }
                streams.push_back(std::move(port));
            }
        }
    }

    double reported_energy_balance = boundary_input - boundary_output;
    double reported_mass_balance = 0.0;
    for (const auto& value : graph.value("system_balances", Json::array())) {
        const auto name = value.value("name", std::string{});
        if (name == "net_boundary_energy_flow") {
            reported_energy_balance = value.value("value_si", 0.0);
        } else if (name == "net_boundary_mass_flow") {
            reported_mass_balance = value.value("value_si", 0.0);
        }
    }
    const double energy_reference =
        std::max(boundary_input, boundary_output);
    const double mass_reference =
        std::max(boundary_mass_input, boundary_mass_output);
    const Json relative_energy_closure = energy_reference > 0.0
        ? Json(std::abs(reported_energy_balance) / energy_reference)
        : Json(nullptr);
    const Json relative_mass_closure = mass_reference > 0.0
        ? Json(std::abs(reported_mass_balance) / mass_reference)
        : Json(nullptr);
    Json uncertainty = boundary_uncertainty(
        request.uncertainty_model,
        request.uncertainty_artifact_revision_id,
        streams, mass_reference,
        energy_reference, transient);
    Json evaluated_requirements = Json::array({
        "exact immutable result provenance",
        "whole-system mass and energy boundary accounting",
        "component mass and energy closure",
        "normalized whole-system closure metrics",
        "run-configuration closure acceptance linkage when declared",
    });
    Json unevaluated_requirements = Json::array({
        "clause-level standards conformity",
        "exergy accounting",
        "transient stored-energy accumulation",
    });
    if (uncertainty.at("status") == "complete") {
        evaluated_requirements.push_back(
            "first-order boundary measurement uncertainty propagation");
    } else {
        unevaluated_requirements.push_back(
            "complete measurement uncertainty propagation");
    }

    Json report = {
        {"schema_version", balance_report_schema_v3},
        {"job_id", job.job_id},
        {"result_checksum", result.manifest.checksum},
        {"provenance", {
            {"project_id", job.request.source_revisions
                ? job.request.source_revisions->project_id : ""},
            {"model_revision_id", job.request.source_revisions
                ? job.request.source_revisions->model_revision_id : ""},
            {"model_checksum", job.request.source_revisions
                ? job.request.source_revisions->model_checksum : ""},
            {"case_revision_id", job.request.source_revisions
                ? job.request.source_revisions->case_revision_id : ""},
            {"case_checksum", job.request.source_revisions
                ? job.request.source_revisions->case_checksum : ""},
            {"run_configuration_revision_id", job.request.source_revisions
                ? job.request.source_revisions->run_configuration_revision_id
                : ""},
            {"run_configuration_checksum", job.request.source_revisions
                ? job.request.source_revisions->run_configuration_checksum
                : ""},
        }},
        {"mode", transient ? "transient" : "steady"},
        {"sample_time_si", sample_time},
        {"accounting_basis", request.accounting_basis},
        {"system_boundary", request.system_boundary},
        {"profile", {
            {"diagram", request.diagram_profile},
            {"calculation", request.calculation_profile},
            {"conformance", "informative"},
            {"evaluated_requirements", std::move(evaluated_requirements)},
            {"unevaluated_requirements", std::move(unevaluated_requirements)}
        }},
        {"boundary", {
            {"mass_input_si", boundary_mass_input},
            {"mass_output_si", boundary_mass_output},
            {"energy_input_si", boundary_input},
            {"energy_output_si", boundary_output},
            {"net_energy_flow_si", reported_energy_balance},
            {"net_mass_flow_si", reported_mass_balance},
            {"energy_reference_si", energy_reference},
            {"mass_reference_si", mass_reference},
            {"relative_energy_closure", relative_energy_closure},
            {"relative_mass_closure", relative_mass_closure},
            {"closure_interpretation", transient
                ? "net boundary rate; stored-energy accumulation not evaluated"
                : "steady whole-system closure residual"}
        }},
        {"closure_acceptance", closure_acceptance(job, transient)},
        {"uncertainty", std::move(uncertainty)},
        {"boundary_streams", std::move(streams)},
        {"component_closures", std::move(components)},
    };
    return report.dump();
}

std::string serialize_balance_report_markdown(
    std::string_view report_json) {
    const Json report = Json::parse(report_json);
    const auto& profile = report.at("profile");
    const auto& provenance = report.at("provenance");
    const auto& boundary = report.at("boundary");
    std::ostringstream out;
    out << "# Thermox thermal balance report\n\n"
        << "> Informative engineering report. Clause-level standards "
           "conformity is not demonstrated.\n\n"
        << "## Scope and provenance\n\n"
        << "- Job: `" << markdown_field(report.at("job_id").get<std::string>())
        << "`\n- Result checksum: `"
        << markdown_field(report.at("result_checksum").get<std::string>())
        << "`\n- Model revision: `"
        << markdown_field(provenance.at("model_revision_id").get<std::string>())
        << "`\n- Model checksum: `"
        << markdown_field(provenance.at("model_checksum").get<std::string>())
        << "`\n- Case revision: `"
        << markdown_field(provenance.at("case_revision_id").get<std::string>())
        << "`\n- Run configuration revision: `"
        << markdown_field(
               provenance.at("run_configuration_revision_id")
                   .get<std::string>())
        << "`\n- Diagram profile: `"
        << markdown_field(profile.at("diagram").get<std::string>())
        << "` (" << profile.at("conformance").get<std::string>() << ")\n"
        << "- Calculation mode: `" << report.at("mode").get<std::string>()
        << "`\n\n## Whole-system accounting\n\n"
        << "| Quantity | SI value | Unit |\n|---|---:|---|\n"
        << "| Energy input | "
        << number(boundary.at("energy_input_si").get<double>())
        << " | W |\n| Energy output | "
        << number(boundary.at("energy_output_si").get<double>())
        << " | W |\n| Mass input | "
        << number(boundary.at("mass_input_si").get<double>())
        << " | kg/s |\n| Mass output | "
        << number(boundary.at("mass_output_si").get<double>())
        << " | kg/s |\n| Net energy flow / closure | "
        << number(boundary.at("net_energy_flow_si").get<double>())
        << " | W |\n| Net mass flow / closure | "
        << number(boundary.at("net_mass_flow_si").get<double>())
        << " | kg/s |\n\nInterpretation: "
        << markdown_field(
               boundary.at("closure_interpretation").get<std::string>())
        << ".\n\nNormalized energy closure: ";
    if (boundary.at("relative_energy_closure").is_null()) {
        out << "not defined";
    } else {
        out << number(
            boundary.at("relative_energy_closure").get<double>() * 100.0)
            << "%";
    }
    out << ". Normalized mass closure: ";
    if (boundary.at("relative_mass_closure").is_null()) {
        out << "not defined";
    } else {
        out << number(
            boundary.at("relative_mass_closure").get<double>() * 100.0)
            << "%";
    }
    const auto& acceptance = report.at("closure_acceptance");
    out << ".\n\n## Closure acceptance\n\n"
        << "- Status: **"
        << markdown_field(acceptance.at("status").get<std::string>())
        << "**\n- Source: `"
        << markdown_field(acceptance.at("source").get<std::string>())
        << "`\n- Coverage: energy "
        << (acceptance.at("energy_declared").get<bool>() ? "declared" : "missing")
        << ", mass "
        << (acceptance.at("mass_declared").get<bool>() ? "declared" : "missing")
        << "\n- Interpretation: "
        << markdown_field(acceptance.at("interpretation").get<std::string>())
        << "\n\n| Criterion | Balance | Actual (SI) | Lower | Upper | Result |\n"
           "|---|---|---:|---:|---:|---|\n";
    for (const auto& criterion : acceptance.at("criteria")) {
        out << "| "
            << markdown_field(criterion.at("criterion_id").get<std::string>())
            << " | " << criterion.at("balance").get<std::string>() << " | "
            << number(criterion.at("actual_value_si").get<double>()) << " | ";
        if (criterion.at("lower_bound_si").is_null()) out << "—";
        else out << number(criterion.at("lower_bound_si").get<double>());
        out << " | ";
        if (criterion.at("upper_bound_si").is_null()) out << "—";
        else out << number(criterion.at("upper_bound_si").get<double>());
        out << " | " << (criterion.at("passed").get<bool>() ? "pass" : "fail")
            << " |\n";
    }
    const auto& uncertainty = report.at("uncertainty");
    out << "\n## Measurement uncertainty\n\n"
        << "- Status: **"
        << markdown_field(uncertainty.at("status").get<std::string>())
        << "**\n- Interpretation: "
        << markdown_field(uncertainty.at("interpretation").get<std::string>())
        << "\n";
    if (!uncertainty.at("source").is_null()) {
        out << "- Model: `"
            << markdown_field(
                   uncertainty.at("source").at("model_id").get<std::string>())
            << "`\n";
        const auto artifact_revision_id = uncertainty.at("source")
            .at("artifact_revision_id").get<std::string>();
        if (!artifact_revision_id.empty()) {
            out << "- Artifact revision: `"
                << markdown_field(artifact_revision_id) << "`\n";
        }
        out << "- Source: "
            << markdown_field(
                   uncertainty.at("source").at("reference").get<std::string>())
            << "\n- Source checksum: `"
            << markdown_field(
                   uncertainty.at("source").at("checksum_sha256")
                       .get<std::string>())
            << "`\n";
    }
    out << "\n| Quantity | Standard uncertainty (SI) | Expanded uncertainty k=2 (SI) |\n"
           "|---|---:|---:|\n";
    for (const auto* quantity : {"mass", "energy"}) {
        out << "| " << quantity << " | ";
        if (uncertainty.at(quantity).is_null()) {
            out << "not evaluated | not evaluated |\n";
        } else {
            out << number(
                       uncertainty.at(quantity)
                           .at("standard_uncertainty_si").get<double>())
                << " | "
                << number(
                       uncertainty.at(quantity)
                           .at("expanded_uncertainty_si_k2").get<double>())
                << " |\n";
        }
    }
    out << "\nExpanded uncertainty uses coverage factor k=2; no coverage "
           "probability is inferred.\n\nLimitations:\n";
    for (const auto& limitation : uncertainty.at("limitations")) {
        out << "- " << markdown_field(limitation.get<std::string>()) << "\n";
    }
    out << "\n## Boundary streams\n\n"
        << "| Component | Port | Direction | Domain | Mass flow (kg/s) | "
           "Energy flow (W) |\n|---|---|---|---|---:|---:|\n";
    for (const auto& stream : report.at("boundary_streams")) {
        out << "| " << markdown_field(stream.at("component_id").get<std::string>())
            << " | " << markdown_field(stream.at("port_name").get<std::string>())
            << " | " << stream.at("boundary_direction").get<std::string>()
            << " | " << stream.at("domain").get<std::string>() << " | "
            << number(stream.at("mass_flow_si").get<double>()) << " | "
            << number(stream.at("energy_flow_si").get<double>()) << " |\n";
    }
    out << "\n## Component closures\n\n"
        << "| Component | Kind | Net mass flow (kg/s) | Net energy flow (W) |\n"
           "|---|---|---:|---:|\n";
    for (const auto& component : report.at("component_closures")) {
        out << "| "
            << markdown_field(component.at("component_id").get<std::string>())
            << " | " << markdown_field(component.at("kind").get<std::string>())
            << " | " << number(component.at("net_mass_flow_si").get<double>())
            << " | " << number(component.at("net_energy_flow_si").get<double>())
            << " |\n";
    }
    out << "\n## Not evaluated\n\n";
    for (const auto& limitation : profile.at("unevaluated_requirements")) {
        out << "- " << markdown_field(limitation.get<std::string>()) << "\n";
    }
    return out.str();
}

std::string serialize_balance_report_csv(std::string_view report_json) {
    const Json report = Json::parse(report_json);
    const auto& provenance = report.at("provenance");
    const auto job_id = csv_field(report.at("job_id").get<std::string>());
    const auto checksum =
        csv_field(report.at("result_checksum").get<std::string>());
    const auto model_revision = csv_field(
        provenance.at("model_revision_id").get<std::string>());
    std::ostringstream out;
    out << "record_type,job_id,result_checksum,model_revision_id,component_id,"
           "kind,port_name,direction,domain,mass_flow_kg_s,energy_flow_W,"
           "net_mass_flow_kg_s,net_energy_flow_W,reference_mass_flow_kg_s,"
           "reference_energy_flow_W,relative_mass_closure,"
           "relative_energy_closure,acceptance_status,uncertainty_model_id,"
           "uncertainty_artifact_revision_id,"
           "mass_standard_uncertainty_kg_s,energy_standard_uncertainty_W,"
           "energy_expanded_uncertainty_k2_W,uncertainty_status\n";
    const auto& boundary = report.at("boundary");
    const auto relative_mass = boundary.at("relative_mass_closure").is_null()
        ? std::string{}
        : number(boundary.at("relative_mass_closure").get<double>());
    const auto relative_energy =
        boundary.at("relative_energy_closure").is_null()
        ? std::string{}
        : number(boundary.at("relative_energy_closure").get<double>());
    const auto& uncertainty = report.at("uncertainty");
    const auto uncertainty_model_id = uncertainty.at("source").is_null()
        ? std::string{}
        : csv_field(
              uncertainty.at("source").at("model_id").get<std::string>());
    const auto uncertainty_artifact_revision_id =
        uncertainty.at("source").is_null()
        ? std::string{}
        : csv_field(
              uncertainty.at("source")
                  .at("artifact_revision_id").get<std::string>());
    const auto mass_uncertainty = uncertainty.at("mass").is_null()
        ? std::string{}
        : number(uncertainty.at("mass")
                     .at("standard_uncertainty_si").get<double>());
    const auto energy_uncertainty = uncertainty.at("energy").is_null()
        ? std::string{}
        : number(uncertainty.at("energy")
                     .at("standard_uncertainty_si").get<double>());
    const auto expanded_energy_uncertainty =
        uncertainty.at("energy").is_null()
        ? std::string{}
        : number(uncertainty.at("energy")
                     .at("expanded_uncertainty_si_k2").get<double>());
    out << "system_summary," << job_id << ',' << checksum << ','
        << model_revision << ",,,,,,,,"
        << number(boundary.at("net_mass_flow_si").get<double>()) << ','
        << number(boundary.at("net_energy_flow_si").get<double>()) << ','
        << number(boundary.at("mass_reference_si").get<double>()) << ','
        << number(boundary.at("energy_reference_si").get<double>()) << ','
        << relative_mass << ',' << relative_energy << ','
        << report.at("closure_acceptance").at("status").get<std::string>()
        << ',' << uncertainty_model_id << ','
        << uncertainty_artifact_revision_id << ',' << mass_uncertainty << ','
        << energy_uncertainty << ',' << expanded_energy_uncertainty << ','
        << uncertainty.at("status").get<std::string>()
        << '\n';
    for (const auto& stream : report.at("boundary_streams")) {
        out << "boundary_stream," << job_id << ',' << checksum << ','
            << model_revision << ','
            << csv_field(stream.at("component_id").get<std::string>())
            << ",," << csv_field(stream.at("port_name").get<std::string>())
            << ',' << stream.at("boundary_direction").get<std::string>()
            << ',' << stream.at("domain").get<std::string>() << ','
            << number(stream.at("mass_flow_si").get<double>()) << ','
            << number(stream.at("energy_flow_si").get<double>())
            << ",,,,,,,,,,,,,\n";
    }
    for (const auto& component : report.at("component_closures")) {
        out << "component_closure," << job_id << ',' << checksum << ','
            << model_revision << ','
            << csv_field(component.at("component_id").get<std::string>())
            << ',' << csv_field(component.at("kind").get<std::string>())
            << ",,,,,,"
            << number(component.at("net_mass_flow_si").get<double>()) << ','
            << number(component.at("net_energy_flow_si").get<double>())
            << ",,,,,,,,,,,\n";
    }
    return out.str();
}

}  // namespace thermox::service

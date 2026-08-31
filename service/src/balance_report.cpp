#include "thermox/service/balance_report.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <set>
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

}  // namespace

std::string build_balance_report_json(
    const SimulationJobRecord& job,
    const ResultArtifact& result,
    const std::string& canonical_topology_json,
    const BalanceReportRequest& request) {
    if (request.schema_version != balance_report_request_schema_v1 ||
        request.accounting_basis != "energy" ||
        request.system_boundary != "whole_system" ||
        request.diagram_profile != "iso-14084-1:2015" ||
        request.calculation_profile != "none") {
        throw std::invalid_argument(
            "balance report supports schema v1 whole-system energy accounting "
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
            const auto found = directions.find({id, name});
            if (found == directions.end()) continue;
            const auto [mass, energy] = port_flows(port);
            const double orientation = found->second == "in" ? 1.0 : -1.0;
            net_mass += orientation * mass;
            net_energy += orientation * energy;
            component_directions.insert(found->second);
            observed_ports.push_back({
                {"port_name", name},
                {"domain", port.value("domain", std::string{})},
                {"direction", found->second},
                {"mass_flow_si", mass},
                {"energy_flow_si", energy},
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
                if (direction == "out") boundary_input += magnitude;
                else boundary_output += magnitude;
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

    Json report = {
        {"schema_version", balance_report_schema_v1},
        {"job_id", job.job_id},
        {"result_checksum", result.manifest.checksum},
        {"mode", transient ? "transient" : "steady"},
        {"sample_time_si", sample_time},
        {"accounting_basis", request.accounting_basis},
        {"system_boundary", request.system_boundary},
        {"profile", {
            {"diagram", request.diagram_profile},
            {"calculation", request.calculation_profile},
            {"conformance", "informative"},
            {"evaluated_requirements", Json::array({
                "exact immutable result provenance",
                "whole-system mass and energy boundary accounting",
                "component mass and energy closure"
            })},
            {"unevaluated_requirements", Json::array({
                "clause-level standards conformity",
                "measurement uncertainty propagation",
                "exergy accounting",
                "transient stored-energy accumulation"
            })}
        }},
        {"boundary", {
            {"energy_input_si", boundary_input},
            {"energy_output_si", boundary_output},
            {"net_energy_flow_si", reported_energy_balance},
            {"net_mass_flow_si", reported_mass_balance},
            {"closure_interpretation", transient
                ? "net boundary rate; stored-energy accumulation not evaluated"
                : "steady whole-system closure residual"}
        }},
        {"boundary_streams", std::move(streams)},
        {"component_closures", std::move(components)},
    };
    return report.dump();
}

}  // namespace thermox::service

#include "thermox/service/balance_report.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <iomanip>
#include <sstream>
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
        << " | W |\n| Net energy flow / closure | "
        << number(boundary.at("net_energy_flow_si").get<double>())
        << " | W |\n| Net mass flow / closure | "
        << number(boundary.at("net_mass_flow_si").get<double>())
        << " | kg/s |\n\nInterpretation: "
        << markdown_field(
               boundary.at("closure_interpretation").get<std::string>())
        << ".\n\n## Boundary streams\n\n"
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
           "net_mass_flow_kg_s,net_energy_flow_W\n";
    const auto& boundary = report.at("boundary");
    out << "system_summary," << job_id << ',' << checksum << ','
        << model_revision << ",,,,,,,,"
        << number(boundary.at("net_mass_flow_si").get<double>()) << ','
        << number(boundary.at("net_energy_flow_si").get<double>()) << '\n';
    for (const auto& stream : report.at("boundary_streams")) {
        out << "boundary_stream," << job_id << ',' << checksum << ','
            << model_revision << ','
            << csv_field(stream.at("component_id").get<std::string>())
            << ",," << csv_field(stream.at("port_name").get<std::string>())
            << ',' << stream.at("boundary_direction").get<std::string>()
            << ',' << stream.at("domain").get<std::string>() << ','
            << number(stream.at("mass_flow_si").get<double>()) << ','
            << number(stream.at("energy_flow_si").get<double>()) << ",,\n";
    }
    for (const auto& component : report.at("component_closures")) {
        out << "component_closure," << job_id << ',' << checksum << ','
            << model_revision << ','
            << csv_field(component.at("component_id").get<std::string>())
            << ',' << csv_field(component.at("kind").get<std::string>())
            << ",,,,,,"
            << number(component.at("net_mass_flow_si").get<double>()) << ','
            << number(component.at("net_energy_flow_si").get<double>())
            << '\n';
    }
    return out.str();
}

}  // namespace thermox::service

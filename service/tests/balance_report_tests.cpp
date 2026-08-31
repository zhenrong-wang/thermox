#include "thermox/service/balance_report.hpp"

#include <boost/json/src.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_whole_system_energy_report() {
    using namespace thermox::service;
    SimulationJobRecord job;
    job.job_id = "balance-job";
    RevisionProvenance provenance;
    provenance.project_id = "project-a";
    provenance.model_revision_id = "model-r4";
    provenance.model_checksum = "sha256:model";
    provenance.case_revision_id = "case-r2";
    provenance.run_configuration_revision_id = "run-r3";
    job.request.source_revisions = provenance;
    ResultProjection energy_projection;
    energy_projection.id = "energy-closure";
    energy_projection.scope = ResultValueScope::system_balance;
    energy_projection.value_name = "net_boundary_energy_flow";
    energy_projection.dimension = "power";
    ResultProjection mass_projection;
    mass_projection.id = "mass-closure";
    mass_projection.scope = ResultValueScope::system_balance;
    mass_projection.value_name = "net_boundary_mass_flow";
    mass_projection.dimension = "mass_flow";
    job.request.result_projections = {energy_projection, mass_projection};
    EngineeringAcceptanceResult energy_acceptance;
    energy_acceptance.criterion_id = "energy-band";
    energy_acceptance.projection_id = energy_projection.id;
    energy_acceptance.dimension = "power";
    energy_acceptance.actual_value_si = 0.0;
    energy_acceptance.lower_bound_si = -1.0;
    energy_acceptance.upper_bound_si = 1.0;
    energy_acceptance.passed = true;
    EngineeringAcceptanceResult mass_acceptance;
    mass_acceptance.criterion_id = "mass-band";
    mass_acceptance.projection_id = mass_projection.id;
    mass_acceptance.dimension = "mass_flow";
    mass_acceptance.actual_value_si = 0.0;
    mass_acceptance.lower_bound_si = -0.01;
    mass_acceptance.upper_bound_si = 0.01;
    mass_acceptance.passed = true;
    EngineeringAcceptanceSummary acceptance;
    acceptance.passed = true;
    acceptance.passed_count = 2U;
    acceptance.criteria = {energy_acceptance, mass_acceptance};
    ResultSummary summary;
    summary.mode = "steady";
    summary.engineering_acceptance = acceptance;
    job.result_summary = summary;
    ResultArtifact result;
    result.manifest.checksum = "sha256:result";
    result.content = R"({"graph":{"components":[
      {"component_id":"source","kind":"source.fluid.boundary","ports":[
        {"port_name":"outlet","domain":"fluid","primary_values":[
          {"name":"m_dot","value_si":2.0},{"name":"h","value_si":50.0}]}]},
      {"component_id":"load","kind":"test.load","ports":[
        {"port_name":"inlet","domain":"fluid","primary_values":[
          {"name":"m_dot","value_si":2.0},{"name":"h","value_si":50.0}]},
        {"port_name":"outlet","domain":"fluid","primary_values":[
          {"name":"m_dot","value_si":2.0},{"name":"h","value_si":50.0}]}]},
      {"component_id":"sink","kind":"sink.fluid.boundary","ports":[
        {"port_name":"inlet","domain":"fluid","primary_values":[
          {"name":"m_dot","value_si":2.0},{"name":"h","value_si":50.0}]}]}
    ],"system_balances":[
      {"name":"net_boundary_mass_flow","value_si":0.0},
      {"name":"net_boundary_energy_flow","value_si":0.0}
    ]}})";
    const std::string topology = R"({"model":{"connections":[
      {"from":"source.outlet","to":"load.inlet"},
      {"from":"load.outlet","to":"sink.inlet"}
    ]}})";
    BalanceReportRequest report_request;
    report_request.uncertainty_model = parse_balance_uncertainty_model_json(
        R"({"schema_version":"thermox.balance_uncertainty/v1",)"
        R"("id":"metering-r1","source":{"reference":"calibration-certificates",)"
        R"("checksum_sha256":"0000000000000000000000000000000000000000000000000000000000000000",)"
        R"("note":"standard uncertainties","limitations":[]},"streams":[)"
        R"({"component_id":"source","port_name":"outlet",)"
        R"("mass_flow_standard_uncertainty_si":0.02,)"
        R"("specific_enthalpy_standard_uncertainty_si":0.5,)"
        R"("energy_flow_standard_uncertainty_si":null,)"
        R"("mass_enthalpy_correlation":0.2},)"
        R"({"component_id":"sink","port_name":"inlet",)"
        R"("mass_flow_standard_uncertainty_si":0.02,)"
        R"("specific_enthalpy_standard_uncertainty_si":null,)"
        R"("energy_flow_standard_uncertainty_si":1.5,)"
        R"("mass_enthalpy_correlation":0.0}],"correlations":[)"
        R"({"quantity":"mass_flow","first":{"component_id":"source","port_name":"outlet"},)"
        R"("second":{"component_id":"sink","port_name":"inlet"},"coefficient":0.5},)"
        R"({"quantity":"energy_flow","first":{"component_id":"source","port_name":"outlet"},)"
        R"("second":{"component_id":"sink","port_name":"inlet"},"coefficient":0.25}]})");
    const auto report = build_balance_report_json(
        job, result, topology, report_request);
    require(
        report.find("\"schema_version\":\"thermox.balance_report/v3\"") !=
                std::string::npos &&
            report.find("\"conformance\":\"informative\"") !=
                std::string::npos &&
            report.find("\"energy_input_si\":100.0") !=
                std::string::npos &&
            report.find("\"energy_output_si\":100.0") !=
                std::string::npos &&
            report.find("\"model_revision_id\":\"model-r4\"") !=
                std::string::npos &&
            report.find("\"relative_energy_closure\":0.0") !=
                std::string::npos &&
            report.find("\"status\":\"passed\"") !=
                std::string::npos &&
            report.find("\"model_id\":\"metering-r1\"") !=
                std::string::npos &&
            report.find("\"correlation_count\":2") !=
                std::string::npos &&
            report.find("\"status\":\"complete\"") !=
                std::string::npos,
        "balance report must preserve profile and boundary accounting");
    const auto parsed_report = boost::json::parse(report).as_object();
    const auto& uncertainty =
        parsed_report.at("uncertainty").as_object();
    const double expected_energy_uncertainty = std::sqrt(
        2.4 + 2.25 - 2.0 * 0.25 * std::sqrt(2.4) * 1.5);
    require(
        std::abs(
            uncertainty.at("mass").as_object()
                    .at("standard_uncertainty_si").as_double() -
            0.02) < 1.0e-12 &&
            std::abs(
                uncertainty.at("energy").as_object()
                        .at("standard_uncertainty_si").as_double() -
                expected_energy_uncertainty) < 1.0e-12,
        "correlated mass and energy closure uncertainties must follow the "
        "declared first-order covariance model");
    const auto markdown = serialize_balance_report_markdown(report);
    require(
        markdown.find("# Thermox thermal balance report") !=
                std::string::npos &&
            markdown.find("model-r4") != std::string::npos &&
            markdown.find("Clause-level standards conformity is not") !=
                std::string::npos &&
            markdown.find("## Measurement uncertainty") !=
                std::string::npos &&
            markdown.find("metering-r1") != std::string::npos,
        "Markdown export must carry accounting, provenance, and limitations");
    const auto csv = serialize_balance_report_csv(report);
    require(
        csv.find("record_type,job_id,result_checksum") == 0U &&
            csv.find("boundary_stream,balance-job") != std::string::npos &&
            csv.find("component_closure,balance-job") != std::string::npos &&
            csv.find("model-r4") != std::string::npos,
        "CSV export must carry machine-readable accounting and provenance");
    std::istringstream rows(csv);
    std::string row;
    while (std::getline(rows, row)) {
        require(
            static_cast<std::size_t>(std::count(row.begin(), row.end(), ',')) ==
                22U,
            "each simple CSV test record must preserve the 23-column schema");
    }
    job.result_summary.reset();
    const auto undeclared = build_balance_report_json(
        job, result, topology, report_request);
    require(
        undeclared.find("\"status\":\"not_declared\"") !=
            std::string::npos,
        "closure acceptance must remain undeclared without immutable run "
        "criteria");
}

void test_transient_closure_is_not_accepted_without_accumulation() {
    using namespace thermox::service;
    SimulationJobRecord job;
    job.job_id = "transient-balance-job";
    ResultArtifact result;
    result.manifest.checksum = "sha256:transient";
    result.content = R"({"trajectory":[{"time":2.0,"graph":{
      "components":[
        {"component_id":"source","kind":"source.fluid.boundary","ports":[
          {"port_name":"outlet","domain":"fluid","primary_values":[
            {"name":"m_dot","value_si":1.0},{"name":"h","value_si":20.0}]}]},
        {"component_id":"sink","kind":"sink.fluid.boundary","ports":[
          {"port_name":"inlet","domain":"fluid","primary_values":[
            {"name":"m_dot","value_si":1.0},{"name":"h","value_si":20.0}]}]}
      ],"system_balances":[
        {"name":"net_boundary_mass_flow","value_si":0.0},
        {"name":"net_boundary_energy_flow","value_si":0.0}
      ]}}]})";
    const std::string topology = R"({"model":{"connections":[
      {"from":"source.outlet","to":"sink.inlet"}
    ]}})";
    const auto report = build_balance_report_json(
        job, result, topology, BalanceReportRequest{});
    require(
        report.find("\"mode\":\"transient\"") != std::string::npos &&
            report.find("\"status\":\"not_evaluated\"") !=
                std::string::npos &&
            report.find("stored mass and energy accumulation") !=
                std::string::npos,
        "transient snapshots must not claim closure acceptance without "
        "accumulation terms");
}

void test_unsupported_profile_is_rejected() {
    using namespace thermox::service;
    BalanceReportRequest request;
    request.diagram_profile = "unverified-profile";
    bool rejected = false;
    try {
        (void)build_balance_report_json(
            SimulationJobRecord{}, ResultArtifact{}, "{}", request);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "unsupported standards profiles must be rejected");
}

}  // namespace

int main() {
    try {
        test_whole_system_energy_report();
        test_transient_closure_is_not_accepted_without_accumulation();
        test_unsupported_profile_is_rejected();
        std::cout << "thermox balance report tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thermox balance report tests failed: "
                  << error.what() << "\n";
        return 1;
    }
}

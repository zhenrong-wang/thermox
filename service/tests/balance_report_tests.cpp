#include "thermox/service/balance_report.hpp"

#include <algorithm>
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
    const auto report = build_balance_report_json(
        job, result, topology, BalanceReportRequest{});
    require(
        report.find("\"schema_version\":\"thermox.balance_report/v1\"") !=
                std::string::npos &&
            report.find("\"conformance\":\"informative\"") !=
                std::string::npos &&
            report.find("\"energy_input_si\":100.0") !=
                std::string::npos &&
            report.find("\"energy_output_si\":100.0") !=
                std::string::npos &&
            report.find("\"model_revision_id\":\"model-r4\"") !=
                std::string::npos,
        "balance report must preserve profile and boundary accounting");
    const auto markdown = serialize_balance_report_markdown(report);
    require(
        markdown.find("# Thermox thermal balance report") !=
                std::string::npos &&
            markdown.find("model-r4") != std::string::npos &&
            markdown.find("Clause-level standards conformity is not") !=
                std::string::npos,
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
                12U,
            "each simple CSV test record must preserve the 13-column schema");
    }
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
        test_unsupported_profile_is_rejected();
        std::cout << "thermox balance report tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thermox balance report tests failed: "
                  << error.what() << "\n";
        return 1;
    }
}

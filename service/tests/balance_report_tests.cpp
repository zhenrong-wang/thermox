#include "thermox/service/balance_report.hpp"

#include <iostream>
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
                std::string::npos,
        "balance report must preserve profile and boundary accounting");
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

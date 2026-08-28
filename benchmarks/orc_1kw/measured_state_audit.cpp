#include "thermox/physics/coolprop_heos_package.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Row = std::map<std::string, double, std::less<>>;

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::istringstream input(line);
    for (std::string field; std::getline(input, field, ',');)
        fields.push_back(std::move(field));
    return fields;
}

std::vector<Row> read_csv(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open measurement CSV: " + path);
    std::string line;
    if (!std::getline(input, line))
        throw std::runtime_error("measurement CSV is empty");
    const auto headers = split_csv_line(line);
    std::vector<Row> rows;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto fields = split_csv_line(line);
        if (fields.size() != headers.size())
            throw std::runtime_error("measurement CSV row width mismatch");
        Row row;
        for (std::size_t index = 0; index < headers.size(); ++index) {
            std::size_t parsed = 0;
            const double item = std::stod(fields.at(index), &parsed);
            if (parsed != fields.at(index).size() || !std::isfinite(item))
                throw std::runtime_error("measurement CSV contains a non-finite value");
            row.emplace(headers.at(index), item);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

double value(const Row& row, std::string_view key) {
    const auto position = row.find(key);
    if (position == row.end())
        throw std::runtime_error("measurement column is missing: " + std::string{key});
    return position->second;
}

thermox::physics::ThermodynamicState state_pt(
    const thermox::physics::PropertyPackage& package,
    double temperature_c,
    double pressure_kpa) {
    const auto result = package.state_pt(
        pressure_kpa * 1000.0, temperature_c + 273.15);
    if (!result.ok())
        throw std::runtime_error("property evaluation failed: " + result.message);
    return result.state;
}

struct Statistics {
    double minimum{};
    double maximum{};
    double mean{};
    double mean_absolute{};
    double maximum_absolute{};
};

Statistics statistics(const std::vector<double>& values) {
    if (values.empty()) throw std::runtime_error("cannot summarize an empty series");
    const auto [minimum, maximum] =
        std::minmax_element(values.begin(), values.end());
    Statistics result;
    result.minimum = *minimum;
    result.maximum = *maximum;
    result.mean = std::accumulate(values.begin(), values.end(), 0.0) /
        static_cast<double>(values.size());
    for (const double item : values) {
        result.mean_absolute += std::abs(item);
        result.maximum_absolute =
            std::max(result.maximum_absolute, std::abs(item));
    }
    result.mean_absolute /= static_cast<double>(values.size());
    return result;
}

void write_statistics(
    std::ostream& output,
    std::string_view name,
    const Statistics& stats,
    bool comma = true) {
    output << "    \"" << name << "\": {"
           << "\"minimum\": " << stats.minimum << ", "
           << "\"maximum\": " << stats.maximum << ", "
           << "\"mean\": " << stats.mean << ", "
           << "\"mean_absolute\": " << stats.mean_absolute << ", "
           << "\"maximum_absolute\": " << stats.maximum_absolute << "}"
           << (comma ? "," : "") << "\n";
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc != 2) {
        std::cerr << "usage: thermox_orc_1kw_measured_state_audit <measurements.csv>\n";
        return 2;
    }
    const auto rows = read_csv(argv[1]);
    if (rows.size() != 77U)
        throw std::runtime_error("the audited source must contain exactly 77 cases");

    const thermox::physics::CoolPropHeosPropertyPackage working_fluid{"R245fa"};
    const thermox::physics::CoolPropHeosPropertyPackage water{"Water"};

    std::vector<double> loop_residual_percent;
    std::vector<double> evaporator_external_difference_percent;
    std::vector<double> condenser_external_difference_percent;
    std::vector<double> expander_shaft_efficiency_percent;
    std::vector<double> gross_thermal_efficiency_percent;
    std::vector<double> evaporator_duty_w;
    std::vector<double> expander_shaft_power_w;
    std::vector<int> nonpositive_pump_power_cases;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows.at(index);
        const int case_id = static_cast<int>(value(row, "case"));
        if (case_id != static_cast<int>(index + 1U))
            throw std::runtime_error("case identifiers must be consecutive from 1 to 77");

        const auto pump_in = state_pt(working_fluid,
            value(row, "pump_inlet_temperature_c"),
            value(row, "pump_inlet_pressure_kpa"));
        const auto pump_out = state_pt(working_fluid,
            value(row, "pump_outlet_temperature_c"),
            value(row, "pump_outlet_pressure_kpa"));
        const auto evaporator_in = state_pt(working_fluid,
            value(row, "evaporator_inlet_temperature_c"),
            value(row, "evaporator_inlet_pressure_kpa"));
        const auto evaporator_out = state_pt(working_fluid,
            value(row, "evaporator_outlet_temperature_c"),
            value(row, "evaporator_outlet_pressure_kpa"));
        const auto expander_in = state_pt(working_fluid,
            value(row, "expander_inlet_temperature_c"),
            value(row, "expander_inlet_pressure_kpa"));
        const auto expander_out = state_pt(working_fluid,
            value(row, "expander_outlet_temperature_c"),
            value(row, "expander_outlet_pressure_kpa"));
        const auto condenser_in = state_pt(working_fluid,
            value(row, "condenser_inlet_temperature_c"),
            value(row, "condenser_inlet_pressure_kpa"));
        const auto condenser_out = state_pt(working_fluid,
            value(row, "condenser_outlet_temperature_c"),
            value(row, "condenser_outlet_pressure_kpa"));

        const double mass_flow =
            value(row, "working_fluid_mass_flow_g_s") / 1000.0;
        const double pump_power = mass_flow *
            (pump_out.enthalpy_j_kg - pump_in.enthalpy_j_kg);
        const double evaporator_power = mass_flow *
            (evaporator_out.enthalpy_j_kg - evaporator_in.enthalpy_j_kg);
        const double expander_fluid_power = mass_flow *
            (expander_in.enthalpy_j_kg - expander_out.enthalpy_j_kg);
        const double condenser_power = mass_flow *
            (condenser_in.enthalpy_j_kg - condenser_out.enthalpy_j_kg);
        const double expander_shaft_power =
            value(row, "expander_torque_n_m") *
            value(row, "expander_speed_rpm") *
            (2.0 * std::numbers::pi / 60.0);
        if (!(pump_power > 0.0)) nonpositive_pump_power_cases.push_back(case_id);

        const auto hot_in = state_pt(water,
            value(row, "hot_water_inlet_temperature_c"),
            value(row, "hot_water_inlet_pressure_kpa"));
        const auto hot_out = state_pt(water,
            value(row, "hot_water_outlet_temperature_c"),
            value(row, "hot_water_outlet_pressure_kpa"));
        const auto cooling_in = state_pt(water,
            value(row, "cooling_water_inlet_temperature_c"),
            value(row, "cooling_water_inlet_pressure_kpa"));
        const auto cooling_out = state_pt(water,
            value(row, "cooling_water_outlet_temperature_c"),
            value(row, "cooling_water_outlet_pressure_kpa"));
        const double hot_mass_flow =
            value(row, "hot_water_volume_flow_lpm") / 60000.0 *
            hot_in.density_kg_m3;
        const double cooling_mass_flow =
            value(row, "cooling_water_volume_flow_lpm") / 60000.0 *
            cooling_in.density_kg_m3;
        const double hot_water_power = hot_mass_flow *
            (hot_in.enthalpy_j_kg - hot_out.enthalpy_j_kg);
        const double cooling_water_power = cooling_mass_flow *
            (cooling_out.enthalpy_j_kg - cooling_in.enthalpy_j_kg);

        evaporator_duty_w.push_back(evaporator_power);
        expander_shaft_power_w.push_back(expander_shaft_power);
        loop_residual_percent.push_back(100.0 *
            (evaporator_power - condenser_power - expander_fluid_power +
                pump_power) / evaporator_power);
        evaporator_external_difference_percent.push_back(100.0 *
            (evaporator_power - hot_water_power) / hot_water_power);
        condenser_external_difference_percent.push_back(100.0 *
            (condenser_power - cooling_water_power) / cooling_water_power);
        expander_shaft_efficiency_percent.push_back(
            100.0 * expander_shaft_power / expander_fluid_power);
        gross_thermal_efficiency_percent.push_back(
            100.0 * expander_shaft_power / evaporator_power);
    }

    const auto loop = statistics(loop_residual_percent);
    const bool loop_gate =
        loop.mean_absolute <= 1.0 && loop.maximum_absolute <= 2.0;
    std::cout << std::setprecision(10)
              << "{\n"
              << "  \"schema_version\": \"thermox.measured_state_audit/v1\",\n"
              << "  \"benchmark_id\": \"orc_1kw_r245fa\",\n"
              << "  \"classification\": \"boundary_constrained_measured_state_accounting\",\n"
              << "  \"case_count\": " << rows.size() << ",\n"
              << "  \"property_provider\": {\"backend\": \"coolprop_heos\", "
              << "\"substance\": \"R245fa\", \"implementation\": \""
              << working_fluid.name() << "\", \"version\": \""
              << working_fluid.version() << "\"},\n"
              << "  \"assumptions\": [\"water volume flow is converted to mass flow with inlet-state density\", \"component boundary PT measurements are treated as exact point estimates for this audit\", \"unmodeled connecting-line energy appears in the closed-loop residual\"],\n"
              << "  \"metrics\": {\n";
    write_statistics(std::cout, "closed_loop_energy_residual_percent", loop);
    write_statistics(std::cout,
        "evaporator_fluid_minus_hot_water_percent",
        statistics(evaporator_external_difference_percent));
    write_statistics(std::cout,
        "condenser_fluid_minus_cooling_water_percent",
        statistics(condenser_external_difference_percent));
    write_statistics(std::cout, "expander_shaft_efficiency_percent",
        statistics(expander_shaft_efficiency_percent));
    write_statistics(std::cout, "gross_thermal_efficiency_percent",
        statistics(gross_thermal_efficiency_percent));
    write_statistics(
        std::cout, "evaporator_duty_w", statistics(evaporator_duty_w));
    write_statistics(std::cout, "expander_shaft_power_w",
        statistics(expander_shaft_power_w), false);
    std::cout << "  },\n  \"diagnostics\": {\"nonpositive_measured_pump_fluid_power_cases\": [";
    for (std::size_t index = 0;
         index < nonpositive_pump_power_cases.size(); ++index) {
        if (index) std::cout << ", ";
        std::cout << nonpositive_pump_power_cases.at(index);
    }
    std::cout << "]},\n"
              << "  \"acceptance\": {\"mean_absolute_loop_residual_limit_percent\": 1.0, "
              << "\"maximum_absolute_loop_residual_limit_percent\": 2.0, "
              << "\"passed\": " << (loop_gate ? "true" : "false") << "},\n"
              << "  \"claim_limit\": \"This audit validates property reconstruction and measured-state conservation only; it does not predict internal states from external boundaries.\"\n"
              << "}\n";
    return loop_gate ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}

#include "thermox/least_squares_solver.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
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
        for (std::size_t index = 0; index < fields.size(); ++index)
            row.emplace(headers.at(index), std::stod(fields.at(index)));
        rows.push_back(std::move(row));
    }
    if (rows.size() != 77U)
        throw std::runtime_error("expected exactly 77 source cases");
    return rows;
}

double value(const Row& row, std::string_view key) {
    const auto found = row.find(key);
    if (found == row.end())
        throw std::runtime_error("missing measurement column: " + std::string{key});
    return found->second;
}

struct Candidate {
    std::string id;
    std::vector<std::string> features;
    bool squared_terms{false};
    double ridge_lambda{0.0};
};

const std::vector<Candidate>& candidates() {
    static const std::vector<Candidate> definitions{
        {"hot_source_linear", {"hot_water_inlet_temperature_c"}, false, 0.0},
        {"hot_source_quadratic", {"hot_water_inlet_temperature_c"}, true, 0.0},
        {"reduced_external_ridge_1",
         {"hot_water_inlet_temperature_c",
          "cooling_water_inlet_temperature_c", "pump_speed_rpm",
          "expander_speed_rpm", "charge_kg"}, false, 1.0},
        {"all_external_ridge_10",
         {"hot_water_inlet_temperature_c", "hot_water_volume_flow_lpm",
          "cooling_water_inlet_temperature_c",
          "cooling_water_volume_flow_lpm", "pump_speed_rpm",
          "expander_speed_rpm", "charge_kg", "ambient_temperature_c"},
         false, 10.0},
    };
    return definitions;
}

struct Normalization {
    std::vector<double> mean;
    std::vector<double> scale;
};

Normalization normalization(
    const std::vector<Row>& rows, const Candidate& candidate,
    const std::vector<std::size_t>& training) {
    Normalization result;
    result.mean.resize(candidate.features.size(), 0.0);
    result.scale.resize(candidate.features.size(), 0.0);
    for (std::size_t feature = 0; feature < candidate.features.size(); ++feature) {
        for (const auto index : training)
            result.mean.at(feature) +=
                value(rows.at(index), candidate.features.at(feature));
        result.mean.at(feature) /= static_cast<double>(training.size());
        for (const auto index : training) {
            const double delta =
                value(rows.at(index), candidate.features.at(feature)) -
                result.mean.at(feature);
            result.scale.at(feature) += delta * delta;
        }
        result.scale.at(feature) = std::sqrt(
            result.scale.at(feature) / static_cast<double>(training.size()));
        if (!(result.scale.at(feature) > 0.0))
            result.scale.at(feature) = 1.0;
    }
    return result;
}

std::vector<double> basis(
    const Row& row, const Candidate& candidate,
    const Normalization& norm) {
    std::vector<double> result{1.0};
    result.reserve(
        1U + candidate.features.size() *
            (candidate.squared_terms ? 2U : 1U));
    for (std::size_t feature = 0; feature < candidate.features.size(); ++feature) {
        result.push_back(
            (value(row, candidate.features.at(feature)) -
             norm.mean.at(feature)) /
            norm.scale.at(feature));
    }
    if (candidate.squared_terms) {
        const std::size_t linear_count = result.size();
        for (std::size_t column = 1; column < linear_count; ++column)
            result.push_back(result.at(column) * result.at(column));
    }
    return result;
}

struct Fit {
    bool success{false};
    Normalization normalization;
    std::vector<double> coefficients;
    std::size_t physical_rank{0};
    std::size_t coefficient_count{0};
    double reciprocal_pivot_ratio{0.0};
    std::string message;
};

Fit fit(
    const std::vector<Row>& rows, const Candidate& candidate,
    const std::vector<std::size_t>& training) {
    Fit result;
    result.normalization = normalization(rows, candidate, training);
    thermox::Matrix physical;
    std::vector<double> targets;
    physical.reserve(training.size());
    targets.reserve(training.size());
    for (const auto index : training) {
        physical.push_back(basis(
            rows.at(index), candidate, result.normalization));
        targets.push_back(value(
            rows.at(index), "expander_inlet_pressure_kpa"));
    }
    result.coefficient_count = physical.front().size();
    const auto rank_check = thermox::solve_dense_least_squares(
        physical, targets);
    result.physical_rank = rank_check.rank;
    result.reciprocal_pivot_ratio =
        rank_check.factorization_quality.reciprocal_pivot_ratio;

    auto augmented = physical;
    auto augmented_targets = targets;
    if (candidate.ridge_lambda > 0.0) {
        const double penalty = std::sqrt(candidate.ridge_lambda);
        for (std::size_t column = 1;
             column < result.coefficient_count; ++column) {
            std::vector<double> row(result.coefficient_count, 0.0);
            row.at(column) = penalty;
            augmented.push_back(std::move(row));
            augmented_targets.push_back(0.0);
        }
    }
    const auto solved = thermox::solve_dense_least_squares(
        std::move(augmented), std::move(augmented_targets));
    result.success = solved.success;
    result.coefficients = solved.x;
    result.message = solved.message;
    return result;
}

double predict(
    const Row& row, const Candidate& candidate, const Fit& fitted) {
    const auto terms = basis(row, candidate, fitted.normalization);
    double pressure = 0.0;
    for (std::size_t column = 0; column < terms.size(); ++column)
        pressure += terms.at(column) * fitted.coefficients.at(column);
    return pressure;
}

struct Metrics {
    std::size_t count{0};
    double mape_percent{0.0};
    double maximum_absolute_relative_error_percent{0.0};
    double mean_bias_percent{0.0};
    double rmse_kpa{0.0};
    bool all_positive_finite{true};
};

Metrics metrics(
    const std::vector<Row>& rows, const Candidate& candidate,
    const Fit& fitted, const std::vector<std::size_t>& evaluation) {
    Metrics result;
    result.count = evaluation.size();
    double squared_error = 0.0;
    for (const auto index : evaluation) {
        const double measured = value(
            rows.at(index), "expander_inlet_pressure_kpa");
        const double predicted = predict(rows.at(index), candidate, fitted);
        result.all_positive_finite = result.all_positive_finite &&
            std::isfinite(predicted) && predicted > 0.0;
        const double relative = (predicted - measured) / measured;
        result.mape_percent += 100.0 * std::abs(relative);
        result.maximum_absolute_relative_error_percent = std::max(
            result.maximum_absolute_relative_error_percent,
            100.0 * std::abs(relative));
        result.mean_bias_percent += 100.0 * relative;
        squared_error += (predicted - measured) * (predicted - measured);
    }
    result.mape_percent /= static_cast<double>(result.count);
    result.mean_bias_percent /= static_cast<double>(result.count);
    result.rmse_kpa = std::sqrt(
        squared_error / static_cast<double>(result.count));
    return result;
}

std::vector<std::size_t> interval(
    std::size_t first, std::size_t last_inclusive) {
    std::vector<std::size_t> result;
    for (std::size_t index = first; index <= last_inclusive; ++index)
        result.push_back(index);
    return result;
}

std::vector<std::size_t> complement(
    const std::vector<std::size_t>& held_out) {
    const std::set<std::size_t> excluded(
        held_out.begin(), held_out.end());
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < 68U; ++index) {
        if (!excluded.contains(index)) result.push_back(index);
    }
    return result;
}

void print_numbers(std::ostream& output, const std::vector<double>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) output << ',';
        output << values.at(index);
    }
    output << ']';
}

void print_metrics(std::ostream& output, const Metrics& value) {
    output << "{\"count\":" << value.count
           << ",\"mape_percent\":" << value.mape_percent
           << ",\"maximum_absolute_relative_error_percent\":"
           << value.maximum_absolute_relative_error_percent
           << ",\"mean_bias_percent\":" << value.mean_bias_percent
           << ",\"rmse_kpa\":" << value.rmse_kpa
           << ",\"all_predictions_positive_finite\":"
           << (value.all_positive_finite ? "true" : "false") << '}';
}

struct FoldResult {
    std::string id;
    Fit fit;
    Metrics metrics;
    bool passed{false};
};

void run(const std::vector<Row>& rows) {
    const std::vector<std::pair<std::string, std::vector<std::size_t>>> folds{
        {"charge_sweeps", interval(0, 28)},
        {"sink_temperature_sweeps", interval(29, 48)},
        {"speed_sweeps", interval(49, 67)},
    };
    const auto full_training = interval(0, 67);
    std::cout << std::setprecision(12)
              << "{\"schema_version\":\"thermox.orc_pressure_formation_study/v1\","
              << "\"contract\":\"pressure_formation_study_contract.json\","
              << "\"classification\":\"training_only_model_discrimination_not_validation\","
              << "\"candidates\":[";
    const Candidate* selected = nullptr;
    Fit selected_full_fit;
    double selected_worst_mape = 0.0;
    std::size_t selected_coefficient_count = 0;
    for (std::size_t candidate_index = 0;
         candidate_index < candidates().size(); ++candidate_index) {
        const auto& candidate = candidates().at(candidate_index);
        if (candidate_index != 0U) std::cout << ',';
        std::vector<FoldResult> results;
        bool candidate_passed = true;
        double worst_mape = 0.0;
        for (const auto& [fold_id, validation] : folds) {
            FoldResult result;
            result.id = fold_id;
            result.fit = fit(rows, candidate, complement(validation));
            if (result.fit.success) {
                result.metrics = metrics(
                    rows, candidate, result.fit, validation);
            }
            result.passed = result.fit.success &&
                result.fit.physical_rank == result.fit.coefficient_count &&
                result.metrics.all_positive_finite &&
                result.metrics.mape_percent <= 8.0 &&
                result.metrics.maximum_absolute_relative_error_percent <= 15.0;
            candidate_passed = candidate_passed && result.passed;
            worst_mape = std::max(worst_mape, result.metrics.mape_percent);
            results.push_back(std::move(result));
        }
        const auto full_fit = fit(rows, candidate, full_training);
        const auto training_metrics = metrics(
            rows, candidate, full_fit, full_training);
        std::cout << "{\"id\":\"" << candidate.id
                  << "\",\"ridge_lambda\":" << candidate.ridge_lambda
                  << ",\"coefficient_count\":"
                  << full_fit.coefficient_count
                  << ",\"full_training_fit\":{\"physical_rank\":"
                  << full_fit.physical_rank
                  << ",\"reciprocal_pivot_ratio\":"
                  << full_fit.reciprocal_pivot_ratio
                  << ",\"normalization_mean\":";
        print_numbers(std::cout, full_fit.normalization.mean);
        std::cout << ",\"normalization_scale\":";
        print_numbers(std::cout, full_fit.normalization.scale);
        std::cout << ",\"coefficients_kpa\":";
        print_numbers(std::cout, full_fit.coefficients);
        std::cout << ",\"metrics\":";
        print_metrics(std::cout, training_metrics);
        std::cout << "},\"blocked_folds\":[";
        for (std::size_t fold_index = 0;
             fold_index < results.size(); ++fold_index) {
            if (fold_index != 0U) std::cout << ',';
            const auto& result = results.at(fold_index);
            std::cout << "{\"id\":\"" << result.id
                      << "\",\"physical_rank\":"
                      << result.fit.physical_rank
                      << ",\"coefficient_count\":"
                      << result.fit.coefficient_count
                      << ",\"metrics\":";
            print_metrics(std::cout, result.metrics);
            std::cout << ",\"acceptance_checks\":{\"full_rank\":"
                      << (result.fit.physical_rank ==
                                  result.fit.coefficient_count
                              ? "true" : "false")
                      << ",\"mape_at_most_8_percent\":"
                      << (result.metrics.mape_percent <= 8.0
                              ? "true" : "false")
                      << ",\"maximum_error_at_most_15_percent\":"
                      << (result.metrics
                                      .maximum_absolute_relative_error_percent <=
                                  15.0
                              ? "true" : "false")
                      << ",\"positive_finite_predictions\":"
                      << (result.metrics.all_positive_finite
                              ? "true" : "false") << '}';
            std::cout << ",\"passed\":"
                      << (result.passed ? "true" : "false") << '}';
        }
        std::cout << "],\"passed_all_folds\":"
                  << (candidate_passed ? "true" : "false") << '}';
        if (candidate_passed &&
            (selected == nullptr ||
             full_fit.coefficient_count < selected_coefficient_count ||
             (full_fit.coefficient_count == selected_coefficient_count &&
              worst_mape < selected_worst_mape))) {
            selected = &candidate;
            selected_full_fit = full_fit;
            selected_worst_mape = worst_mape;
            selected_coefficient_count = full_fit.coefficient_count;
        }
    }
    std::cout << "],\"selection\":";
    if (selected == nullptr) {
        std::cout << "{\"accepted\":false,\"reason\":"
                  << "\"no preregistered candidate passed every blocked fold\"},"
                  << "\"consumed_holdout_diagnostic\":null";
    } else {
        const auto consumed = interval(68, 76);
        const auto diagnostic = metrics(
            rows, *selected, selected_full_fit, consumed);
        std::cout << "{\"accepted\":true,\"candidate_id\":\""
                  << selected->id << "\",\"worst_fold_mape_percent\":"
                  << selected_worst_mape << "},"
                  << "\"consumed_holdout_diagnostic\":{\"independent_validation\":false,"
                  << "\"metrics\":";
        print_metrics(std::cout, diagnostic);
        std::cout << '}';
    }
    std::cout << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::invalid_argument(
                "usage: thermox_orc_1kw_pressure_formation_study <measurements.csv>");
        run(read_csv(argv[1]));
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ORC pressure-formation study failed: "
                  << ex.what() << '\n';
        return 1;
    }
}

#include "thermox/service/engineering_study.hpp"

#include "artifact_payload.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <exception>
#include <set>
#include <string>

namespace thermox::service {

namespace {

using Json = nlohmann::json;

void parse_prediction_solver(
    const Json& value,
    SteadySolverSettings& settings);
void parse_transient_prediction_solver(
    const Json& value,
    TransientSolverSettings& settings);

void parse_artifacts(
    const Json& value,
    SimulationArtifactBundle& artifacts) {
    if (!value.is_object()) {
        throw EngineeringStudyRequestError(
            "artifacts must be an object");
    }
    const std::set<std::string> allowed = {
        "performance_maps", "correlations", "regime_maps"};
    for (const auto& [key, unused] : value.items()) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw EngineeringStudyRequestError(
                "unknown artifacts field: " + key);
        }
    }
    const auto require_declaration = [](const Json& declaration) {
        if (!declaration.is_object()) {
            throw EngineeringStudyRequestError(
                "artifact declaration must be an object");
        }
        const std::set<std::string> declaration_allowed = {
            "id", "schema_version", "revision", "checksum_sha256",
            "payload"};
        for (const auto& [key, unused] : declaration.items()) {
            (void)unused;
            if (!declaration_allowed.contains(key)) {
                throw EngineeringStudyRequestError(
                    "unknown artifact declaration field: " + key);
            }
        }
        for (const char* required : {
                 "id", "schema_version", "revision",
                 "checksum_sha256", "payload"}) {
            if (!declaration.contains(required)) {
                throw EngineeringStudyRequestError(
                    "artifact declaration is missing field: " +
                    std::string(required));
            }
        }
    };
    if (value.contains("performance_maps")) {
        for (const auto& declaration :
             value.at("performance_maps")) {
            require_declaration(declaration);
            artifacts.performance_maps.push_back(
                detail::performance_map_from_payload(
                    declaration.at("id").get<std::string>(),
                    declaration.at("schema_version")
                        .get<std::string>(),
                    declaration.at("revision").get<std::string>(),
                    declaration.at("checksum_sha256")
                        .get<std::string>(),
                    declaration.at("payload").dump()));
        }
    }
    if (value.contains("correlations")) {
        for (const auto& declaration : value.at("correlations")) {
            require_declaration(declaration);
            artifacts.correlations.push_back(
                detail::correlation_from_payload(
                    declaration.at("id").get<std::string>(),
                    declaration.at("schema_version")
                        .get<std::string>(),
                    declaration.at("revision").get<std::string>(),
                    declaration.at("checksum_sha256")
                        .get<std::string>(),
                    declaration.at("payload").dump()));
        }
    }
    if (value.contains("regime_maps")) {
        for (const auto& declaration : value.at("regime_maps")) {
            require_declaration(declaration);
            artifacts.regime_maps.push_back(
                detail::regime_map_from_payload(
                    declaration.at("id").get<std::string>(),
                    declaration.at("schema_version")
                        .get<std::string>(),
                    declaration.at("revision").get<std::string>(),
                    declaration.at("checksum_sha256")
                        .get<std::string>(),
                    declaration.at("payload").dump()));
        }
    }
}

void require_positive_finite(double value, const std::string& field) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw EngineeringStudyRequestError(
            field + " must be a positive finite number");
    }
}

void parse_calibration_solver(
    const Json& value,
    CalibrationSolverSettings& settings) {
    if (!value.is_object()) {
        throw EngineeringStudyRequestError(
            "calibration_solver must be an object");
    }
    const std::set<std::string> allowed = {
        "max_iterations", "finite_difference_fraction",
        "initial_trust_region_radius",
        "minimum_trust_region_radius",
        "maximum_trust_region_radius", "acceptance_ratio",
        "gradient_tolerance", "step_tolerance",
        "objective_relative_tolerance",
        "minimum_continuation_fraction", "continuation_growth",
        "steady_simulation_solver",
        "transient_simulation_solver",
    };
    for (const auto& [key, unused] : value.items()) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw EngineeringStudyRequestError(
                "unknown calibration_solver field: " + key);
        }
    }
    if (value.contains("max_iterations")) {
        settings.max_iterations = value.at("max_iterations").get<int>();
        if (settings.max_iterations <= 0 || settings.max_iterations > 1000) {
            throw EngineeringStudyRequestError(
                "calibration_solver.max_iterations must be in [1, 1000]");
        }
    }
    if (value.contains("finite_difference_fraction")) {
        settings.finite_difference_fraction =
            value.at("finite_difference_fraction").get<double>();
        require_positive_finite(
            settings.finite_difference_fraction,
            "calibration_solver.finite_difference_fraction");
        if (settings.finite_difference_fraction >= 1.0) {
            throw EngineeringStudyRequestError(
                "calibration_solver.finite_difference_fraction must be "
                "less than 1");
        }
    }
    const auto positive_setting =
        [&](const char* name, double& destination) {
        if (!value.contains(name)) return;
        destination = value.at(name).get<double>();
        require_positive_finite(
            destination,
            "calibration_solver." + std::string(name));
    };
    positive_setting(
        "initial_trust_region_radius",
        settings.initial_trust_region_radius);
    positive_setting(
        "minimum_trust_region_radius",
        settings.minimum_trust_region_radius);
    positive_setting(
        "maximum_trust_region_radius",
        settings.maximum_trust_region_radius);
    positive_setting(
        "gradient_tolerance", settings.gradient_tolerance);
    positive_setting("step_tolerance", settings.step_tolerance);
    positive_setting(
        "objective_relative_tolerance",
        settings.objective_relative_tolerance);
    positive_setting(
        "minimum_continuation_fraction",
        settings.minimum_continuation_fraction);
    positive_setting(
        "continuation_growth", settings.continuation_growth);
    if (value.contains("acceptance_ratio")) {
        settings.acceptance_ratio =
            value.at("acceptance_ratio").get<double>();
    }
    if (settings.minimum_trust_region_radius >=
            settings.initial_trust_region_radius ||
        settings.maximum_trust_region_radius <
            settings.initial_trust_region_radius ||
        !std::isfinite(settings.acceptance_ratio) ||
        settings.acceptance_ratio < 0.0 ||
        settings.acceptance_ratio >= 1.0) {
        throw EngineeringStudyRequestError(
            "calibration_solver trust-region settings are invalid");
    }
    if (value.contains("steady_simulation_solver")) {
        parse_prediction_solver(
            value.at("steady_simulation_solver"),
            settings.steady_simulation_solver);
    }
    if (value.contains("transient_simulation_solver")) {
        parse_transient_prediction_solver(
            value.at("transient_simulation_solver"),
            settings.transient_simulation_solver);
    }
}

void parse_prediction_solver(
    const Json& value,
    SteadySolverSettings& settings) {
    if (!value.is_object()) {
        throw EngineeringStudyRequestError(
            "steady solver must be an object");
    }
    const std::set<std::string> allowed = {
        "max_iterations", "residual_tolerance", "step_tolerance",
        "linear_residual_tolerance",
        "structural_decomposition_policy",
        "finite_difference_epsilon", "min_damping",
        "damping_reduction", "sufficient_decrease",
        "max_line_search_steps", "globalization_policy",
        "trust_region_initial_radius",
        "trust_region_minimum_radius",
        "trust_region_maximum_radius",
        "trust_region_acceptance_threshold",
        "max_trust_region_steps", "continuation_enabled",
        "continuation_initial_step",
        "continuation_minimum_step", "continuation_step_growth",
        "continuation_step_reduction",
        "continuation_maximum_stages",
    };
    for (const auto& [key, unused] : value.items()) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw EngineeringStudyRequestError(
                "unknown steady prediction solver field: " + key);
        }
    }
    settings.continuation_enabled =
        value.value("continuation_enabled", settings.continuation_enabled);
    settings.max_iterations =
        value.value("max_iterations", settings.max_iterations);
    settings.residual_tolerance = value.value(
        "residual_tolerance", settings.residual_tolerance);
    settings.step_tolerance =
        value.value("step_tolerance", settings.step_tolerance);
    settings.linear_residual_tolerance = value.value(
        "linear_residual_tolerance",
        settings.linear_residual_tolerance);
    if (value.contains("structural_decomposition_policy")) {
        settings.structural_decomposition_policy =
            structural_decomposition_policy_from_string(
                value.at("structural_decomposition_policy")
                    .get<std::string>());
    }
    settings.finite_difference_epsilon = value.value(
        "finite_difference_epsilon",
        settings.finite_difference_epsilon);
    settings.min_damping =
        value.value("min_damping", settings.min_damping);
    settings.damping_reduction = value.value(
        "damping_reduction", settings.damping_reduction);
    settings.sufficient_decrease = value.value(
        "sufficient_decrease", settings.sufficient_decrease);
    settings.max_line_search_steps = value.value(
        "max_line_search_steps", settings.max_line_search_steps);
    if (value.contains("globalization_policy")) {
        settings.globalization_policy =
            globalization_policy_from_string(
                value.at("globalization_policy")
                    .get<std::string>());
    }
    settings.trust_region_initial_radius = value.value(
        "trust_region_initial_radius",
        settings.trust_region_initial_radius);
    settings.trust_region_minimum_radius = value.value(
        "trust_region_minimum_radius",
        settings.trust_region_minimum_radius);
    settings.trust_region_maximum_radius = value.value(
        "trust_region_maximum_radius",
        settings.trust_region_maximum_radius);
    settings.trust_region_acceptance_threshold = value.value(
        "trust_region_acceptance_threshold",
        settings.trust_region_acceptance_threshold);
    settings.max_trust_region_steps = value.value(
        "max_trust_region_steps",
        settings.max_trust_region_steps);
    settings.continuation_initial_step = value.value(
        "continuation_initial_step",
        settings.continuation_initial_step);
    settings.continuation_minimum_step = value.value(
        "continuation_minimum_step",
        settings.continuation_minimum_step);
    settings.continuation_step_growth = value.value(
        "continuation_step_growth",
        settings.continuation_step_growth);
    settings.continuation_step_reduction = value.value(
        "continuation_step_reduction",
        settings.continuation_step_reduction);
    settings.continuation_maximum_stages = value.value(
        "continuation_maximum_stages",
        settings.continuation_maximum_stages);
}

void parse_transient_prediction_solver(
    const Json& value,
    TransientSolverSettings& settings) {
    if (!value.is_object()) {
        throw EngineeringStudyRequestError(
            "transient_prediction_solver must be an object");
    }
    const std::set<std::string> allowed = {
        "start_time", "initial_step", "min_step", "max_step",
        "absolute_tolerance", "relative_tolerance", "max_steps",
        "max_consecutive_rejections", "maximum_order",
        "compute_consistent_initial_conditions", "nonlinear_solver",
    };
    for (const auto& [key, unused] : value.items()) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw EngineeringStudyRequestError(
                "unknown transient prediction solver field: " + key);
        }
    }
    const auto positive = [&](const char* name, double& destination) {
        if (!value.contains(name)) return;
        destination = value.at(name).get<double>();
        require_positive_finite(
            destination,
            "transient_prediction_solver." + std::string(name));
    };
    if (value.contains("start_time")) {
        settings.start_time = value.at("start_time").get<double>();
        if (!std::isfinite(settings.start_time)) {
            throw EngineeringStudyRequestError(
                "transient_prediction_solver.start_time must be finite");
        }
    }
    positive("initial_step", settings.initial_step);
    positive("min_step", settings.min_step);
    positive("max_step", settings.max_step);
    positive("absolute_tolerance", settings.absolute_tolerance);
    positive("relative_tolerance", settings.relative_tolerance);
    if (value.contains("max_steps")) {
        settings.max_steps = value.at("max_steps").get<int>();
    }
    if (value.contains("max_consecutive_rejections")) {
        settings.max_consecutive_rejections =
            value.at("max_consecutive_rejections").get<int>();
    }
    if (value.contains("maximum_order")) {
        settings.maximum_order = value.at("maximum_order").get<int>();
    }
    if (value.contains("compute_consistent_initial_conditions")) {
        settings.compute_consistent_initial_conditions = value.at(
            "compute_consistent_initial_conditions").get<bool>();
    }
    if (settings.min_step > settings.max_step ||
        settings.max_steps <= 0 ||
        settings.max_consecutive_rejections <= 0 ||
        settings.maximum_order < 1 || settings.maximum_order > 2) {
        throw EngineeringStudyRequestError(
            "transient_prediction_solver settings are invalid");
    }
    if (value.contains("nonlinear_solver")) {
        parse_prediction_solver(
            value.at("nonlinear_solver"),
            settings.nonlinear_solver);
    }
}

}  // namespace

EngineeringStudyRequest parse_engineering_study_request_json(
    const std::string& text) {
    try {
        const auto root = Json::parse(text);
        if (!root.is_object()) {
            throw EngineeringStudyRequestError(
                "engineering-study request root must be an object");
        }
        const std::set<std::string> allowed = {
            "schema_version", "model_document", "calibration_id",
            "calibration_solver", "steady_prediction_solver",
            "transient_prediction_solver", "prediction_cases",
            "artifacts",
        };
        for (const auto& [key, unused] : root.items()) {
            (void)unused;
            if (!allowed.contains(key)) {
                throw EngineeringStudyRequestError(
                    "unknown engineering-study field: " + key);
            }
        }
        if (root.value("schema_version", "") !=
            engineering_study_request_schema_v2) {
            throw EngineeringStudyRequestError(
                "schema_version must be " +
                std::string(engineering_study_request_schema_v2));
        }

        EngineeringStudyRequest request;
        request.model_json = root.at("model_document").dump();
        request.calibration_id =
            root.at("calibration_id").get<std::string>();
        if (root.contains("artifacts")) {
            parse_artifacts(root.at("artifacts"), request.artifacts);
        }
        if (root.contains("calibration_solver")) {
            parse_calibration_solver(
                root.at("calibration_solver"),
                request.calibration_solver);
        }
        if (root.contains("steady_prediction_solver")) {
            parse_prediction_solver(
                root.at("steady_prediction_solver"),
                request.steady_prediction_solver);
        }
        if (root.contains("transient_prediction_solver")) {
            parse_transient_prediction_solver(
                root.at("transient_prediction_solver"),
                request.transient_prediction_solver);
        }

        for (const auto& value : root.at("prediction_cases")) {
            StudyPredictionCase prediction;
            prediction.case_id = value.at("case_id").get<std::string>();
            for (const auto& item : value.at("observations")) {
                StudyObservation observation;
                observation.id = item.at("id").get<std::string>();
                observation.target =
                    item.at("target").get<std::string>();
                observation.dimension =
                    item.at("dimension").get<std::string>();
                observation.measured_si =
                    item.at("measured_si").get<double>();
                observation.sigma_si =
                    item.at("sigma_si").get<double>();
                if (item.contains("time_si")) {
                    observation.time_si =
                        item.at("time_si").get<double>();
                }
                if (!std::isfinite(observation.measured_si)) {
                    throw EngineeringStudyRequestError(
                        "prediction measured_si must be finite");
                }
                if (observation.time_si.has_value() &&
                    (!std::isfinite(*observation.time_si) ||
                     *observation.time_si < 0.0)) {
                    throw EngineeringStudyRequestError(
                        "prediction time_si must be non-negative and "
                        "finite");
                }
                require_positive_finite(
                    observation.sigma_si,
                    "prediction sigma_si");
                prediction.observations.push_back(
                    std::move(observation));
            }
            request.prediction_cases.push_back(
                std::move(prediction));
        }
        return request;
    } catch (const EngineeringStudyRequestError&) {
        throw;
    } catch (const std::exception& ex) {
        throw EngineeringStudyRequestError(
            std::string("invalid engineering-study request: ") +
            ex.what());
    }
}

EngineeringStudyResponse evaluate_engineering_study_json(
    const std::string& text) {
    SimulationService service;
    return service.run_engineering_study(
        parse_engineering_study_request_json(text));
}

}  // namespace thermox::service

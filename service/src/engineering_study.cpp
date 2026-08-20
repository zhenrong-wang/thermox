#include "thermox/service/engineering_study.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <exception>
#include <string>

namespace thermox::service {

namespace {

using Json = nlohmann::json;

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
}

void parse_prediction_solver(
    const Json& value,
    SteadySolverSettings& settings) {
    if (!value.is_object()) {
        throw EngineeringStudyRequestError(
            "prediction_solver must be an object");
    }
    settings.continuation_enabled =
        value.value("continuation_enabled", settings.continuation_enabled);
    if (value.contains("maximum_iterations")) {
        settings.max_iterations =
            value.at("maximum_iterations").get<int>();
        if (settings.max_iterations <= 0 ||
            settings.max_iterations > 10000) {
            throw EngineeringStudyRequestError(
                "prediction_solver.maximum_iterations must be in [1, 10000]");
        }
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
        if (root.value("schema_version", "") !=
            engineering_study_request_schema_v1) {
            throw EngineeringStudyRequestError(
                "schema_version must be " +
                std::string(engineering_study_request_schema_v1));
        }

        EngineeringStudyRequest request;
        request.model_json = root.at("model_document").dump();
        request.calibration_id =
            root.at("calibration_id").get<std::string>();
        if (root.contains("calibration_solver")) {
            parse_calibration_solver(
                root.at("calibration_solver"),
                request.calibration_solver);
        }
        if (root.contains("prediction_solver")) {
            parse_prediction_solver(
                root.at("prediction_solver"),
                request.prediction_solver);
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
                if (!std::isfinite(observation.measured_si)) {
                    throw EngineeringStudyRequestError(
                        "prediction measured_si must be finite");
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

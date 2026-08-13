#include "thermox/service/iso2314_equivalent_cooling.hpp"

#include <nlohmann/json.hpp>

#include <iomanip>
#include <sstream>

namespace thermox::service {

namespace {

using Json = nlohmann::json;

void json_string(std::ostream& out, const std::string& value) {
    out << Json(value).dump();
}

}  // namespace

Iso2314EquivalentCoolingRequest
parse_iso2314_equivalent_cooling_request_json(const std::string& text) {
    try {
        const auto root = Json::parse(text);
        if (!root.is_object() || root.value("schema_version", "") !=
                iso2314_equivalent_cooling_schema_v1) {
            throw Iso2314EquivalentCoolingRequestError(
                "schema_version must be " +
                std::string(iso2314_equivalent_cooling_schema_v1));
        }
        Iso2314EquivalentCoolingRequest request;
        request.id = root.at("id").get<std::string>();
        request.standard_reference = root.value(
            "standard_reference", request.standard_reference);
        auto& input = request.calculation;
        input.compressor_inlet_mass_flow_kg_s =
            root.at("compressor_inlet_mass_flow_kg_s").get<double>();
        input.compressor_inlet_specific_enthalpy_j_kg =
            root.at("compressor_inlet_specific_enthalpy_j_kg")
                .get<double>();
        input.compressor_discharge_specific_enthalpy_j_kg =
            root.at("compressor_discharge_specific_enthalpy_j_kg")
                .get<double>();
        const int determination_count =
            (root.contains("extractions") ? 1 : 0) +
            (root.contains("compressor_power_w") ? 1 : 0) +
            (root.contains("manufacturer_md") ? 1 : 0);
        if (determination_count != 1) {
            throw Iso2314EquivalentCoolingRequestError(
                "declare exactly one of extractions, compressor_power_w, "
                "or manufacturer_md");
        }
        if (root.contains("compressor_power_w")) {
            input.compressor_power_w =
                root.at("compressor_power_w").get<double>();
        }
        if (root.contains("manufacturer_md")) {
            input.manufacturer_md =
                root.at("manufacturer_md").get<double>();
        }
        if (root.contains("extractions")) {
            for (const auto& value : root.at("extractions")) {
                input.extractions.push_back({
                    value.at("id").get<std::string>(),
                    value.at("mass_flow_kg_s").get<double>(),
                    value.at("specific_enthalpy_j_kg").get<double>(),
                });
            }
        }
        return request;
    } catch (const Iso2314EquivalentCoolingRequestError&) {
        throw;
    } catch (const std::exception& ex) {
        throw Iso2314EquivalentCoolingRequestError(
            std::string("invalid ISO 2314 equivalent-cooling request: ") +
            ex.what());
    }
}

Iso2314EquivalentCoolingResponse evaluate_iso2314_equivalent_cooling(
    const Iso2314EquivalentCoolingRequest& request) {
    if (request.id.empty() || request.standard_reference.empty()) {
        throw Iso2314EquivalentCoolingRequestError(
            "id and standard_reference are required");
    }
    Iso2314EquivalentCoolingResponse response;
    response.id = request.id;
    response.standard_reference = request.standard_reference;
    response.calculation = physics::calculate_iso2314_equivalent_cooling(
        request.calculation);
    switch (response.calculation.determination) {
        case physics::Iso2314CoolingDetermination::extraction_schedule:
            response.evidence_basis = "provided_extraction_states";
            response.actual_cooling_flow_identified = true;
            response.limitations.push_back(
                "Equivalent extraction flow is a work-equivalent quantity "
                "and generally differs from the sum of physical extraction "
                "flows.");
            break;
        case physics::Iso2314CoolingDetermination::compressor_power:
            response.evidence_basis = "provided_compressor_power";
            response.limitations.push_back(
                "Compressor power determines the ISO-equivalent flow but "
                "does not identify physical extraction locations or flows.");
            break;
        case physics::Iso2314CoolingDetermination::manufacturer_md:
            response.evidence_basis = "provided_manufacturer_md";
            response.limitations.push_back(
                "The supplied md determines an equivalent flow but does not "
                "independently identify physical extraction states.");
            break;
    }
    response.limitations.push_back(
        "Q_ex depends on the declared enthalpy reference and is intended for "
        "the consistent ISO 2314 combustion-balance formulation.");
    response.limitations.push_back(
        "This calculation does not by itself determine turbine inlet "
        "temperature; the remaining ISO 2314 equation 35 boundary terms are "
        "also required.");
    return response;
}

Iso2314EquivalentCoolingResponse
evaluate_iso2314_equivalent_cooling_json(const std::string& text) {
    return evaluate_iso2314_equivalent_cooling(
        parse_iso2314_equivalent_cooling_request_json(text));
}

std::string serialize_iso2314_equivalent_cooling_response_json(
    const Iso2314EquivalentCoolingResponse& response) {
    std::ostringstream out;
    out << std::setprecision(17) << "{\n  \"schema_version\": ";
    json_string(out, response.schema_version);
    out << ",\n  \"id\": ";
    json_string(out, response.id);
    out << ",\n  \"standard_reference\": ";
    json_string(out, response.standard_reference);
    out << ",\n  \"determination\": ";
    json_string(out, physics::to_string(response.calculation.determination));
    out << ",\n  \"evidence_basis\": ";
    json_string(out, response.evidence_basis);
    out << ",\n  \"actual_cooling_flow_identified\": "
        << (response.actual_cooling_flow_identified ? "true" : "false")
        << ",\n  \"calculation\": {"
        << "\"no_extraction_compressor_power_w\": "
        << response.calculation.no_extraction_compressor_power_w
        << ", \"actual_compressor_power_w\": "
        << response.calculation.actual_compressor_power_w
        << ", \"actual_extraction_mass_flow_kg_s\": "
        << response.calculation.actual_extraction_mass_flow_kg_s
        << ", \"equivalent_compressor_mass_flow_kg_s\": "
        << response.calculation.equivalent_compressor_mass_flow_kg_s
        << ", \"equivalent_extraction_mass_flow_kg_s\": "
        << response.calculation.equivalent_extraction_mass_flow_kg_s
        << ", \"relative_equivalent_flow_difference_md\": "
        << response.calculation.relative_equivalent_flow_difference_md
        << ", \"equivalent_extraction_energy_w\": "
        << response.calculation.equivalent_extraction_energy_w
        << "},\n  \"limitations\": [";
    for (std::size_t i = 0; i < response.limitations.size(); ++i) {
        if (i != 0) out << ", ";
        json_string(out, response.limitations[i]);
    }
    out << "]\n}\n";
    return out.str();
}

}  // namespace thermox::service

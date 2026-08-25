#include "thermox/physics/tabulated_incompressible_package.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace thermox::physics {

namespace {

PropertyResult property_error(
    PropertyStatus status, std::string message) {
    return {{}, status, std::move(message)};
}

double interpolate(double a, double b, double fraction) {
    return a + fraction * (b - a);
}

}  // namespace

TabulatedIncompressiblePropertyPackage::
    TabulatedIncompressiblePropertyPackage(
        std::string implementation_name,
        std::string implementation_version,
        std::vector<IncompressiblePropertySample> samples,
        double maximum_pressure_pa,
        double reference_pressure_pa)
    : name_(std::move(implementation_name)),
      version_(std::move(implementation_version)),
      samples_(std::move(samples)),
      limits_{0.0, maximum_pressure_pa,
              samples_.empty() ? 0.0
                               : samples_.front().temperature_k,
              samples_.empty() ? 0.0
                               : samples_.back().temperature_k},
      reference_pressure_pa_(reference_pressure_pa) {
    if (name_.empty() || version_.empty() || samples_.size() < 2U ||
        !std::isfinite(maximum_pressure_pa) ||
        maximum_pressure_pa <= 0.0 ||
        !std::isfinite(reference_pressure_pa_) ||
        reference_pressure_pa_ <= 0.0) {
        throw std::invalid_argument(
            "tabulated incompressible property declaration is invalid");
    }
    for (std::size_t index = 0; index < samples_.size(); ++index) {
        const auto& sample = samples_[index];
        const double values[]{
            sample.temperature_k, sample.density_kg_m3,
            sample.cp_j_kg_k, sample.viscosity_pa_s,
            sample.thermal_conductivity_w_m_k};
        if (!std::all_of(
                std::begin(values), std::end(values),
                [](double value) {
                    return std::isfinite(value) && value > 0.0;
                }) ||
            (index > 0U &&
             sample.temperature_k <=
                 samples_[index - 1U].temperature_k)) {
            throw std::invalid_argument(
                "tabulated incompressible samples must be positive, "
                "finite, and strictly temperature ordered");
        }
    }
}

std::string_view TabulatedIncompressiblePropertyPackage::name()
    const noexcept {
    return name_;
}

std::string_view TabulatedIncompressiblePropertyPackage::version()
    const noexcept {
    return version_;
}

PropertyLimits TabulatedIncompressiblePropertyPackage::limits()
    const noexcept {
    return limits_;
}

bool TabulatedIncompressiblePropertyPackage::supports(
    PropertyCapability capability) const noexcept {
    switch (capability) {
        case PropertyCapability::state_pt:
        case PropertyCapability::state_ph:
        case PropertyCapability::state_ps:
        case PropertyCapability::transport:
            return true;
        case PropertyCapability::state_ph_derivatives:
        case PropertyCapability::saturation_p:
        case PropertyCapability::surface_tension:
            return false;
    }
    return false;
}

std::size_t TabulatedIncompressiblePropertyPackage::interval(
    double temperature_k) const {
    const auto upper = std::upper_bound(
        samples_.begin(), samples_.end(), temperature_k,
        [](double value, const auto& sample) {
            return value < sample.temperature_k;
        });
    if (upper == samples_.begin()) return 0U;
    if (upper == samples_.end()) return samples_.size() - 2U;
    return static_cast<std::size_t>(
        std::distance(samples_.begin(), upper) - 1);
}

double TabulatedIncompressiblePropertyPackage::enthalpy_at(
    double temperature_k) const {
    double enthalpy = 0.0;
    const std::size_t last = interval(temperature_k);
    for (std::size_t index = 0; index <= last; ++index) {
        const auto& low = samples_[index];
        const auto& high = samples_[index + 1U];
        const double end =
            index == last ? temperature_k : high.temperature_k;
        const double delta = end - low.temperature_k;
        const double slope =
            (high.cp_j_kg_k - low.cp_j_kg_k) /
            (high.temperature_k - low.temperature_k);
        enthalpy += low.cp_j_kg_k * delta +
                    0.5 * slope * delta * delta;
    }
    return enthalpy;
}

double TabulatedIncompressiblePropertyPackage::entropy_at(
    double temperature_k) const {
    double entropy = 0.0;
    const std::size_t last = interval(temperature_k);
    for (std::size_t index = 0; index <= last; ++index) {
        const auto& low = samples_[index];
        const auto& high = samples_[index + 1U];
        const double end =
            index == last ? temperature_k : high.temperature_k;
        const double delta = end - low.temperature_k;
        const double slope =
            (high.cp_j_kg_k - low.cp_j_kg_k) /
            (high.temperature_k - low.temperature_k);
        entropy += slope * delta +
            (low.cp_j_kg_k - slope * low.temperature_k) *
                std::log(end / low.temperature_k);
    }
    return entropy;
}

PropertyResult TabulatedIncompressiblePropertyPackage::state_at(
    double pressure_pa, double temperature_k) const {
    const std::size_t index = interval(temperature_k);
    const auto& low = samples_[index];
    const auto& high = samples_[index + 1U];
    const double fraction =
        (temperature_k - low.temperature_k) /
        (high.temperature_k - low.temperature_k);
    ThermodynamicState state;
    state.pressure_pa = pressure_pa;
    state.temperature_k = temperature_k;
    state.density_kg_m3 = interpolate(
        low.density_kg_m3, high.density_kg_m3, fraction);
    state.cp_j_kg_k = interpolate(
        low.cp_j_kg_k, high.cp_j_kg_k, fraction);
    state.cv_j_kg_k = state.cp_j_kg_k;
    state.viscosity_pa_s = interpolate(
        low.viscosity_pa_s, high.viscosity_pa_s, fraction);
    state.thermal_conductivity_w_m_k = interpolate(
        low.thermal_conductivity_w_m_k,
        high.thermal_conductivity_w_m_k, fraction);
    const double pressure_enthalpy =
        (pressure_pa - reference_pressure_pa_) /
        state.density_kg_m3;
    state.enthalpy_j_kg =
        enthalpy_at(temperature_k) + pressure_enthalpy;
    state.internal_energy_j_kg =
        state.enthalpy_j_kg - pressure_pa / state.density_kg_m3;
    state.entropy_j_kg_k = entropy_at(temperature_k);
    state.speed_of_sound_m_s = 0.0;
    state.vapor_quality = -1.0;
    state.phase = Phase::liquid;
    return {state, PropertyStatus::success, {}};
}

PropertyResult TabulatedIncompressiblePropertyPackage::state_pt(
    double pressure_pa, double temperature_k) const {
    if (!std::isfinite(pressure_pa) || pressure_pa <= 0.0 ||
        !std::isfinite(temperature_k)) {
        return property_error(
            PropertyStatus::invalid_input,
            "pressure and temperature must be finite and pressure positive");
    }
    if (pressure_pa > limits_.maximum_pressure_pa ||
        temperature_k < limits_.minimum_temperature_k ||
        temperature_k > limits_.maximum_temperature_k) {
        return property_error(
            PropertyStatus::out_of_range,
            "state is outside the tabulated property range");
    }
    return state_at(pressure_pa, temperature_k);
}

PropertyResult TabulatedIncompressiblePropertyPackage::state_ph(
    double pressure_pa, double enthalpy_j_kg) const {
    if (!std::isfinite(pressure_pa) || pressure_pa <= 0.0 ||
        !std::isfinite(enthalpy_j_kg)) {
        return property_error(
            PropertyStatus::invalid_input,
            "pressure and enthalpy must be finite and pressure positive");
    }
    if (pressure_pa > limits_.maximum_pressure_pa) {
        return property_error(
            PropertyStatus::out_of_range,
            "pressure is outside the tabulated property range");
    }
    const auto low_state =
        state_at(pressure_pa, limits_.minimum_temperature_k);
    const auto high_state =
        state_at(pressure_pa, limits_.maximum_temperature_k);
    if (enthalpy_j_kg < low_state.state.enthalpy_j_kg ||
        enthalpy_j_kg > high_state.state.enthalpy_j_kg) {
        return property_error(
            PropertyStatus::out_of_range,
            "enthalpy is outside the tabulated property range");
    }
    double low = limits_.minimum_temperature_k;
    double high = limits_.maximum_temperature_k;
    for (int iteration = 0; iteration < 80; ++iteration) {
        const double middle = 0.5 * (low + high);
        if (state_at(pressure_pa, middle).state.enthalpy_j_kg <
            enthalpy_j_kg) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return state_at(pressure_pa, 0.5 * (low + high));
}

PropertyResult TabulatedIncompressiblePropertyPackage::state_ps(
    double pressure_pa, double entropy_j_kg_k) const {
    if (!std::isfinite(pressure_pa) || pressure_pa <= 0.0 ||
        !std::isfinite(entropy_j_kg_k)) {
        return property_error(
            PropertyStatus::invalid_input,
            "pressure and entropy must be finite and pressure positive");
    }
    if (pressure_pa > limits_.maximum_pressure_pa ||
        entropy_j_kg_k < entropy_at(limits_.minimum_temperature_k) ||
        entropy_j_kg_k > entropy_at(limits_.maximum_temperature_k)) {
        return property_error(
            PropertyStatus::out_of_range,
            "state is outside the tabulated property range");
    }
    double low = limits_.minimum_temperature_k;
    double high = limits_.maximum_temperature_k;
    for (int iteration = 0; iteration < 80; ++iteration) {
        const double middle = 0.5 * (low + high);
        if (entropy_at(middle) < entropy_j_kg_k) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return state_at(pressure_pa, 0.5 * (low + high));
}

SaturationResult TabulatedIncompressiblePropertyPackage::saturation_p(
    double) const {
    return {
        {}, {}, PropertyStatus::unsupported,
        "tabulated incompressible liquids do not expose a saturation pair"};
}

std::shared_ptr<const PropertyPackage>
make_sandia_solar_salt_property_package() {
    constexpr double fahrenheit_offset = 32.0;
    constexpr double fahrenheit_scale = 5.0 / 9.0;
    constexpr double rankine_offset = 273.15;
    constexpr double density_scale = 16.01846337396014;
    constexpr double cp_scale = 4186.8;
    constexpr double viscosity_scale =
        0.45359237 / (0.3048 * 3600.0);
    constexpr double conductivity_scale = 1.730734666371391;
    const double temperature_f[] = {
        500, 550, 600, 650, 700, 750, 800,
        850, 900, 950, 1000, 1050, 1100};
    const double density[] = {
        120.10, 118.98, 117.87, 116.76, 115.65, 114.54,
        113.43, 112.32, 111.21, 110.10, 108.99, 107.88,
        106.77};
    const double cp[] = {
        0.356, 0.358, 0.359, 0.360, 0.361, 0.362, 0.363,
        0.364, 0.366, 0.367, 0.368, 0.369, 0.370};
    const double viscosity[] = {
        10.5058, 8.6073, 7.0853, 5.8940, 4.9873, 4.3196,
        3.8450, 3.5175, 3.2913, 3.1206, 2.9596, 2.7623,
        2.4830};
    const double conductivity[] = {
        0.284557, 0.287692, 0.290827, 0.293962, 0.297097,
        0.300232, 0.303367, 0.306502, 0.309637, 0.312771,
        0.315906, 0.319041, 0.322176};
    std::vector<IncompressiblePropertySample> samples;
    samples.reserve(std::size(temperature_f));
    for (std::size_t index = 0; index < std::size(temperature_f);
         ++index) {
        samples.push_back({
            (temperature_f[index] - fahrenheit_offset) *
                    fahrenheit_scale +
                rankine_offset,
            density[index] * density_scale,
            cp[index] * cp_scale,
            viscosity[index] * viscosity_scale,
            conductivity[index] * conductivity_scale});
    }
    return std::make_shared<TabulatedIncompressiblePropertyPackage>(
        "sandia-solar-salt-table",
        "SAND2001-2100-table-1-1/v1", std::move(samples));
}

}  // namespace thermox::physics

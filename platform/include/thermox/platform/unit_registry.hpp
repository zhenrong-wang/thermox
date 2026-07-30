#pragma once

#include "thermox/platform/model_document.hpp"

#include <map>
#include <string>
#include <vector>

namespace thermox::platform {

struct DisplayUnitDescriptor {
    std::string symbol;
    double scale_from_si{1.0};
    double offset_from_si{0.0};
};

struct AcceptedUnitDescriptor {
    std::string symbol;
    std::vector<std::string> aliases;
    double scale_to_si{1.0};
    double offset_to_si{0.0};
};

struct DimensionUnitDescriptor {
    std::string dimension;
    std::string canonical_unit;
    DisplayUnitDescriptor si_display;
    DisplayUnitDescriptor engineering_display;
    std::vector<AcceptedUnitDescriptor> accepted_units;
};

class UnitRegistry {
public:
    void register_dimension(DimensionUnitDescriptor descriptor);
    [[nodiscard]] ScalarValue convert(
        double value,
        const std::string& unit,
        const std::string& field_name = {}) const;
    [[nodiscard]] const DimensionUnitDescriptor&
    require_dimension(const std::string& dimension) const;
    [[nodiscard]] std::vector<DimensionUnitDescriptor>
    descriptors() const;

private:
    struct Conversion {
        std::string dimension;
        std::string canonical_unit;
        double scale_to_si{1.0};
        double offset_to_si{0.0};
    };

    std::map<std::string, DimensionUnitDescriptor> dimensions_;
    std::map<std::string, Conversion> conversions_;
};

UnitRegistry make_default_unit_registry();

}  // namespace thermox::platform

#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thermox::platform {

namespace {

ComponentModelDescriptor boundary_descriptor(
    std::string kind,
    std::vector<PortModelDescriptor> ports,
    bool supports_transient,
    std::string boundary_role) {
    ComponentModelDescriptor descriptor;
    descriptor.kind = std::move(kind);
    descriptor.version = "1.0.0";
    descriptor.system_boundary_role = std::move(boundary_role);
    descriptor.ports = std::move(ports);
    descriptor.supports_transient = supports_transient;
    return descriptor;
}

class FixedCompositionMaterialSourceModel final
    : public ComponentModel {
public:
    FixedCompositionMaterialSourceModel() {
        descriptor_ = boundary_descriptor(
            "source.material.fixed_composition",
            {{"outlet", "material", "out"}}, false, "source");
        descriptor_.parameters = {{
            "mass_fraction[{species}]",
            "dimensionless",
            false,
            0.0,
            0.0,
            1.0,
            true,
            true,
        }};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        using component_model_support::require_port_species;
        using component_model_support::require_port_variable;

        const auto species =
            require_port_species(context, "outlet");
        if (species.empty()) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' fixed-composition source requires a nonempty "
                "material species basis");
        }
        std::vector<double> fractions;
        std::vector<std::size_t> flows;
        std::set<std::string> expected_parameters;
        fractions.reserve(species.size());
        flows.reserve(species.size());
        double sum = 0.0;
        for (const auto& name : species) {
            const auto parameter =
                "mass_fraction[" + name + "]";
            expected_parameters.insert(parameter);
            const double fraction =
                component_model_support::parameter_or(
                    context.component, parameter, 0.0);
            fractions.push_back(fraction);
            sum += fraction;
            flows.push_back(require_port_variable(
                context, "outlet.m_dot[" + name + "]"));
        }
        for (const auto& [name, _] :
             context.component.parameters) {
            if (!expected_parameters.contains(name)) {
                throw std::invalid_argument(
                    "component '" + context.component.id +
                    "' fixed-composition source parameter "
                    "references a species outside its material "
                    "basis: " + name);
            }
        }
        if (!std::isfinite(sum) ||
            std::abs(sum - 1.0) > 1.0e-10) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material source mass fractions must sum to one");
        }

        const std::string prefix =
            "component." + context.component.id +
            ".composition.";
        const auto reference = static_cast<std::size_t>(
            std::distance(
                fractions.begin(),
                std::max_element(
                    fractions.begin(), fractions.end())));
        for (std::size_t row = 0;
             row < species.size(); ++row) {
            if (row == reference) continue;
            system.add_linear_equation(
                prefix + species[row],
                {{flows[row], fractions[reference]},
                 {flows[reference], -fractions[row]}},
                0.0, 100.0);
        }
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_boundary_component_models(ComponentRegistry& registry) {
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "source.fluid.boundary",
            {{"outlet", "fluid", "out"}}, true, "source")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "sink.fluid.boundary",
            {{"inlet", "fluid", "in"}}, true, "sink")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "source.material.boundary",
            {{"outlet", "material", "out"}}, false, "source")));
    registry.register_model(
        std::make_shared<FixedCompositionMaterialSourceModel>());
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "sink.material.boundary",
            {{"inlet", "material", "in"}}, false, "sink")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "source.heat.boundary",
            {{"outlet", "heat", "out"}}, true, "source")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "sink.heat.boundary",
            {{"inlet", "heat", "in"}}, true, "sink")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "source.shaft.boundary",
            {{"outlet", "shaft", "out"}}, true, "source")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "sink.shaft.boundary",
            {{"inlet", "shaft", "in"}}, true, "sink")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "source.electrical.boundary",
            {{"outlet", "electrical", "out"}}, false, "source")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "sink.electrical.boundary",
            {{"inlet", "electrical", "in"}}, false, "sink")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "source.signal.boundary",
            {{"outlet", "signal", "out"}}, true, "source")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "sink.signal.boundary",
            {{"inlet", "signal", "in"}}, true, "sink")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "source.control.boundary",
            {{"outlet", "control", "out"}}, true, "source")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "sink.control.boundary",
            {{"inlet", "control", "in"}}, true, "sink")));
}

}  // namespace thermox::platform

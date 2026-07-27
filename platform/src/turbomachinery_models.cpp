#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace thermox::platform {

namespace {

using component_model_support::property_failure;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::required_parameter;

ComponentModelDescriptor make_descriptor(
    std::string kind,
    std::string shaft_direction) {
    ComponentModelDescriptor out;
    out.kind = std::move(kind);
    out.version = "1.0.0";
    out.ports = {
        {"inlet", "fluid", "in"},
        {"outlet", "fluid", "out"},
        {"shaft", "shaft", std::move(shaft_direction)}};
    out.parameters = {
        {"pressure_ratio", "dimensionless", true, std::nullopt,
         1.0, std::numeric_limits<double>::infinity(), false, true},
        {"eta_is", "dimensionless", true, std::nullopt,
         0.0, 1.0, false, true}};
    out.required_property_capabilities = {
        physics::PropertyCapability::state_ph,
        physics::PropertyCapability::state_ps};
    return out;
}

void add_turbomachinery_equations(
    const ComponentCompileContext& context,
    EquationSystemBuilder& system,
    bool compressor) {
    const double pressure_ratio =
        required_parameter(context.component, "pressure_ratio");
    const double eta_is =
        required_parameter(context.component, "eta_is");
    const auto properties =
        require_property_package(context, "inlet");
    if (properties !=
        require_property_package(context, "outlet")) {
        throw std::invalid_argument(
            "component '" + context.component.id +
            "' inlet and outlet must use the same medium");
    }

    const auto inlet_m =
        require_port_variable(context, "inlet.m_dot");
    const auto inlet_p =
        require_port_variable(context, "inlet.p");
    const auto inlet_h =
        require_port_variable(context, "inlet.h");
    const auto outlet_m =
        require_port_variable(context, "outlet.m_dot");
    const auto outlet_p =
        require_port_variable(context, "outlet.p");
    const auto outlet_h =
        require_port_variable(context, "outlet.h");
    const auto shaft_w =
        require_port_variable(context, "shaft.W_dot");
    const std::string prefix =
        "component." + context.component.id + ".";

    system.add_linear_equation(
        prefix + "mass_continuity",
        {{outlet_m, 1.0}, {inlet_m, -1.0}},
        0.0, 100.0);
    system.add_linear_equation(
        prefix + "pressure_ratio",
        compressor
            ? std::vector<LinearTerm>{
                  {outlet_p, 1.0},
                  {inlet_p, -pressure_ratio}}
            : std::vector<LinearTerm>{
                  {inlet_p, 1.0},
                  {outlet_p, -pressure_ratio}},
        0.0, 100000.0 * pressure_ratio);
    system.add_checked_equation(
        prefix + "isentropic_efficiency",
        [properties, compressor, eta_is, inlet_p, inlet_h,
         outlet_p, outlet_h](
            const std::vector<double>& x, double& residual) {
            const auto inlet = properties->state_ph(
                x.at(inlet_p), x.at(inlet_h));
            if (!inlet.ok()) return property_failure(inlet);
            const auto isentropic = properties->state_ps(
                x.at(outlet_p), inlet.state.entropy_j_kg_k);
            if (!isentropic.ok()) {
                return property_failure(isentropic);
            }
            const double ideal_change =
                isentropic.state.enthalpy_j_kg -
                x.at(inlet_h);
            residual =
                x.at(outlet_h) - x.at(inlet_h) -
                (compressor ? ideal_change / eta_is
                            : eta_is * ideal_change);
            return EvaluationStatus::success();
        },
        1.0e6);
    system.add_sparse_equation(
        prefix + "shaft_power",
        [compressor, inlet_m, inlet_h, outlet_h, shaft_w](
            const std::vector<double>& x,
            std::vector<EquationPartial>& jacobian) {
            const double direction = compressor ? 1.0 : -1.0;
            const double enthalpy_change =
                x.at(outlet_h) - x.at(inlet_h);
            jacobian.push_back({shaft_w, 1.0});
            jacobian.push_back(
                {inlet_m, -direction * enthalpy_change});
            jacobian.push_back(
                {inlet_h, direction * x.at(inlet_m)});
            jacobian.push_back(
                {outlet_h, -direction * x.at(inlet_m)});
            return x.at(shaft_w) -
                   direction * x.at(inlet_m) *
                       enthalpy_change;
        },
        1.0e6);
}

class TurbomachineryModel final : public ComponentModel {
public:
    TurbomachineryModel(
        std::string kind,
        bool compressor)
        : descriptor_(
              make_descriptor(
                  std::move(kind),
                  compressor ? "in" : "out")),
          compressor_(compressor) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        add_turbomachinery_equations(
            context, system, compressor_);
    }

private:
    ComponentModelDescriptor descriptor_;
    bool compressor_{false};
};

}  // namespace

void register_turbomachinery_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "compressor.gas.isentropic_efficiency", true));
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "compressor.fluid.isentropic_efficiency", true));
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "pump.fluid.isentropic_efficiency", true));
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "turbine.gas.isentropic_efficiency", false));
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "turbine.fluid.isentropic_efficiency", false));
}

}  // namespace thermox::platform

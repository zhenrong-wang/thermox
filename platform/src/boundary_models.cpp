#include "component_modules.hpp"

#include <memory>
#include <utility>

namespace thermox::platform {

namespace {

ComponentModelDescriptor boundary_descriptor(
    std::string kind,
    std::vector<PortModelDescriptor> ports,
    bool supports_transient) {
    ComponentModelDescriptor descriptor;
    descriptor.kind = std::move(kind);
    descriptor.version = "1.0.0";
    descriptor.ports = std::move(ports);
    descriptor.supports_transient = supports_transient;
    return descriptor;
}

}  // namespace

void register_boundary_component_models(ComponentRegistry& registry) {
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "source.fluid.boundary",
            {{"outlet", "fluid", "out"}}, true)));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "sink.fluid.boundary",
            {{"inlet", "fluid", "in"}}, true)));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "source.heat.boundary",
            {{"outlet", "heat", "out"}}, true)));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "sink.heat.boundary",
            {{"inlet", "heat", "in"}}, true)));
}

}  // namespace thermox::platform

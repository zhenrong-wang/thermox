#include "component_modules.hpp"

#include <memory>
#include <utility>

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
            "source.electrical.boundary",
            {{"outlet", "electrical", "out"}}, false, "source")));
    registry.register_model(std::make_shared<MetadataComponentModel>(
        boundary_descriptor(
            "sink.electrical.boundary",
            {{"inlet", "electrical", "in"}}, false, "sink")));
}

}  // namespace thermox::platform

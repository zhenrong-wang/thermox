#include "thermox/platform/model_document.hpp"

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace thermox::platform {

namespace {

struct ExpandedScope {
    std::vector<ComponentDefinition> components;
    std::vector<ConnectionDefinition> connections;
    std::map<std::string, std::string> ports;
    std::map<std::string, std::string> parameters;
};

std::pair<std::string, std::string> split_endpoint(
    const std::string& endpoint,
    const std::string& owner) {
    const auto dot = endpoint.find('.');
    if (dot == std::string::npos || dot == 0U ||
        dot + 1U >= endpoint.size() ||
        endpoint.find('.', dot + 1U) != std::string::npos) {
        throw std::invalid_argument(
            owner + " endpoint must use child.port: " + endpoint);
    }
    return {endpoint.substr(0, dot), endpoint.substr(dot + 1U)};
}

std::string child_id(
    const std::string& prefix,
    const std::string& local_id) {
    if (local_id.empty()) {
        throw std::invalid_argument("assembly child ID must not be empty");
    }
    if (local_id.find('/') != std::string::npos ||
        local_id.find('.') != std::string::npos) {
        throw std::invalid_argument(
            "assembly-local ID must not contain '/' or '.': " +
            local_id);
    }
    return prefix + local_id;
}

ExpandedScope expand_scope(
    const std::string& prefix,
    const std::vector<ComponentDefinition>& components,
    const std::vector<ConnectionDefinition>& connections,
    const std::vector<AssemblyDefinition>& assemblies,
    const std::vector<AssemblyPortDefinition>& ports,
    const std::vector<AssemblyParameterDefinition>& parameters,
    const std::string& owner) {
    ExpandedScope result;
    std::set<std::string> local_ids;
    std::map<std::string, std::string> primitive_ids;
    std::map<std::string, const ComponentDefinition*>
        primitive_definitions;
    std::map<std::string, std::map<std::string, std::string>>
        assembly_ports;
    std::map<std::string, std::map<std::string, std::string>>
        assembly_parameters;

    for (const auto& declared : components) {
        if (!local_ids.insert(declared.id).second) {
            throw std::invalid_argument(
                owner + " has duplicate child ID: " + declared.id);
        }
        auto expanded = declared;
        expanded.id = child_id(prefix, declared.id);
        primitive_ids.emplace(declared.id, expanded.id);
        primitive_definitions.emplace(declared.id, &declared);
        result.components.push_back(std::move(expanded));
    }

    for (const auto& assembly : assemblies) {
        if (!local_ids.insert(assembly.id).second) {
            throw std::invalid_argument(
                owner + " has duplicate child ID: " + assembly.id);
        }
        if (assembly.components.empty() && assembly.assemblies.empty()) {
            throw std::invalid_argument(
                "assembly '" + assembly.id +
                "' must contain a component or nested assembly");
        }
        const auto expanded = expand_scope(
            child_id(prefix, assembly.id) + "/",
            assembly.components,
            assembly.connections,
            assembly.assemblies,
            assembly.ports,
            assembly.parameters,
            "assembly '" + assembly.id + "'");
        result.components.insert(
            result.components.end(),
            expanded.components.begin(), expanded.components.end());
        result.connections.insert(
            result.connections.end(),
            expanded.connections.begin(), expanded.connections.end());
        assembly_ports.emplace(assembly.id, expanded.ports);
        assembly_parameters.emplace(
            assembly.id, expanded.parameters);
    }

    const auto resolve = [&](const std::string& endpoint) {
        const auto [child, port] = split_endpoint(endpoint, owner);
        if (const auto primitive = primitive_ids.find(child);
            primitive != primitive_ids.end()) {
            return primitive->second + "." + port;
        }
        const auto nested = assembly_ports.find(child);
        if (nested == assembly_ports.end()) {
            throw std::invalid_argument(
                owner + " endpoint references unknown child: " +
                endpoint);
        }
        const auto exported = nested->second.find(port);
        if (exported == nested->second.end()) {
            throw std::invalid_argument(
                owner + " endpoint references unexported assembly port: " +
                endpoint);
        }
        return exported->second;
    };

    std::set<std::string> connection_ids;
    for (const auto& declared : connections) {
        if (!connection_ids.insert(declared.id).second) {
            throw std::invalid_argument(
                owner + " has duplicate connection ID: " + declared.id);
        }
        auto expanded = declared;
        expanded.id = child_id(prefix, declared.id);
        expanded.from = resolve(declared.from);
        expanded.to = resolve(declared.to);
        if (expanded.from == expanded.to) {
            throw std::invalid_argument(
                "connection '" + expanded.id +
                "' cannot connect a port to itself");
        }
        result.connections.push_back(std::move(expanded));
    }

    for (const auto& port : ports) {
        if (port.name.empty() || port.name.find('.') != std::string::npos ||
            port.name.find('/') != std::string::npos ||
            !result.ports.emplace(port.name, resolve(port.endpoint)).second) {
            throw std::invalid_argument(
                owner + " has invalid or duplicate public port: " +
                port.name);
        }
    }
    for (const auto& parameter : parameters) {
        const auto [child, name] = split_endpoint(
            parameter.target, owner + " parameter '" + parameter.name + "'");
        std::string target;
        if (const auto primitive = primitive_ids.find(child);
            primitive != primitive_ids.end()) {
            if (!primitive_definitions.at(child)->parameters.contains(name)) {
                throw std::invalid_argument(
                    owner + " public parameter '" + parameter.name +
                    "' targets an undeclared child parameter: " +
                    parameter.target);
            }
            target = "components." + primitive->second +
                ".parameters." + name;
        } else {
            const auto nested = assembly_parameters.find(child);
            if (nested == assembly_parameters.end() ||
                !nested->second.contains(name)) {
                throw std::invalid_argument(
                    owner + " public parameter '" + parameter.name +
                    "' targets an unknown child parameter: " +
                    parameter.target);
            }
            target = nested->second.at(name);
        }
        if (parameter.name.empty() ||
            parameter.name.find('.') != std::string::npos ||
            parameter.name.find('/') != std::string::npos ||
            !result.parameters.emplace(
                parameter.name, std::move(target)).second) {
            throw std::invalid_argument(
                owner + " has invalid or duplicate public parameter: " +
                parameter.name);
        }
    }
    return result;
}

std::string rewrite_public_variable(
    const std::string& key,
    const std::map<std::string, std::string>& public_ports) {
    const auto first = key.find('.');
    if (first == std::string::npos) return key;
    const auto second = key.find('.', first + 1U);
    if (second == std::string::npos) return key;
    const auto endpoint = key.substr(0, second);
    const auto found = public_ports.find(endpoint);
    if (found == public_ports.end()) return key;
    return found->second + key.substr(second);
}

void rewrite_scalar_keys(
    std::map<std::string, ScalarValue>& values,
    const std::map<std::string, std::string>& public_ports) {
    std::map<std::string, ScalarValue> rewritten;
    for (auto& [key, value] : values) {
        const auto expanded = rewrite_public_variable(key, public_ports);
        if (!rewritten.emplace(expanded, std::move(value)).second) {
            throw std::invalid_argument(
                "assembly port expansion creates duplicate case key: " +
                expanded);
        }
    }
    values = std::move(rewritten);
}

}  // namespace

ModelDocument flatten_model_document(const ModelDocument& document) {
    const auto expanded = expand_scope(
        "", document.components, document.connections,
        document.assemblies, {}, {},
        "model '" + document.model_id + "'");

    ModelDocument result = document;
    result.components = expanded.components;
    result.connections = expanded.connections;
    result.assemblies.clear();

    std::map<std::string, std::string> public_ports;
    std::map<std::string, std::string> public_parameters;
    for (const auto& assembly : document.assemblies) {
        const auto nested = expand_scope(
            assembly.id + "/", assembly.components,
            assembly.connections, assembly.assemblies,
            assembly.ports, assembly.parameters,
            "assembly '" + assembly.id + "'");
        for (const auto& [port, endpoint] : nested.ports) {
            public_ports.emplace(
                assembly.id + "." + port, endpoint);
        }
        for (const auto& [parameter, target] : nested.parameters) {
            public_parameters.emplace(
                "components." + assembly.id + ".parameters." +
                    parameter,
                target);
        }
    }

    for (auto& simulation_case : result.cases) {
        rewrite_scalar_keys(
            simulation_case.fixed_values, public_ports);
        rewrite_scalar_keys(
            simulation_case.initial_guesses, public_ports);
        std::map<std::string, ScalarValue> overrides;
        for (auto& [key, value] :
             simulation_case.parameter_overrides) {
            const auto found = public_parameters.find(key);
            const auto& expanded_key = found == public_parameters.end()
                ? key : found->second;
            if (!overrides.emplace(expanded_key, std::move(value)).second) {
                throw std::invalid_argument(
                    "assembly parameter expansion creates duplicate "
                    "case override: " + expanded_key);
            }
        }
        simulation_case.parameter_overrides = std::move(overrides);
    }
    for (auto& calibration : result.calibrations) {
        for (auto& parameter : calibration.parameters) {
            for (auto& target : parameter.targets) {
                const auto found = public_parameters.find(target);
                if (found != public_parameters.end()) {
                    target = found->second;
                }
            }
        }
        for (auto& observation : calibration.observations) {
            observation.target = rewrite_public_variable(
                observation.target, public_ports);
        }
    }
    return result;
}

}  // namespace thermox::platform

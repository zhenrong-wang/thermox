#include "thermox/physics/thermochemistry.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace thermox::physics {

SpeciesComposition::SpeciesComposition(
    CompositionBasis basis,
    std::vector<std::string> species,
    std::vector<double> fractions)
    : basis_(basis),
      species_(std::move(species)),
      fractions_(std::move(fractions)) {
    if (species_.empty() ||
        species_.size() != fractions_.size()) {
        throw std::invalid_argument(
            "species composition requires equally sized, "
            "non-empty species and fraction vectors");
    }
    std::set<std::string> unique_species;
    double sum = 0.0;
    for (std::size_t index = 0; index < species_.size();
         ++index) {
        if (species_[index].empty() ||
            !unique_species.insert(species_[index]).second) {
            throw std::invalid_argument(
                "species composition names must be non-empty "
                "and unique");
        }
        const double value = fractions_[index];
        if (!std::isfinite(value) || value < 0.0 ||
            value > 1.0) {
            throw std::invalid_argument(
                "species composition fractions must be finite "
                "and within [0, 1]");
        }
        sum += value;
    }
    if (std::abs(sum - 1.0) > 1.0e-10) {
        throw std::invalid_argument(
            "species composition fractions must sum to one");
    }
}

double SpeciesComposition::fraction(
    std::string_view species) const {
    const auto found = std::find(
        species_.begin(), species_.end(), species);
    if (found == species_.end()) {
        throw std::invalid_argument(
            "species is absent from composition basis: " +
            std::string(species));
    }
    return fractions_.at(static_cast<std::size_t>(
        std::distance(species_.begin(), found)));
}

void ThermochemistryPackageRegistry::register_backend(
    ThermochemistryBackendDescriptor descriptor,
    ThermochemistryPackageFactory factory) {
    if (descriptor.backend.empty() ||
        descriptor.implementation_name.empty() ||
        descriptor.implementation_version.empty() ||
        descriptor.capabilities.empty() || !factory) {
        throw std::invalid_argument(
            "thermochemistry backend registration must be "
            "complete");
    }
    const auto backend = descriptor.backend;
    if (!entries_
             .emplace(
                 backend,
                 Entry{
                     std::move(descriptor),
                     std::move(factory)})
             .second) {
        throw std::invalid_argument(
            "duplicate thermochemistry backend registration: " +
            backend);
    }
}

bool ThermochemistryPackageRegistry::contains(
    std::string_view backend) const {
    return entries_.find(backend) != entries_.end();
}

std::shared_ptr<const ThermochemistryPackage>
ThermochemistryPackageRegistry::create(
    std::string_view backend,
    std::string_view mechanism,
    std::string_view phase) const {
    const auto found = entries_.find(backend);
    if (found == entries_.end()) {
        throw std::invalid_argument(
            "no thermochemistry package registered for backend: " +
            std::string(backend));
    }
    auto package = found->second.factory(mechanism, phase);
    if (!package) {
        throw std::runtime_error(
            "thermochemistry package factory returned null");
    }
    if (package->name() !=
            found->second.descriptor.implementation_name ||
        package->version() !=
            found->second.descriptor.implementation_version ||
        package->mechanism() != mechanism ||
        package->phase() != phase) {
        throw std::runtime_error(
            "thermochemistry package identity does not match "
            "its registered descriptor or request");
    }
    for (const auto capability :
         found->second.descriptor.capabilities) {
        if (!package->supports(capability)) {
            throw std::runtime_error(
                "thermochemistry package does not provide a "
                "declared capability");
        }
    }
    return package;
}

std::vector<std::string>
ThermochemistryPackageRegistry::backends() const {
    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const auto& [backend, _] : entries_) {
        result.push_back(backend);
    }
    return result;
}

std::vector<ThermochemistryBackendDescriptor>
ThermochemistryPackageRegistry::descriptors() const {
    std::vector<ThermochemistryBackendDescriptor> result;
    result.reserve(entries_.size());
    for (const auto& [_, entry] : entries_) {
        result.push_back(entry.descriptor);
    }
    return result;
}

}  // namespace thermox::physics

#pragma once

#include "thermox/physics/property_package.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace thermox::physics {

enum class CompositionBasis {
    mass_fraction,
    mole_fraction,
};

class SpeciesComposition {
public:
    SpeciesComposition() = default;
    SpeciesComposition(
        CompositionBasis basis,
        std::vector<std::string> species,
        std::vector<double> fractions);

    [[nodiscard]] CompositionBasis basis() const noexcept {
        return basis_;
    }
    [[nodiscard]] const std::vector<std::string>& species()
        const noexcept {
        return species_;
    }
    [[nodiscard]] const std::vector<double>& fractions()
        const noexcept {
        return fractions_;
    }
    [[nodiscard]] double fraction(
        std::string_view species) const;
    [[nodiscard]] bool empty() const noexcept {
        return species_.empty();
    }

private:
    CompositionBasis basis_{CompositionBasis::mass_fraction};
    std::vector<std::string> species_;
    std::vector<double> fractions_;
};

enum class ThermochemistryCapability {
    state_pt,
    state_ph,
    equilibrium_hp,
    transport,
};

struct ThermochemicalState {
    ThermodynamicState thermodynamic;
    SpeciesComposition composition;
    double mean_molecular_weight_kg_mol{0.0};
};

struct ThermochemicalResult {
    ThermochemicalState state;
    PropertyStatus status{PropertyStatus::backend_error};
    std::string message;

    [[nodiscard]] bool ok() const {
        return status == PropertyStatus::success;
    }
};

class ThermochemistryPackage {
public:
    virtual ~ThermochemistryPackage() = default;
    [[nodiscard]] virtual std::string_view name()
        const noexcept = 0;
    [[nodiscard]] virtual std::string_view version()
        const noexcept = 0;
    [[nodiscard]] virtual std::string_view mechanism()
        const noexcept = 0;
    [[nodiscard]] virtual std::string_view phase()
        const noexcept = 0;
    [[nodiscard]] virtual const std::vector<std::string>&
    species_basis() const noexcept = 0;
    [[nodiscard]] virtual bool supports(
        ThermochemistryCapability capability) const noexcept = 0;
    [[nodiscard]] virtual ThermochemicalResult state_pt(
        double pressure_pa,
        double temperature_k,
        const SpeciesComposition& composition) const = 0;
    [[nodiscard]] virtual ThermochemicalResult state_ph(
        double pressure_pa,
        double enthalpy_j_kg,
        const SpeciesComposition& composition) const = 0;
    [[nodiscard]] virtual ThermochemicalResult equilibrate_hp(
        double pressure_pa,
        double enthalpy_j_kg,
        const SpeciesComposition& reactants) const = 0;
};

struct ThermochemistryBackendDescriptor {
    std::string backend;
    std::string implementation_name;
    std::string implementation_version;
    std::vector<ThermochemistryCapability> capabilities;
};

using ThermochemistryPackageFactory = std::function<
    std::shared_ptr<const ThermochemistryPackage>(
        std::string_view mechanism,
        std::string_view phase)>;

class ThermochemistryPackageRegistry {
public:
    void register_backend(
        ThermochemistryBackendDescriptor descriptor,
        ThermochemistryPackageFactory factory);
    [[nodiscard]] bool contains(
        std::string_view backend) const;
    [[nodiscard]] std::shared_ptr<const ThermochemistryPackage>
    create(
        std::string_view backend,
        std::string_view mechanism,
        std::string_view phase) const;
    [[nodiscard]] std::vector<std::string> backends() const;
    [[nodiscard]]
    std::vector<ThermochemistryBackendDescriptor>
    descriptors() const;

private:
    struct Entry {
        ThermochemistryBackendDescriptor descriptor;
        ThermochemistryPackageFactory factory;
    };
    std::map<std::string, Entry, std::less<>> entries_;
};

}  // namespace thermox::physics

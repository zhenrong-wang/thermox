#include "thermox/physics/cantera_thermochemistry.hpp"

#include "cantera/base/Solution.h"
#include "cantera/base/ctexceptions.h"
#include "cantera/base/global.h"
#include "cantera/thermo/ThermoPhase.h"
#include "cantera/transport/Transport.h"

#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace thermox::physics {

namespace {

class CanteraThermochemistryPackage final
    : public ThermochemistryPackage {
public:
    CanteraThermochemistryPackage(
        std::string mechanism,
        std::string phase)
        : mechanism_(std::move(mechanism)),
          phase_(std::move(phase)),
          version_(Cantera::version()),
          solution_(Cantera::newSolution(
              mechanism_, phase_, "mixture-averaged")) {
        const auto thermo = solution_->thermo();
        species_.reserve(thermo->nSpecies());
        for (std::size_t index = 0; index < thermo->nSpecies();
             ++index) {
            species_.push_back(thermo->speciesName(index));
        }
    }

    std::string_view name() const noexcept override {
        return "cantera";
    }
    std::string_view version() const noexcept override {
        return version_;
    }
    std::string_view mechanism() const noexcept override {
        return mechanism_;
    }
    std::string_view phase() const noexcept override {
        return phase_;
    }
    const std::vector<std::string>& species_basis()
        const noexcept override {
        return species_;
    }
    bool supports(
        ThermochemistryCapability) const noexcept override {
        return true;
    }

    ThermochemicalResult state_pt(
        double pressure,
        double temperature,
        const SpeciesComposition& composition)
        const override {
        return evaluate(
            pressure, temperature, composition, Flash::pt);
    }

    ThermochemicalResult state_ph(
        double pressure,
        double enthalpy,
        const SpeciesComposition& composition)
        const override {
        return evaluate(
            pressure, enthalpy, composition, Flash::ph);
    }

    ThermochemicalResult equilibrate_hp(
        double pressure,
        double enthalpy,
        const SpeciesComposition& reactants)
        const override {
        return evaluate(
            pressure, enthalpy, reactants,
            Flash::equilibrium_hp);
    }

private:
    enum class Flash { pt, ph, equilibrium_hp };

    void set_composition(
        Cantera::ThermoPhase& thermo,
        const SpeciesComposition& composition) const {
        std::vector<double> fractions(
            thermo.nSpecies(), 0.0);
        for (std::size_t index = 0;
             index < composition.species().size();
             ++index) {
            const auto species_index = thermo.speciesIndex(
                composition.species().at(index));
            if (species_index == Cantera::npos) {
                throw Cantera::CanteraError(
                    "CanteraThermochemistryPackage::"
                    "set_composition",
                    "species '{}' is absent from mechanism '{}'",
                    composition.species().at(index), mechanism_);
            }
            fractions.at(species_index) =
                composition.fractions().at(index);
        }
        if (composition.basis() ==
            CompositionBasis::mass_fraction) {
            thermo.setMassFractions(fractions.data());
        } else {
            thermo.setMoleFractions(fractions.data());
        }
    }

    ThermochemicalResult evaluate(
        double pressure,
        double second,
        const SpeciesComposition& composition,
        Flash flash) const {
        if (!std::isfinite(pressure) ||
            !std::isfinite(second) || pressure <= 0.0 ||
            (flash == Flash::pt && second <= 0.0)) {
            return {
                {},
                PropertyStatus::invalid_input,
                "thermochemistry state inputs must be finite "
                "and physically positive",
            };
        }
        std::scoped_lock lock(mutex_);
        try {
            const auto thermo = solution_->thermo();
            set_composition(*thermo, composition);
            if (flash == Flash::pt) {
                thermo->setState_TP(second, pressure);
            } else {
                thermo->setState_HP(second, pressure);
            }
            if (flash == Flash::equilibrium_hp) {
                thermo->equilibrate("HP");
            }

            ThermochemicalState state;
            auto& bulk = state.thermodynamic;
            bulk.pressure_pa = thermo->pressure();
            bulk.temperature_k = thermo->temperature();
            bulk.density_kg_m3 = thermo->density();
            bulk.internal_energy_j_kg =
                thermo->intEnergy_mass();
            bulk.enthalpy_j_kg = thermo->enthalpy_mass();
            bulk.entropy_j_kg_k = thermo->entropy_mass();
            bulk.cv_j_kg_k = thermo->cv_mass();
            bulk.cp_j_kg_k = thermo->cp_mass();
            bulk.speed_of_sound_m_s = thermo->soundSpeed();
            bulk.phase = Phase::vapor;
            const auto transport = solution_->transport();
            bulk.viscosity_pa_s = transport->viscosity();
            bulk.thermal_conductivity_w_m_k =
                transport->thermalConductivity();
            std::vector<double> mass_fractions(
                thermo->nSpecies(), 0.0);
            thermo->getMassFractions(mass_fractions.data());
            state.composition = SpeciesComposition{
                CompositionBasis::mass_fraction,
                species_,
                std::move(mass_fractions),
            };
            state.mean_molecular_weight_kg_mol =
                thermo->meanMolecularWeight() / 1000.0;
            return {
                std::move(state),
                PropertyStatus::success,
                {},
            };
        } catch (const Cantera::CanteraError& error) {
            return {
                {},
                PropertyStatus::backend_error,
                error.what(),
            };
        }
    }

    std::string mechanism_;
    std::string phase_;
    std::string version_;
    std::shared_ptr<Cantera::Solution> solution_;
    std::vector<std::string> species_;
    mutable std::mutex mutex_;
};

}  // namespace

void register_cantera_thermochemistry_backend(
    ThermochemistryPackageRegistry& registry) {
    const std::string version = Cantera::version();
    registry.register_backend(
        {
            "cantera",
            "cantera",
            version,
            {
                ThermochemistryCapability::state_pt,
                ThermochemistryCapability::state_ph,
                ThermochemistryCapability::equilibrium_hp,
                ThermochemistryCapability::transport,
            },
        },
        [](std::string_view mechanism,
           std::string_view phase) {
            return std::make_shared<
                const CanteraThermochemistryPackage>(
                std::string(mechanism), std::string(phase));
        });
}

}  // namespace thermox::physics

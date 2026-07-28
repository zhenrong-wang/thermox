#pragma once

#include <memory>

namespace thermox::service {

namespace detail {
struct NativeRuntimeFactory;
}

class SimulationService;
class SimulationRuntime;

std::shared_ptr<const SimulationRuntime>
make_default_simulation_runtime();

class SimulationRuntime {
public:
    ~SimulationRuntime();
    SimulationRuntime(const SimulationRuntime&) = delete;
    SimulationRuntime& operator=(const SimulationRuntime&) = delete;

private:
    struct Impl;
    explicit SimulationRuntime(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;

    friend class SimulationService;
    friend struct detail::NativeRuntimeFactory;
    friend std::shared_ptr<const SimulationRuntime>
    make_default_simulation_runtime();
};

}  // namespace thermox::service

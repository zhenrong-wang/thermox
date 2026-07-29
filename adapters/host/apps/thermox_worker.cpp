#include "thermox/host/host_runtime.hpp"

#include "thermox/service/simulation_runtime.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t stopping = 0;

void request_stop(int) {
    stopping = 1;
}

void usage(std::ostream& out) {
    out << "Usage: thermox_worker [--worker-id <id>]\n"
        << "Durable storage and worker policy are configured "
           "through THERMOX_* environment variables.\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        auto worker = thermox::host::worker_from_environment();
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--worker-id") {
                if (++index >= argc) {
                    throw std::invalid_argument(
                        "missing value for --worker-id");
                }
                worker.worker_id = argv[index];
            } else if (
                argument == "--help" || argument == "-h") {
                usage(std::cout);
                return 0;
            } else {
                throw std::invalid_argument(
                    "unknown argument: " + argument);
            }
        }
        if (worker.worker_id.empty()) {
            throw std::invalid_argument(
                "worker ID must be supplied through --worker-id "
                "or THERMOX_WORKER_ID");
        }

        const auto persistence =
            thermox::host::persistence_from_environment();
        thermox::host::require_durable(persistence);
        thermox::host::configure_library_thread_limit(
            worker.library_threads);
        const auto runtime =
            thermox::service::make_default_simulation_runtime();
        auto service = std::make_shared<
            thermox::service::SimulationJobService>(
                runtime,
                thermox::host::make_job_repository(persistence),
                thermox::host::make_result_artifact_store(
                    persistence));

        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        std::cout << "Thermox worker " << worker.worker_id
                  << " started ("
                  << thermox::host::persistence_description(
                         persistence)
                  << ", library threads: "
                  << worker.library_threads << ")\n";
        while (stopping == 0) {
            try {
                if (service->run_next(
                        worker.worker_id, worker.lease)) {
                    continue;
                }
            } catch (const std::exception& error) {
                std::cerr << "worker iteration failed: "
                          << error.what() << '\n';
            }
            if (stopping == 0) {
                std::this_thread::sleep_for(
                    worker.poll_interval);
            }
        }
        std::cout << "Thermox worker " << worker.worker_id
                  << " stopped\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thermox_worker error: "
                  << error.what() << '\n';
        usage(std::cerr);
        return 1;
    }
}

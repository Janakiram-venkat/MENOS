#include "plan/available_engines.h"

#include "engines/dense/dense_engine.h"
#include "engines/density_matrix/density_matrix_engine.h"
#include "engines/mps/mps_engine.h"
#include "engines/pauli/pauli_propagation_engine.h"
#include "engines/stabilizer/stabilizer_engine.h"
#include "engines/tensor_network/tensor_network_engine.h"
#include "engines/trajectory/trajectory_engine.h"

namespace menos::plan {

const std::vector<const SimulationEngine*>& available_engines() {
    // Function-local static: constructed once, never mutated after
    // (not the "global mutable state" CLAUDE.md forbids -- these five
    // engines are stateless and immutable for the process lifetime).
    static const engines::dense::DenseEngine kDense;
    static const engines::stabilizer::StabilizerEngine kStabilizer;
    static const engines::pauli::PauliPropagationEngine kPauli;
    static const engines::mps::MPSEngine kMps;
    static const engines::tensor_network::TensorNetworkEngine kTensorNetwork;
    static const std::vector<const SimulationEngine*> kEngines{
        &kDense, &kStabilizer, &kPauli, &kMps, &kTensorNetwork};
    return kEngines;
}

const std::vector<const SimulationEngine*>& noisy_job_engines() {
    static const engines::density_matrix::DensityMatrixEngine kDensityMatrix;
    static const std::vector<const SimulationEngine*> kEngines = [] {
        std::vector<const SimulationEngine*> engines = available_engines();
        engines.push_back(&kDensityMatrix);
        return engines;
    }();
    return kEngines;
}

const SimulationEngine& trajectory_engine() {
    static const engines::trajectory::TrajectoryEngine kTrajectory;
    return kTrajectory;
}

}  // namespace menos::plan

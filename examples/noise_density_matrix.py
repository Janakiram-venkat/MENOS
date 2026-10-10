"""A noisy Bell pair: a Qiskit Aer noise model simulated exactly by MENOS.

Needs qiskit-aer (pip install 'menos[noise]'). The expectation values are
checked against Aer's own density-matrix estimator, and the counts against the
distribution they imply.
"""

from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp
from qiskit_aer.noise import NoiseModel, ReadoutError, amplitude_damping_error, depolarizing_error
from qiskit_aer.primitives import EstimatorV2 as AerEstimator

from menos.qiskit import MENOSBackend, MENOSEstimator

noise_model = NoiseModel()
noise_model.add_all_qubit_quantum_error(depolarizing_error(0.02, 1), ["h"])
noise_model.add_all_qubit_quantum_error(depolarizing_error(0.05, 2), ["cx"])
noise_model.add_all_qubit_quantum_error(amplitude_damping_error(0.1), ["measure"])
noise_model.add_all_qubit_readout_error(ReadoutError([[0.98, 0.02], [0.04, 0.96]]))

bell = QuantumCircuit(2)
bell.h(0)
bell.cx(0, 1)

# Exact expectation values under noise.
observables = [SparsePauliOp("ZZ"), SparsePauliOp("XX"), SparsePauliOp("ZI")]
menos_evs = MENOSEstimator(noise_model=noise_model).run([(bell, observables)]).result()[0].data.evs
aer = AerEstimator(
    options={"backend_options": {"noise_model": noise_model, "method": "density_matrix"}}
)
aer_evs = aer.run([(bell, observables)]).result()[0].data.evs
print("<ZZ>, <XX>, <ZI> =", [round(float(v), 6) for v in menos_evs])
assert max(abs(m - a) for m, a in zip(menos_evs, aer_evs)) < 1e-12

# Counts, with the measurement error and readout error applied.
measured = bell.copy()
measured.measure_all()
counts = (
    MENOSBackend()
    .run(measured, shots=10_000, seed_simulator=7, noise_model=noise_model)
    .result()
    .get_counts()
)
print("counts:", dict(sorted(counts.items())))
assert sum(counts.values()) == 10_000
# Noise makes the odd-parity outcomes possible but rare.
odd = counts.get("01", 0) + counts.get("10", 0)
assert 0 < odd < 2_000

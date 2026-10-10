"""Ask MENOS how it planned a job, using the native API's explain()."""

from menos import Circuit, Opcode, RunOptions, run

# A 3-qubit GHZ circuit that measures every qubit.
circuit = Circuit(3, 3)
circuit.append(Opcode.H, [0])
circuit.append(Opcode.CX, [0, 1])
circuit.append(Opcode.CX, [1, 2])
for qubit in range(3):
    circuit.append_measure(qubit, qubit)

options = RunOptions()
options.shots = 100
options.seed = 5

result = run(circuit, options)
print(result.explain())

report = result.explain()
assert report.startswith("MENOS EXECUTION REPORT")
assert "candidate 0" in report  # every enumerated candidate is listed, winners and losers
assert sum(result.counts.values()) == 100
assert result.exact

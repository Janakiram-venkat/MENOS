"""Check which backends an installed MENOS wheel really has (build-wheels.yml).

    check_wheel_backends.py cpu            a CPU-only wheel
    check_wheel_backends.py cuda           a CUDA wheel on a machine with no GPU
    check_wheel_backends.py cuda --gpu     a CUDA wheel on a machine with a GPU

The CUDA wheel is the "auto" wheel: CUDA is compiled in, but the planner only
offers it when a device is present (src/plan/execution_engines.cpp), so with no
GPU it must behave exactly like the CPU wheel.
"""

from __future__ import annotations

import argparse
import sys

from menos import Backend, Circuit, Opcode, RunOptions, run
from menos.exceptions import UnsupportedFeatureError


def bell() -> Circuit:
    circuit = Circuit(2, 2)
    circuit.append(Opcode.H, [0])
    circuit.append(Opcode.CX, [0, 1])
    circuit.append_measure(0, 0)
    circuit.append_measure(1, 1)
    return circuit


def options(backend: Backend | None = None) -> RunOptions:
    opts = RunOptions()
    opts.shots = 256
    opts.seed = 7
    opts.backend = backend
    return opts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("variant", choices=["cpu", "cuda"])
    parser.add_argument("--gpu", action="store_true", help="a CUDA device is present")
    args = parser.parse_args()

    auto = run(bell(), options())
    assert sum(auto.counts.values()) == 256, auto.counts
    forced_cpu = run(bell(), options(Backend.Cpu))
    assert forced_cpu.counts == auto.counts, "forced CPU differs from the automatic choice"

    if args.variant == "cuda" and args.gpu:
        forced_cuda = run(bell(), options(Backend.Cuda))
        assert "backend=cuda" in forced_cuda.explain(), forced_cuda.explain()
        # Not compared with the CPU counts: the automatic CPU choice for a Bell
        # circuit is the stabilizer engine, a different sampler. Bitwise CPU/CUDA
        # agreement of the dense engines is the C++ differential test's job.
        assert set(forced_cuda.counts) == set(auto.counts), forced_cuda.counts
        assert sum(forced_cuda.counts.values()) == 256, forced_cuda.counts
        print("OK: CUDA wheel ran the dense engine on the GPU")
        return 0

    try:
        run(bell(), options(Backend.Cuda))
    except UnsupportedFeatureError as error:
        message = str(error)
    else:
        raise AssertionError("backend=cuda was honoured on a machine without a GPU")

    # The CUDA engine is only registered when a device exists, so with no GPU
    # both wheel types refuse alike. That a CUDA wheel really has CUDA is shown
    # by the bundled libcudart (workflow) and by the --gpu run.
    assert "backend=cuda was requested" in message, message
    print(f"OK: {args.variant} wheel: automatic choice runs on the CPU; forced CUDA: {message}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

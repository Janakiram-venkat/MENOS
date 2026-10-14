"""Diagnostic for the Phase 18 Windows CI wheel crash (temporary; docs/adr/0022).

Prints the CPU, the OpenBLAS core type it selects and the result of calling
LAPACKE_zgesdd directly, once with OpenBLAS's own choice and once with
OPENBLAS_CORETYPE=Haswell, each in a child process so a crash is reported as an
exit code. Usage: python scripts/openblas-probe-windows.py <libopenblas.dll>
"""

import ctypes
import os
import subprocess
import sys

dll_path = sys.argv[1]
if len(sys.argv) == 2:
    print("cpu:", subprocess.run(
        ["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name"],
        capture_output=True, text=True).stdout.strip())
    for coretype in ("", "Haswell"):
        env = dict(os.environ)
        env.pop("OPENBLAS_CORETYPE", None)
        if coretype:
            env["OPENBLAS_CORETYPE"] = coretype
        result = subprocess.run([sys.executable, __file__, dll_path, "child"], env=env,
                                capture_output=True, text=True)
        print(f"OPENBLAS_CORETYPE={coretype or '(unset)'}: exit {result.returncode:#x}")
        print(result.stdout.strip())
    sys.exit(0)

blas = ctypes.CDLL(dll_path)
blas.openblas_get_corename.restype = ctypes.c_char_p
blas.openblas_get_config.restype = ctypes.c_char_p
print("  core:", blas.openblas_get_corename().decode(), "|", blas.openblas_get_config().decode(),
      "| threads:", blas.openblas_get_num_threads(), flush=True)
for n in (2, 4, 16, 64):
    a = (ctypes.c_double * (2 * n * n))(*[((i * 7919) % 13) / 13.0 - 0.5 for i in range(2 * n * n)])
    s = (ctypes.c_double * n)()
    u = (ctypes.c_double * (2 * n * n))()
    vt = (ctypes.c_double * (2 * n * n))()
    info = blas.LAPACKE_zgesdd(102, ctypes.c_char(b"A"), n, n, a, n, s, u, n, vt, n)
    print(f"  zgesdd n={n}: info={info} s0={s[0]:.6f}", flush=True)

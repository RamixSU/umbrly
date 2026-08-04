# Umbrly interpreter vs CPython CLI benchmark

Measured on 2026-07-11 with:

- Umbrly 0.2 "Waffles", invoked as `umbrly.exe file.umb`;
- CPython 3.12.13, invoked as `python.exe file.py`;
- 1 warmup and 5 measured runs per workload;
- median full-process wall time, including interpreter startup, source parsing and execution;
- identical algorithms and verified identical output checksums.

No Umbrly native compilation (`-b`/`-c`) is used.

| Workload | Umbrly interpreter | CPython interpreter | Python / Umbrly |
|---|---:|---:|---:|
| Integer loop, 5,000,000 iterations | 306.704 ms | 798.228 ms | 2.60x |
| Recursive Fibonacci(30) | 318.555 ms | 283.103 ms | 0.89x |
| 500,000 formatted string assignments | 328.445 ms | 228.452 ms | 0.70x |
| Build and sum a 300,000-element array | 188.954 ms | 125.028 ms | 0.66x |

Geometric mean: **1.02x Python/Umbrly**. Values above 1.00 mean Umbrly is faster. The current result is therefore approximately tied overall: Umbrly wins the specialized integer loop, while CPython wins recursion, strings and arrays.

Re-run with:

```powershell
.\benchmarks\run_cli_benchmark.ps1
```

Use `-Runs 9 -Warmups 2` for a longer stability run. Results are machine-dependent and should not be presented as universal performance claims.

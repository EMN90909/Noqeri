# Noqeri native matrix toolkit (CPU)
The additive `noqeri_ai::matrix` API complements the existing bounded GEMM, Tape autodiff and transformer tools without changing their semantics. Implemented in C++17, dependency-free, and linked into `libnoqeri_ai`.

## Operations
- Creation: `zeros`, `ones`, `identity`.
- Layout: `transpose`, `reshape` (copy), `slice`, `concatenate_rows`, `concatenate_cols`.
- Arithmetic: `add`, `subtract`, `multiply`, `divide` (elementwise, singleton-axis broadcasting), `scale`, `clip`, `apply_relu`, numerically stable `sigmoid`.
- Statistics: `sum`, `mean`, population `variance`, `normalize_columns`, `dot`, `l2_norm`.
- Vision: `conv2d_valid` (single-channel cross-correlation, no padding, configurable positive stride).

Include `noqeri/matrix.hpp`, link `noqeri_ai`. All matrices own row-major `f64` storage. Calls reject invalid shapes, NaN/Inf, division by zero, oversized results and out-of-bound indices with C++ exceptions. Existing library limits still apply: dimensions <= 2048, total elements <= 1,048,576, magnitude <= 1e8. This is a CPU C++ SDK, not automatically available from portable `.noqeri` imports. No GPU kernels, general N-dimensional tensor broadcasting, or gradient definitions for these functions are claimed.

Run `cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build --target noqeri-matrix-test && ctest --test-dir build -R '^noqeri-matrix$' --output-on-failure` to verify the new API.

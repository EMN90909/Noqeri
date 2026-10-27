# Noqeri AI: expanded matrix operations

This is an **opt-in, dependency-free C++17 CPU** numerical layer within
\`libnoqeri_ai\`. Noqeri is still a general-purpose language. The portable
\`.noqeri\` scalar library, the experimental transformer, and existing ABI
entrypoints are unchanged.

## C++ surface

\`\`\`cpp
#include <noqeri/matrix.hpp>
using noqeri_ai::Matrix;
namespace mx = noqeri_ai::matrix;

Matrix a(2,2); a.values={4,7,2,6};
Matrix rhs=mx::identity(2);
auto inverse=mx::inverse(a);
auto solution=mx::solve(a,rhs);
auto stable_probabilities=mx::softmax(a,1);
auto normalized=mx::layer_norm_rows(a);
auto lower=mx::cholesky(mx::matmul(mx::transpose(a),a));
\`\`\`

The earlier constructors, transpose/reshape/slice, concatenation, elementwise
broadcast arithmetic, ReLU/sigmoid, clipping, sum/mean/variance,
column normalization, valid 2D convolution, dot and L2 norm are preserved.

| Category | New calls | Important behavior |
| --- | --- | --- |
| Matrix algebra | \`matmul\`, \`diagonal\`, \`trace\` | GEMM delegates to the existing scalar/tiled/SSE2 backend. Trace requires square input. |
| Probabilities | \`softmax\`, \`log_softmax\`, \`logsumexp\` | Supports \`axis=0\` (across rows) or \`axis=1\` (across columns, default); subtracts the maximum before exponentiation. |
| Reductions | \`maximum\`, \`minimum\`, \`argmax\`, \`l1_norm\` | \`argmax\` returns zero-based indices, first index wins ties. |
| Neural layers | \`layer_norm_rows\`, \`gelu\`, \`softplus\` | Layer normalization uses Welford's centered variance and positive epsilon. GELU is the tanh approximation. |
| Selection | \`gather_rows\`, \`gather_columns\` | Duplicate indices supported, empty/out-of-range selections rejected. |
| Linear systems | \`solve\`, \`inverse\`, \`cholesky\` | 128×128 maximum. Solve uses partial pivoting and a conservative near-singular threshold; Cholesky requires symmetric positive-definite input. |
| Statistics | \`covariance_columns\` | Unbiased sample covariance, rows=observations, max 128 features and bounded work. |

**Capacity and numerical contracts:** all dense matrices are row-major f64 with
nonzero dimensions at most 2048 and at most 1,048,576 values. Each input and
result must be finite with absolute element values at most \`1e8\`. Matrix
factorizations are limited to 128×128. GEMM inherits the existing
50-million-multiply-add budget. Covariance is limited to 50 million
feature-products. All operations return owned results or throw on invalid
inputs, no silently clipped outputs. CPU SIMD is only used where the existing
GEMM backend supports it. There are no GPU, sparse, FP16, distributed, or
arbitrary-size tensor claims.

### Linear-solver limitations

\`solve\` is a **bounded dense** partial-pivot Gaussian solver, not a
rank-revealing SVD/QR solver. It rejects pivots at or below \`1e-12\` of the
largest input coefficient magnitude; extremely ill-conditioned but
mathematically invertible systems can therefore be rejected. For large-scale
or sensitive scientific workloads prefer a specialized, fully qualified
numerical library. Cholesky returns lower triangular \`L\` such that
\`A ≈ L × transpose(L)\` for symmetric positive-definite \`A\`.

## Noqeri-native C ABI

\`Include/noqeri/ai_matrix_c.h\` exports
\`nq_ai_matrix_execute(const nq_ai_matrix_request*)\`. Each call takes a
row-major f64 input, an optional second matrix, an operation selector, axis,
epsilon, and caller-owned output buffer plus dimension pointers.

\`Lib/ai/matrix_native.noqeri\` wraps the same request as
\`AiMatrixRequest\` and \`ai_native_matrix(&request)\`. Explicit linking to
\`libnoqeri_ai\` is required, through the Linux x86-64 native AOT preview.
It is **not** an intrinsic, an interpreter API, or a Stage-2 NQRI service.
The native example is at
\`examples/simplicity/ai_native_matrix.noqeri\`.

| Operation ID | Action | Operation ID | Action |
| --- | --- | --- | --- |
| 0 | GEMM | 1 | transpose |
| 2 | softmax | 3 | log-softmax |
| 4 | logsumexp | 5 | layer norm |
| 6 | GELU | 7 | softplus |
| 8 | solve | 9 | inverse |
| 10 | Cholesky | 11 | sample covariance |
| 12 | maximum | 13 | minimum |
| 14 | broadcast add | 15 | subtract |
| 16 | elementwise multiply | 17 | divide |
| 18 | sum | 19 | mean |
| 20 | population variance | 21 | normalize columns |
| 22 | valid 2D convolution | 23 | trace |
| 24 | L1 norm | | |

For operations 2–4, 12–13, 18–20, \`axis\` is 0 or 1.
For convolution (22), \`axis\` specifies positive stride.
For normalization (5 and 21), \`epsilon\` must be positive.
Missing pointers return \`-1\`, malformed input dimensions \`-2\`,
insufficient output capacity \`-3\`, invalid selector/axis \`-4\`,
and numerical or operational failures \`-5\`. An error never writes the
output buffer **or the two dimension outputs**. Aliased input/output is
supported because inputs are copied before writing the final output. C
callers still must provide accessible, correctly aligned buffers and the
declared number of values.

## Verification and maturity

\`\`\`sh
cmake -S . -B build -DBUILD_TESTING=ON -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON
cmake --build build --target noqeri-matrix-test noqeri-matrix-advanced-test
ctest --test-dir build --output-on-failure -R '^noqeri-matrix(-advanced)?$'
\`\`\`

\`.github/workflows/ai-native-ffi.yml\` also compiles a real
\`.noqeri\` AOT client and checks it against the C ABI.
Regression tests cover broadcast compatibility, decomposition, covariance,
extreme logits, axis semantics, NaN, invalid shapes, singular systems,
capacity errors, untouched output state, and aliased input/output.

This is a more complete **bounded numerical foundation**, not a claim of
production qualification for every workload. Further qualification should
include sanitizers, random/property-based comparison against BLAS/Eigen,
benchmarks on each architecture, and long-duration stress testing.

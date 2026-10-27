// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "noqeri/lm.hpp"
#include <cstddef>
#include <vector>
namespace noqeri_ai::matrix {
// Bounded, row-major f64 CPU numerical utilities. All results are owned.
Matrix zeros(std::size_t rows, std::size_t cols);
Matrix ones(std::size_t rows, std::size_t cols);
Matrix identity(std::size_t n);
Matrix transpose(const Matrix& x);
Matrix reshape(const Matrix& x, std::size_t rows, std::size_t cols);
Matrix slice(const Matrix& x, std::size_t row_begin, std::size_t row_end,
             std::size_t col_begin, std::size_t col_end);
Matrix concatenate_rows(const Matrix& a, const Matrix& b);
Matrix concatenate_cols(const Matrix& a, const Matrix& b);
Matrix add(const Matrix& a, const Matrix& b);
Matrix subtract(const Matrix& a, const Matrix& b);
Matrix multiply(const Matrix& a, const Matrix& b);
Matrix divide(const Matrix& a, const Matrix& b);
Matrix scale(const Matrix& x, double value);
Matrix apply_relu(const Matrix& x);
Matrix sigmoid(const Matrix& x);
Matrix clip(const Matrix& x, double low, double high);
Matrix sum(const Matrix& x, std::size_t axis); // 0: (1,cols), 1: (rows,1)
Matrix mean(const Matrix& x, std::size_t axis);
Matrix variance(const Matrix& x, std::size_t axis); // population variance
Matrix normalize_columns(const Matrix& x, double epsilon=1e-8);
Matrix conv2d_valid(const Matrix& input, const Matrix& kernel,
                    std::size_t stride=1);
double dot(const Matrix& a, const Matrix& b); // equal flattened size
double l2_norm(const Matrix& x);

// Additional bounded, row-major f64 operators for numerical and ML workloads.
// All operations validate shape, finite values and the existing 1,048,576
// element / 2,048 dimension limits. Axis 0 reduces rows, axis 1 columns.
Matrix matmul(const Matrix& a,const Matrix& b); // uses the framework GEMM
Matrix diagonal(const Matrix& x);                // shape (1, min(rows,cols))
double trace(const Matrix& x);                   // requires square input
Matrix softmax(const Matrix& x,std::size_t axis=1);
Matrix log_softmax(const Matrix& x,std::size_t axis=1);
Matrix logsumexp(const Matrix& x,std::size_t axis=1);
Matrix maximum(const Matrix& x,std::size_t axis=1);
Matrix minimum(const Matrix& x,std::size_t axis=1);
// Indices are zero-based; ties resolve to the first index.
std::vector<std::size_t> argmax(const Matrix& x,std::size_t axis=1);
Matrix layer_norm_rows(const Matrix& x,double epsilon=1e-5);
Matrix gelu(const Matrix& x);                    // tanh approximation
Matrix softplus(const Matrix& x);
double l1_norm(const Matrix& x);
Matrix gather_rows(const Matrix& x,const std::vector<std::size_t>& indices);
Matrix gather_columns(const Matrix& x,const std::vector<std::size_t>& indices);
// Square dense systems are bounded to 128x128; solve supports multiple RHS.
Matrix solve(const Matrix& coefficients,const Matrix& rhs);
Matrix inverse(const Matrix& x);
Matrix cholesky(const Matrix& x); // lower triangular, symmetric positive definite
// Unbiased sample covariance; rows=observations, columns=features (<=128).
Matrix covariance_columns(const Matrix& x);

} // namespace noqeri_ai::matrix

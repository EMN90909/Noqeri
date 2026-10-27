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
} // namespace noqeri_ai::matrix

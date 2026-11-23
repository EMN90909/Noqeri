// SPDX-License-Identifier: Apache-2.0
#include "noqeri/ai_framework.hpp"
#include "noqeri/matrix.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#define NQAI_SSE2 1
#endif
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(_M_X64))
#include <immintrin.h>
#define NQAI_X86_RUNTIME_AVX2 1
#endif

namespace noqeri_ai::framework {
namespace {
void require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(std::string("Noqeri framework: ")+message);
}
void valid(const Matrix& x) {
    require(x.rows>0 && x.cols>0 && x.rows<=2048 && x.cols<=2048 &&
            x.rows<=1048576/x.cols && x.values.size()==x.rows*x.cols,
            "invalid tensor shape");
    for(double v:x.values) require(std::isfinite(v) && std::abs(v)<=1e8,
                                    "nonfinite or oversized tensor value");
}
void same(const Matrix& a,const Matrix& b) {
    require(a.rows==b.rows && a.cols==b.cols,"tensor shapes differ");
}
} // namespace

bool simd_available() noexcept {
#ifdef NQAI_SSE2
    return true;
#else
    return false;
#endif
}
// Quantize GEMM operands but retain FP64 master parameters and gradients.
// FP16 is an IEEE 754 binary16 storage/rounding emulation on CPU,
// with float (FP32) accumulation. This is not hardware Tensor Core speedup.
namespace {
double cast_compute(double value,Precision precision) {
    if(precision==Precision::FP64)return value;
    if(precision==Precision::FP32)return static_cast<double>(static_cast<float>(value));
    require(precision==Precision::FP16,"unsupported compute precision");
    require(std::abs(value)<=65504,"binary16 operand overflow");
    if(value==0)return value;
    const double magnitude=std::abs(value);
    if(magnitude<std::ldexp(1.0,-25))return std::copysign(0.0,value);
    int exponent=0;std::frexp(magnitude,&exponent);
    const double step=std::ldexp(1.0,std::max(exponent-11,-24));
    const double rounded=std::nearbyint(magnitude/step)*step;
    require(rounded<=65504,"binary16 operand overflow after rounding");
    return std::copysign(rounded,value);
}

#ifdef NQAI_X86_RUNTIME_AVX2
bool cpu_has_avx2_fma() noexcept {
    static const bool supported =
        __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
    return supported;
}

__attribute__((target("avx2,fma")))
void gemm_f64_avx2(const double* a, const double* b, double* c,
                   std::size_t M, std::size_t K, std::size_t N) {
    constexpr std::size_t BM = 64;
    constexpr std::size_t BK = 64;
    constexpr std::size_t BN = 64;
    for (std::size_t ii = 0; ii < M; ii += BM) {
        const std::size_t i_end = std::min(ii + BM, M);
        for (std::size_t kk = 0; kk < K; kk += BK) {
            const std::size_t k_end = std::min(kk + BK, K);
            for (std::size_t jj = 0; jj < N; jj += BN) {
                const std::size_t j_end = std::min(jj + BN, N);
                std::size_t i = ii;
                for (; i + 3 < i_end; i += 4) {
                    std::size_t j = jj;
                    for (; j + 7 < j_end; j += 8) {
                        __m256d c00 = _mm256_loadu_pd(&c[(i + 0) * N + j]);
                        __m256d c01 = _mm256_loadu_pd(&c[(i + 0) * N + j + 4]);
                        __m256d c10 = _mm256_loadu_pd(&c[(i + 1) * N + j]);
                        __m256d c11 = _mm256_loadu_pd(&c[(i + 1) * N + j + 4]);
                        __m256d c20 = _mm256_loadu_pd(&c[(i + 2) * N + j]);
                        __m256d c21 = _mm256_loadu_pd(&c[(i + 2) * N + j + 4]);
                        __m256d c30 = _mm256_loadu_pd(&c[(i + 3) * N + j]);
                        __m256d c31 = _mm256_loadu_pd(&c[(i + 3) * N + j + 4]);
                        for (std::size_t k = kk; k < k_end; ++k) {
                            const __m256d b0 = _mm256_loadu_pd(&b[k * N + j]);
                            const __m256d b1 = _mm256_loadu_pd(&b[k * N + j + 4]);
                            const __m256d a0 = _mm256_set1_pd(a[(i + 0) * K + k]);
                            const __m256d a1 = _mm256_set1_pd(a[(i + 1) * K + k]);
                            const __m256d a2 = _mm256_set1_pd(a[(i + 2) * K + k]);
                            const __m256d a3 = _mm256_set1_pd(a[(i + 3) * K + k]);
                            c00 = _mm256_fmadd_pd(a0, b0, c00);
                            c01 = _mm256_fmadd_pd(a0, b1, c01);
                            c10 = _mm256_fmadd_pd(a1, b0, c10);
                            c11 = _mm256_fmadd_pd(a1, b1, c11);
                            c20 = _mm256_fmadd_pd(a2, b0, c20);
                            c21 = _mm256_fmadd_pd(a2, b1, c21);
                            c30 = _mm256_fmadd_pd(a3, b0, c30);
                            c31 = _mm256_fmadd_pd(a3, b1, c31);
                        }
                        _mm256_storeu_pd(&c[(i + 0) * N + j], c00);
                        _mm256_storeu_pd(&c[(i + 0) * N + j + 4], c01);
                        _mm256_storeu_pd(&c[(i + 1) * N + j], c10);
                        _mm256_storeu_pd(&c[(i + 1) * N + j + 4], c11);
                        _mm256_storeu_pd(&c[(i + 2) * N + j], c20);
                        _mm256_storeu_pd(&c[(i + 2) * N + j + 4], c21);
                        _mm256_storeu_pd(&c[(i + 3) * N + j], c30);
                        _mm256_storeu_pd(&c[(i + 3) * N + j + 4], c31);
                    }
                    for (; j + 3 < j_end; j += 4) {
                        __m256d c0 = _mm256_loadu_pd(&c[(i + 0) * N + j]);
                        __m256d c1 = _mm256_loadu_pd(&c[(i + 1) * N + j]);
                        __m256d c2 = _mm256_loadu_pd(&c[(i + 2) * N + j]);
                        __m256d c3 = _mm256_loadu_pd(&c[(i + 3) * N + j]);
                        for (std::size_t k = kk; k < k_end; ++k) {
                            const __m256d b0 = _mm256_loadu_pd(&b[k * N + j]);
                            c0 = _mm256_fmadd_pd(_mm256_set1_pd(a[(i + 0) * K + k]), b0, c0);
                            c1 = _mm256_fmadd_pd(_mm256_set1_pd(a[(i + 1) * K + k]), b0, c1);
                            c2 = _mm256_fmadd_pd(_mm256_set1_pd(a[(i + 2) * K + k]), b0, c2);
                            c3 = _mm256_fmadd_pd(_mm256_set1_pd(a[(i + 3) * K + k]), b0, c3);
                        }
                        _mm256_storeu_pd(&c[(i + 0) * N + j], c0);
                        _mm256_storeu_pd(&c[(i + 1) * N + j], c1);
                        _mm256_storeu_pd(&c[(i + 2) * N + j], c2);
                        _mm256_storeu_pd(&c[(i + 3) * N + j], c3);
                    }
                    for (; j < j_end; ++j) {
                        double s0 = c[(i + 0) * N + j], s1 = c[(i + 1) * N + j];
                        double s2 = c[(i + 2) * N + j], s3 = c[(i + 3) * N + j];
                        for (std::size_t k = kk; k < k_end; ++k) {
                            const double bv = b[k * N + j];
                            s0 += a[(i + 0) * K + k] * bv;
                            s1 += a[(i + 1) * K + k] * bv;
                            s2 += a[(i + 2) * K + k] * bv;
                            s3 += a[(i + 3) * K + k] * bv;
                        }
                        c[(i + 0) * N + j] = s0; c[(i + 1) * N + j] = s1;
                        c[(i + 2) * N + j] = s2; c[(i + 3) * N + j] = s3;
                    }
                }
                for (; i < i_end; ++i) {
                    for (std::size_t k = kk; k < k_end; ++k) {
                        const double av = a[i * K + k];
                        const __m256d va = _mm256_set1_pd(av);
                        std::size_t j = jj;
                        for (; j + 3 < j_end; j += 4) {
                            __m256d vc = _mm256_loadu_pd(&c[i * N + j]);
                            const __m256d vb = _mm256_loadu_pd(&b[k * N + j]);
                            _mm256_storeu_pd(&c[i * N + j], _mm256_fmadd_pd(va, vb, vc));
                        }
                        for (; j < j_end; ++j)
                            c[i * N + j] += av * b[k * N + j];
                    }
                }
            }
        }
    }
}

__attribute__((target("avx2,fma")))
void gemm_f32_avx2(const float* a, const float* b, float* c,
                   std::size_t M, std::size_t K, std::size_t N) {
    constexpr std::size_t BM = 64;
    constexpr std::size_t BK = 64;
    constexpr std::size_t BN = 64;
    for (std::size_t ii = 0; ii < M; ii += BM) {
        const std::size_t i_end = std::min(ii + BM, M);
        for (std::size_t kk = 0; kk < K; kk += BK) {
            const std::size_t k_end = std::min(kk + BK, K);
            for (std::size_t jj = 0; jj < N; jj += BN) {
                const std::size_t j_end = std::min(jj + BN, N);
                std::size_t i = ii;
                for (; i + 3 < i_end; i += 4) {
                    std::size_t j = jj;
                    for (; j + 15 < j_end; j += 16) {
                        __m256 c00 = _mm256_loadu_ps(&c[(i + 0) * N + j]);
                        __m256 c01 = _mm256_loadu_ps(&c[(i + 0) * N + j + 8]);
                        __m256 c10 = _mm256_loadu_ps(&c[(i + 1) * N + j]);
                        __m256 c11 = _mm256_loadu_ps(&c[(i + 1) * N + j + 8]);
                        __m256 c20 = _mm256_loadu_ps(&c[(i + 2) * N + j]);
                        __m256 c21 = _mm256_loadu_ps(&c[(i + 2) * N + j + 8]);
                        __m256 c30 = _mm256_loadu_ps(&c[(i + 3) * N + j]);
                        __m256 c31 = _mm256_loadu_ps(&c[(i + 3) * N + j + 8]);
                        for (std::size_t k = kk; k < k_end; ++k) {
                            const __m256 b0 = _mm256_loadu_ps(&b[k * N + j]);
                            const __m256 b1 = _mm256_loadu_ps(&b[k * N + j + 8]);
                            const __m256 a0 = _mm256_set1_ps(a[(i + 0) * K + k]);
                            const __m256 a1 = _mm256_set1_ps(a[(i + 1) * K + k]);
                            const __m256 a2 = _mm256_set1_ps(a[(i + 2) * K + k]);
                            const __m256 a3 = _mm256_set1_ps(a[(i + 3) * K + k]);
                            c00 = _mm256_fmadd_ps(a0, b0, c00);
                            c01 = _mm256_fmadd_ps(a0, b1, c01);
                            c10 = _mm256_fmadd_ps(a1, b0, c10);
                            c11 = _mm256_fmadd_ps(a1, b1, c11);
                            c20 = _mm256_fmadd_ps(a2, b0, c20);
                            c21 = _mm256_fmadd_ps(a2, b1, c21);
                            c30 = _mm256_fmadd_ps(a3, b0, c30);
                            c31 = _mm256_fmadd_ps(a3, b1, c31);
                        }
                        _mm256_storeu_ps(&c[(i + 0) * N + j], c00);
                        _mm256_storeu_ps(&c[(i + 0) * N + j + 8], c01);
                        _mm256_storeu_ps(&c[(i + 1) * N + j], c10);
                        _mm256_storeu_ps(&c[(i + 1) * N + j + 8], c11);
                        _mm256_storeu_ps(&c[(i + 2) * N + j], c20);
                        _mm256_storeu_ps(&c[(i + 2) * N + j + 8], c21);
                        _mm256_storeu_ps(&c[(i + 3) * N + j], c30);
                        _mm256_storeu_ps(&c[(i + 3) * N + j + 8], c31);
                    }
                    for (; j + 7 < j_end; j += 8) {
                        __m256 c0 = _mm256_loadu_ps(&c[(i + 0) * N + j]);
                        __m256 c1 = _mm256_loadu_ps(&c[(i + 1) * N + j]);
                        __m256 c2 = _mm256_loadu_ps(&c[(i + 2) * N + j]);
                        __m256 c3 = _mm256_loadu_ps(&c[(i + 3) * N + j]);
                        for (std::size_t k = kk; k < k_end; ++k) {
                            const __m256 b0 = _mm256_loadu_ps(&b[k * N + j]);
                            c0 = _mm256_fmadd_ps(_mm256_set1_ps(a[(i + 0) * K + k]), b0, c0);
                            c1 = _mm256_fmadd_ps(_mm256_set1_ps(a[(i + 1) * K + k]), b0, c1);
                            c2 = _mm256_fmadd_ps(_mm256_set1_ps(a[(i + 2) * K + k]), b0, c2);
                            c3 = _mm256_fmadd_ps(_mm256_set1_ps(a[(i + 3) * K + k]), b0, c3);
                        }
                        _mm256_storeu_ps(&c[(i + 0) * N + j], c0);
                        _mm256_storeu_ps(&c[(i + 1) * N + j], c1);
                        _mm256_storeu_ps(&c[(i + 2) * N + j], c2);
                        _mm256_storeu_ps(&c[(i + 3) * N + j], c3);
                    }
                    for (; j < j_end; ++j) {
                        float s0 = c[(i + 0) * N + j], s1 = c[(i + 1) * N + j];
                        float s2 = c[(i + 2) * N + j], s3 = c[(i + 3) * N + j];
                        for (std::size_t k = kk; k < k_end; ++k) {
                            const float bv = b[k * N + j];
                            s0 += a[(i + 0) * K + k] * bv;
                            s1 += a[(i + 1) * K + k] * bv;
                            s2 += a[(i + 2) * K + k] * bv;
                            s3 += a[(i + 3) * K + k] * bv;
                        }
                        c[(i + 0) * N + j] = s0; c[(i + 1) * N + j] = s1;
                        c[(i + 2) * N + j] = s2; c[(i + 3) * N + j] = s3;
                    }
                }
                for (; i < i_end; ++i) {
                    for (std::size_t k = kk; k < k_end; ++k) {
                        const float av = a[i * K + k];
                        const __m256 va = _mm256_set1_ps(av);
                        std::size_t j = jj;
                        for (; j + 7 < j_end; j += 8) {
                            __m256 vc = _mm256_loadu_ps(&c[i * N + j]);
                            const __m256 vb = _mm256_loadu_ps(&b[k * N + j]);
                            _mm256_storeu_ps(&c[i * N + j], _mm256_fmadd_ps(va, vb, vc));
                        }
                        for (; j < j_end; ++j)
                            c[i * N + j] += av * b[k * N + j];
                    }
                }
            }
        }
    }
}

__attribute__((target("avx2,fma")))
inline double hsum_f64_avx2(const __m256d v) {
    const __m128d lo = _mm256_castpd256_pd128(v);
    const __m128d hi = _mm256_extractf128_pd(v, 1);
    const __m128d s = _mm_add_pd(lo, hi);
    const __m128d sh = _mm_unpackhi_pd(s, s);
    return _mm_cvtsd_f64(_mm_add_sd(s, sh));
}

__attribute__((target("avx2,fma")))
void matmul_backward_f64_avx2(const double* ap, const double* bp, const double* gp,
                              double* da, double* db,
                              std::size_t M, std::size_t K, std::size_t N) {
    // Accumulate dB += A^T * G and dA += G * B^T using vectorized FMA loops.
    for (std::size_t i = 0; i < M; ++i) {
        const double* gi = &gp[i * N];
        const double* ai = &ap[i * K];
        double* dai = &da[i * K];
        std::size_t k = 0;
        for (; k + 3 < K; k += 4) {
            const double* bk0 = &bp[(k + 0) * N];
            const double* bk1 = &bp[(k + 1) * N];
            const double* bk2 = &bp[(k + 2) * N];
            const double* bk3 = &bp[(k + 3) * N];
            double* dbk0 = &db[(k + 0) * N];
            double* dbk1 = &db[(k + 1) * N];
            double* dbk2 = &db[(k + 2) * N];
            double* dbk3 = &db[(k + 3) * N];
            const __m256d va0 = _mm256_set1_pd(ai[k + 0]);
            const __m256d va1 = _mm256_set1_pd(ai[k + 1]);
            const __m256d va2 = _mm256_set1_pd(ai[k + 2]);
            const __m256d va3 = _mm256_set1_pd(ai[k + 3]);
            __m256d vs0 = _mm256_setzero_pd();
            __m256d vs1 = _mm256_setzero_pd();
            __m256d vs2 = _mm256_setzero_pd();
            __m256d vs3 = _mm256_setzero_pd();
            std::size_t j = 0;
            for (; j + 3 < N; j += 4) {
                const __m256d vg = _mm256_loadu_pd(&gi[j]);
                vs0 = _mm256_fmadd_pd(vg, _mm256_loadu_pd(&bk0[j]), vs0);
                vs1 = _mm256_fmadd_pd(vg, _mm256_loadu_pd(&bk1[j]), vs1);
                vs2 = _mm256_fmadd_pd(vg, _mm256_loadu_pd(&bk2[j]), vs2);
                vs3 = _mm256_fmadd_pd(vg, _mm256_loadu_pd(&bk3[j]), vs3);
                _mm256_storeu_pd(&dbk0[j], _mm256_fmadd_pd(va0, vg, _mm256_loadu_pd(&dbk0[j])));
                _mm256_storeu_pd(&dbk1[j], _mm256_fmadd_pd(va1, vg, _mm256_loadu_pd(&dbk1[j])));
                _mm256_storeu_pd(&dbk2[j], _mm256_fmadd_pd(va2, vg, _mm256_loadu_pd(&dbk2[j])));
                _mm256_storeu_pd(&dbk3[j], _mm256_fmadd_pd(va3, vg, _mm256_loadu_pd(&dbk3[j])));
            }
            double s0 = hsum_f64_avx2(vs0), s1 = hsum_f64_avx2(vs1),
                   s2 = hsum_f64_avx2(vs2), s3 = hsum_f64_avx2(vs3);
            const double a0 = ai[k + 0], a1 = ai[k + 1], a2 = ai[k + 2], a3 = ai[k + 3];
            for (; j < N; ++j) {
                const double gij = gi[j];
                s0 += gij * bk0[j]; dbk0[j] += gij * a0;
                s1 += gij * bk1[j]; dbk1[j] += gij * a1;
                s2 += gij * bk2[j]; dbk2[j] += gij * a2;
                s3 += gij * bk3[j]; dbk3[j] += gij * a3;
            }
            dai[k + 0] += s0; dai[k + 1] += s1;
            dai[k + 2] += s2; dai[k + 3] += s3;
        }
        for (; k < K; ++k) {
            const double aik = ai[k];
            const double* bk = &bp[k * N];
            double* dbk = &db[k * N];
            const __m256d va = _mm256_set1_pd(aik);
            __m256d vs = _mm256_setzero_pd();
            std::size_t j = 0;
            for (; j + 3 < N; j += 4) {
                const __m256d vg = _mm256_loadu_pd(&gi[j]);
                vs = _mm256_fmadd_pd(vg, _mm256_loadu_pd(&bk[j]), vs);
                _mm256_storeu_pd(&dbk[j], _mm256_fmadd_pd(va, vg, _mm256_loadu_pd(&dbk[j])));
            }
            const __m128d lo = _mm256_castpd256_pd128(vs);
            const __m128d hi = _mm256_extractf128_pd(vs, 1);
            const __m128d s = _mm_add_pd(lo, hi);
            double sum = _mm_cvtsd_f64(_mm_add_sd(s, _mm_unpackhi_pd(s, s)));
            for (; j < N; ++j) {
                const double gij = gi[j];
                sum += gij * bk[j];
                dbk[j] += gij * aik;
            }
            dai[k] += sum;
        }
    }
}
#endif

Matrix compute_gemm(const Matrix& a,const Matrix& b,Precision precision) {
    valid(a);valid(b);
    require(a.cols==b.rows && a.rows<=1048576/b.cols &&
            a.rows<=50000000/(a.cols*b.cols),"invalid compute GEMM shape");
    std::vector<float> aq(a.values.size());
    std::vector<float> bq(b.values.size());
    if (precision == Precision::FP32) {
        for (std::size_t i = 0; i < a.values.size(); ++i)
            aq[i] = static_cast<float>(a.values[i]);
        for (std::size_t i = 0; i < b.values.size(); ++i)
            bq[i] = static_cast<float>(b.values[i]);
    } else {
        for (std::size_t i = 0; i < a.values.size(); ++i)
            aq[i] = static_cast<float>(cast_compute(a.values[i], precision));
        for (std::size_t i = 0; i < b.values.size(); ++i)
            bq[i] = static_cast<float>(cast_compute(b.values[i], precision));
    }
    const std::size_t M = a.rows, K = a.cols, N = b.cols;
    std::vector<float> acc(M * N, 0.0f);
#ifdef NQAI_X86_RUNTIME_AVX2
    if (cpu_has_avx2_fma()) {
        gemm_f32_avx2(aq.data(), bq.data(), acc.data(), M, K, N);
    } else
#endif
    {
        constexpr std::size_t block = 64;
        for (std::size_t ii = 0; ii < M; ii += block)
            for (std::size_t kk = 0; kk < K; kk += block)
                for (std::size_t jj = 0; jj < N; jj += block) {
                    const std::size_t ie = std::min(ii + block, M);
                    const std::size_t ke = std::min(kk + block, K);
                    const std::size_t je = std::min(jj + block, N);
                    for (std::size_t i = ii; i < ie; ++i)
                        for (std::size_t k = kk; k < ke; ++k) {
                            const float av = aq[i * K + k];
                            float* crow = &acc[i * N];
                            const float* brow = &bq[k * N];
                            for (std::size_t j = jj; j < je; ++j)
                                crow[j] += av * brow[j];
                        }
                }
    }
    Matrix out(M, N);
    for (std::size_t i = 0; i < out.values.size(); ++i)
        out.values[i] = static_cast<double>(acc[i]);
    valid(out);return out;
}
Matrix transpose_for_grad(const Matrix& m) {
    Matrix out(m.cols,m.rows);
    for(std::size_t i=0;i<m.rows;++i)
        for(std::size_t j=0;j<m.cols;++j)out.at(j,i)=m.at(i,j);
    return out;
}
} // namespace

Matrix gemm(const Matrix& a,const Matrix& b,Backend backend) {
    valid(a);valid(b);
    require(a.cols==b.rows && a.rows<=1048576/b.cols &&
            a.rows<=50000000/(a.cols*b.cols),"incompatible or oversized GEMM");
    if(backend==Backend::OpenCL)return gpu_gemm(a,b);
    Matrix c(a.rows,b.cols);
    if(backend==Backend::Auto) backend=simd_available()?Backend::Simd:Backend::Tiled;
    if(backend==Backend::Simd && !simd_available())
        throw std::runtime_error("Noqeri framework: SIMD backend unavailable");
    if(backend==Backend::Scalar) {
        for(std::size_t i=0;i<a.rows;++i)
            for(std::size_t j=0;j<b.cols;++j)
                for(std::size_t k=0;k<a.cols;++k)
                    c.values[i*b.cols+j]+=a.values[i*a.cols+k]*b.values[k*b.cols+j];
#ifdef NQAI_X86_RUNTIME_AVX2
    } else if (backend == Backend::Simd && cpu_has_avx2_fma()) {
        gemm_f64_avx2(a.values.data(), b.values.data(), c.values.data(),
                      a.rows, a.cols, b.cols);
#endif
    }else{
        constexpr std::size_t block=64;
        for(std::size_t ii=0;ii<a.rows;ii+=block)
            for(std::size_t kk=0;kk<a.cols;kk+=block)
                for(std::size_t jj=0;jj<b.cols;jj+=block)
                    for(std::size_t i=ii;i<std::min(ii+block,a.rows);++i)
                        for(std::size_t k=kk;k<std::min(kk+block,a.cols);++k) {
                            const double x=a.values[i*a.cols+k];
                            std::size_t j=jj;
#ifdef NQAI_SSE2
                            if(backend==Backend::Simd) {
                                const __m128d broadcast=_mm_set1_pd(x);
                                for(;j+1<std::min(jj+block,b.cols);j+=2) {
                                    const __m128d y=_mm_loadu_pd(&b.values[k*b.cols+j]);
                                    const __m128d sum=_mm_loadu_pd(&c.values[i*b.cols+j]);
                                    _mm_storeu_pd(&c.values[i*b.cols+j],
                                                  _mm_add_pd(sum,_mm_mul_pd(broadcast,y)));
                                }
                            }
#endif
                            for(;j<std::min(jj+block,b.cols);++j)
                                c.values[i*b.cols+j]+=x*b.values[k*b.cols+j];
                        }
    }
    valid(c);
    return c;
}
std::size_t Tape::append(Node node) {
    require(nodes_.size()<128,"autodiff graph budget exceeded");
    valid(node.value);
    node.grad=Matrix(node.value.rows,node.value.cols);
    nodes_.push_back(std::move(node));
    return nodes_.size()-1;
}
std::size_t Tape::size() const noexcept {return nodes_.size();}
void Tape::set_precision(Precision precision) {
    require(precision==Precision::FP64 || precision==Precision::FP32 ||
            precision==Precision::FP16,"invalid compute precision");
    precision_=precision;
}
void Tape::set_backend(Backend backend) {
    require(backend==Backend::Auto || backend==Backend::Scalar ||
            backend==Backend::Tiled || backend==Backend::Simd ||
            backend==Backend::OpenCL,"unknown tensor backend");
    backend_=backend;
}
const Matrix& Tape::value(std::size_t id) const {
    require(id<nodes_.size(),"invalid tensor handle");
    return nodes_[id].value;
}
const Matrix& Tape::grad(std::size_t id) const {
    require(id<nodes_.size(),"invalid gradient handle");
    return nodes_[id].grad;
}
std::size_t Tape::leaf(const Matrix& value) {
    Node n;n.value=value;return append(std::move(n));
}
std::size_t Tape::add(std::size_t a,std::size_t b) {
    const Matrix& x=value(a);const Matrix& y=value(b);
    same(x,y);Node n;n.op=Op::Add;n.a=a;n.b=b;n.value=Matrix(x.rows,x.cols);
    for(std::size_t j=0;j<x.values.size();++j)n.value.values[j]=x.values[j]+y.values[j];
    return append(std::move(n));
}
std::size_t Tape::matmul(std::size_t a,std::size_t b) {
    Node n;n.op=Op::Matmul;n.a=a;n.b=b;
    require(backend_!=Backend::OpenCL || precision_==Precision::FP32,
            "OpenCL training uses FP32 compute only");
    n.value=(precision_==Precision::FP64 || backend_==Backend::OpenCL)
        ?gemm(value(a),value(b),backend_)
        :compute_gemm(value(a),value(b),precision_);
    return append(std::move(n));
}
std::size_t Tape::transpose(std::size_t a) {
    const Matrix& x=value(a);Node n;n.op=Op::Transpose;n.a=a;n.value=Matrix(x.cols,x.rows);
    for(std::size_t i=0;i<x.rows;++i)
        for(std::size_t j=0;j<x.cols;++j)n.value.at(j,i)=x.at(i,j);
    return append(std::move(n));
}
std::size_t Tape::scale(std::size_t a,double factor) {
    require(std::isfinite(factor) && std::abs(factor)<=1e6,"invalid scale factor");
    const Matrix& x=value(a);Node n;n.op=Op::Scale;n.a=a;n.factor=factor;n.value=x;
    for(double& z:n.value.values)z*=factor;
    return append(std::move(n));
}
std::size_t Tape::tanh(std::size_t a) {
    Node n;n.op=Op::Tanh;n.a=a;n.value=value(a);
    for(double& z:n.value.values)z=std::tanh(z);
    return append(std::move(n));
}
std::size_t Tape::relu(std::size_t a) {
    Node n;n.op=Op::Relu;n.a=a;n.value=matrix::apply_relu(value(a));
    return append(std::move(n));
}
std::size_t Tape::gelu(std::size_t a) {
    Node n;n.op=Op::Gelu;n.a=a;n.value=matrix::gelu(value(a));
    return append(std::move(n));
}
std::size_t Tape::layer_norm(std::size_t a,double epsilon) {
    require(std::isfinite(epsilon) && epsilon>=1e-8 && epsilon<=1,
            "invalid layer norm epsilon");
    Node n;n.op=Op::LayerNorm;n.a=a;n.factor=epsilon;
    n.value=matrix::layer_norm_rows(value(a),epsilon);
    return append(std::move(n));
}
std::size_t Tape::softmax(std::size_t a,bool causal) {
    const Matrix& x=value(a);
    if(causal)require(x.rows==x.cols && x.rows<=256,"causal attention needs square bounded scores");
    Node n;n.op=Op::Softmax;n.a=a;n.causal=causal;n.value=Matrix(x.rows,x.cols);
    const std::size_t cols=x.cols;
    for(std::size_t i=0;i<x.rows;++i) {
        const std::size_t limit=causal?i+1:cols;
        const double* src=&x.values[i*cols];
        double* dst=&n.value.values[i*cols];
        double top=-std::numeric_limits<double>::infinity();
        for(std::size_t j=0;j<limit;++j)top=std::max(top,src[j]);
        double denom=0;
        for(std::size_t j=0;j<limit;++j) {
            const double e=std::exp(src[j]-top);
            dst[j]=e;
            denom+=e;
        }
        require(std::isfinite(denom) && denom>0,"unstable tensor softmax");
        const double inv_denom=1.0/denom;
        for(std::size_t j=0;j<limit;++j)dst[j]*=inv_denom;
    }
    return append(std::move(n));
}
std::size_t Tape::gather_rows(std::size_t embedding,const std::vector<std::size_t>& ids) {
    const Matrix& x=value(embedding);
    require(!ids.empty() && ids.size()<=256,"invalid gather index length");
    Node n;n.op=Op::Gather;n.a=embedding;n.indices=ids;n.value=Matrix(ids.size(),x.cols);
    for(std::size_t i=0;i<ids.size();++i) {
        require(ids[i]<x.rows,"gather index out of bounds");
        for(std::size_t j=0;j<x.cols;++j)n.value.at(i,j)=x.at(ids[i],j);
    }
    return append(std::move(n));
}
std::size_t Tape::last_row(std::size_t a) {
    const Matrix& x=value(a);Node n;n.op=Op::LastRow;n.a=a;n.value=Matrix(1,x.cols);
    for(std::size_t j=0;j<x.cols;++j)n.value.at(0,j)=x.at(x.rows-1,j);
    return append(std::move(n));
}
std::size_t Tape::cross_entropy(std::size_t logits,std::size_t target) {
    const Matrix& x=value(logits);
    require(x.rows==1 && target<x.cols,"invalid next-token classification");
    double top=*std::max_element(x.values.begin(),x.values.end()),denom=0;
    for(double z:x.values)denom+=std::exp(z-top);
    require(std::isfinite(denom) && denom>0,"unstable cross entropy");
    Node n;n.op=Op::CrossEntropy;n.a=logits;n.target=target;
    n.value=Matrix(1,1,top+std::log(denom)-x.values[target]);
    return append(std::move(n));
}
void Tape::backward(std::size_t loss) {
    require(loss<nodes_.size() && nodes_[loss].value.values.size()==1,
            "backward requires scalar loss");
    for(auto& node:nodes_)std::fill(node.grad.values.begin(),node.grad.values.end(),0.0);
    nodes_[loss].grad.values[0]=1.0;
    for(std::size_t id=nodes_.size();id-->0;) {
        const Node& n=nodes_[id];
        switch(n.op) {
        case Op::Leaf:break;
        case Op::Add:
            for(std::size_t j=0;j<n.grad.values.size();++j) {
                nodes_[n.a].grad.values[j]+=n.grad.values[j];
                nodes_[n.b].grad.values[j]+=n.grad.values[j];
            }
            break;
        case Op::Matmul: {
            const Matrix& a=nodes_[n.a].value;const Matrix& b=nodes_[n.b].value;
            if(backend_==Backend::OpenCL) {
                const Matrix ga=gpu_gemm(n.grad,transpose_for_grad(b));
                const Matrix gb=gpu_gemm(transpose_for_grad(a),n.grad);
                for(std::size_t i=0;i<ga.values.size();++i)
                    nodes_[n.a].grad.values[i]+=ga.values[i];
                for(std::size_t i=0;i<gb.values.size();++i)
                    nodes_[n.b].grad.values[i]+=gb.values[i];
                break;
            }
            const std::size_t M=a.rows,K=a.cols,N=b.cols;
            const double* ap=a.values.data();
            const double* bp=b.values.data();
            const double* gp=n.grad.values.data();
            double* da=nodes_[n.a].grad.values.data();
            double* db=nodes_[n.b].grad.values.data();
#ifdef NQAI_X86_RUNTIME_AVX2
            if((backend_==Backend::Auto || backend_==Backend::Simd) && cpu_has_avx2_fma()) {
                matmul_backward_f64_avx2(ap,bp,gp,da,db,M,K,N);
                break;
            }
#endif
            for(std::size_t i=0;i<M;++i) {
                const double* gi=&gp[i*N];
                double* dai=&da[i*K];
                const double* ai=&ap[i*K];
                for(std::size_t k=0;k<K;++k) {
                    const double aik=ai[k];
                    const double* bk=&bp[k*N];
                    double* dbk=&db[k*N];
                    double sum=0.0;
                    for(std::size_t j=0;j<N;++j) {
                        const double gij=gi[j];
                        sum+=gij*bk[j];
                        dbk[j]+=gij*aik;
                    }
                    dai[k]+=sum;
                }
            }
            break;
        }
        case Op::Transpose: {
            for(std::size_t i=0;i<n.value.rows;++i)
                for(std::size_t j=0;j<n.value.cols;++j)
                    nodes_[n.a].grad.at(j,i)+=n.grad.at(i,j);
            break;
        }
        case Op::Scale:
            for(std::size_t j=0;j<n.grad.values.size();++j)
                nodes_[n.a].grad.values[j]+=n.grad.values[j]*n.factor;
            break;
        case Op::Tanh:
            for(std::size_t j=0;j<n.grad.values.size();++j)
                nodes_[n.a].grad.values[j]+=
                    n.grad.values[j]*(1.0-n.value.values[j]*n.value.values[j]);
            break;
        case Op::Relu: {
            const Matrix& source=nodes_[n.a].value;
            for(std::size_t j=0;j<n.grad.values.size();++j)
                if(source.values[j]>0)
                    nodes_[n.a].grad.values[j]+=n.grad.values[j];
            break;
        }
        case Op::Gelu: {
            const Matrix& source=nodes_[n.a].value;
            constexpr double k=0.7978845608028654; // sqrt(2/pi)
            for(std::size_t j=0;j<n.grad.values.size();++j) {
                const double x=source.values[j];
                const double t=std::tanh(k*(x+0.044715*x*x*x));
                const double d=0.5*(1+t)+0.5*x*(1-t*t)*
                    k*(1+3*0.044715*x*x);
                nodes_[n.a].grad.values[j]+=n.grad.values[j]*d;
            }
            break;
        }
        case Op::LayerNorm: {
            const Matrix& source=nodes_[n.a].value;
            for(std::size_t i=0;i<source.rows;++i) {
                long double mean=0,m2=0,gsum=0,gysum=0;
                for(std::size_t j=0;j<source.cols;++j) {
                    const long double x=source.at(i,j);
                    const long double delta=x-mean;
                    mean+=delta/static_cast<long double>(j+1);
                    m2+=delta*(x-mean);
                    gsum+=n.grad.at(i,j);
                    gysum+=static_cast<long double>(n.grad.at(i,j))*n.value.at(i,j);
                }
                const long double count=source.cols;
                const long double inverse=1/std::sqrt(m2/count+n.factor);
                const long double mean_g=gsum/count;
                const long double mean_gy=gysum/count;
                for(std::size_t j=0;j<source.cols;++j) {
                    const long double grad=inverse*
                        (static_cast<long double>(n.grad.at(i,j))-mean_g-
                         static_cast<long double>(n.value.at(i,j))*mean_gy);
                    nodes_[n.a].grad.at(i,j)+=static_cast<double>(grad);
                }
            }
            break;
        }
        case Op::Softmax:
            for(std::size_t i=0;i<n.value.rows;++i) {
                const std::size_t limit=n.causal?i+1:n.value.cols;
                double dot=0;
                for(std::size_t j=0;j<limit;++j)
                    dot+=n.grad.at(i,j)*n.value.at(i,j);
                for(std::size_t j=0;j<limit;++j)
                    nodes_[n.a].grad.at(i,j)+=
                        n.value.at(i,j)*(n.grad.at(i,j)-dot);
            }
            break;
        case Op::Gather:
            for(std::size_t i=0;i<n.indices.size();++i)
                for(std::size_t j=0;j<n.value.cols;++j)
                    nodes_[n.a].grad.at(n.indices[i],j)+=n.grad.at(i,j);
            break;
        case Op::LastRow:
            for(std::size_t j=0;j<n.value.cols;++j)
                nodes_[n.a].grad.at(nodes_[n.a].value.rows-1,j)+=n.grad.at(0,j);
            break;
        case Op::CrossEntropy: {
            const Matrix& x=nodes_[n.a].value;
            double max=*std::max_element(x.values.begin(),x.values.end());
            double denom=0;
            for(double v:x.values)denom+=std::exp(v-max);
            for(std::size_t j=0;j<x.cols;++j) {
                const double derivative=std::exp(x.values[j]-max)/denom-
                                       (j==n.target?1.0:0.0);
                nodes_[n.a].grad.values[j]+=n.grad.values[0]*derivative;
            }
            break;
        }
        }
    }
    for(const auto& node:nodes_)
        for(double g:node.grad.values)
            require(std::isfinite(g),"nonfinite automatic gradient");
}
void sgd_update(Matrix& param,const Matrix& grad,double rate,double clip) {
    valid(param);valid(grad);same(param,grad);
    require(std::isfinite(rate) && rate>0 && rate<=0.2 &&
            std::isfinite(clip) && clip>0 && clip<=100,
            "invalid optimizer learning rate or clip");
    // Stage the whole update first. An overflowing element must not leave a
    // partially modified parameter tensor (important for training retries).
    Matrix candidate=param;
    for(std::size_t i=0;i<candidate.values.size();++i)
        candidate.values[i]-=rate*std::clamp(grad.values[i],-clip,clip);
    valid(candidate);
    param.values.swap(candidate.values);
}
} // namespace noqeri_ai::framework

// SPDX-License-Identifier: Apache-2.0
#include "noqeri/lm.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace noqeri_ai {
namespace {
constexpr std::size_t vocab = 96; // 0 = LF, 1..95 = ASCII 32..126
constexpr std::size_t max_elements = 1048576;
void check(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(std::string("Noqeri LM: ") + reason);
}
bool is_finite(double x) { return std::isfinite(x) && std::abs(x) <= 1e8; }
void validate(const Matrix& m) {
    check(m.rows > 0 && m.cols > 0 && m.rows <= 2048 &&
          m.cols <= 2048 && m.rows <= max_elements / m.cols &&
          m.values.size() == m.rows * m.cols, "invalid matrix shape");
    for (double v : m.values) check(is_finite(v), "nonfinite matrix value");
}
std::size_t token(unsigned char ch) {
    if (ch == '\n') return 0;
    check(ch >= 32 && ch <= 126, "text must contain printable ASCII or LF");
    return static_cast<std::size_t>(ch - 31);
}
char decode(std::size_t id) {
    check(id < vocab, "invalid token");
    return id == 0 ? '\n' : static_cast<char>(id + 31);
}
std::vector<std::size_t> encode(const std::string& text) {
    std::vector<std::size_t> result;
    result.reserve(text.size());
    for (unsigned char c : text) result.push_back(token(c));
    return result;
}
void validate(const LanguageModel& m) {
    check(m.context >= 1 && m.context <= 64 && m.hidden >= 2 && m.hidden <= 64,
          "invalid language model dimensions");
    check(m.embedding.size() == vocab * m.hidden &&
          m.projection.size() == m.hidden * vocab && m.bias.size() == vocab,
          "invalid language model tensors");
    for (double x : m.embedding) check(is_finite(x), "bad embedding");
    for (double x : m.projection) check(is_finite(x), "bad projection");
    for (double x : m.bias) check(is_finite(x), "bad model bias");
}
std::vector<double> distribution(std::vector<double> logits) {
    const double maximum = *std::max_element(logits.begin(), logits.end());
    double sum = 0.0;
    for (double& x : logits) {
        x = std::exp(x - maximum);
        sum += x;
    }
    check(std::isfinite(sum) && sum > 0.0, "unstable softmax");
    for (double& x : logits) x /= sum;
    return logits;
}
std::vector<double> predict_distribution(const LanguageModel& m,
                                         const std::vector<std::size_t>& context) {
    std::vector<double> state(m.hidden, 0.0);
    for (std::size_t t : context)
        for (std::size_t h = 0; h < m.hidden; ++h)
            state[h] += m.embedding[t*m.hidden+h] / static_cast<double>(context.size());
    std::vector<double> logits(m.bias);
    for (std::size_t h = 0; h < m.hidden; ++h)
        for (std::size_t j = 0; j < vocab; ++j)
            logits[j] += state[h] * m.projection[h*vocab+j];
    return distribution(std::move(logits));
}
std::vector<std::size_t> context_at(const std::vector<std::size_t>& data,
                                    std::size_t i, std::size_t window) {
    std::vector<std::size_t> result(window, 0);
    for (std::size_t n = 0; n < window; ++n) {
        const std::size_t back = window - n;
        if (i >= back) result[n] = data[i-back];
    }
    return result;
}
} // namespace

Matrix::Matrix(std::size_t r, std::size_t c, double fill) : rows(r), cols(c) {
    check(r > 0 && c > 0 && r <= 2048 && c <= 2048 &&
          r <= max_elements / c && is_finite(fill), "invalid matrix allocation");
    values.assign(r*c, fill);
}
double& Matrix::at(std::size_t r, std::size_t c) {
    check(r < rows && c < cols && values.size() == rows*cols, "matrix index out of bounds");
    return values[r*cols+c];
}
double Matrix::at(std::size_t r, std::size_t c) const {
    check(r < rows && c < cols && values.size() == rows*cols, "matrix index out of bounds");
    return values[r*cols+c];
}
Matrix matmul(const Matrix& a, const Matrix& b) {
    validate(a); validate(b);
    check(a.cols == b.rows && a.rows <= max_elements / b.cols &&
          a.rows*b.cols*b.rows <= 50000000, "incompatible or oversized matmul");
    Matrix out(a.rows,b.cols);
    for (std::size_t i = 0; i < a.rows; ++i)
        for (std::size_t k = 0; k < a.cols; ++k)
            for (std::size_t j = 0; j < b.cols; ++j)
                out.values[i*b.cols+j] += a.values[i*a.cols+k] * b.values[k*b.cols+j];
    validate(out);
    return out;
}
Matrix softmax_rows(const Matrix& input) {
    validate(input);
    Matrix out(input.rows,input.cols);
    for (std::size_t i = 0; i < input.rows; ++i) {
        std::vector<double> row(input.values.begin()+static_cast<std::ptrdiff_t>(i*input.cols),
                                input.values.begin()+static_cast<std::ptrdiff_t>((i+1)*input.cols));
        auto probs = distribution(std::move(row));
        std::copy(probs.begin(),probs.end(),out.values.begin()+static_cast<std::ptrdiff_t>(i*input.cols));
    }
    return out;
}
Matrix rms_norm_rows(const Matrix& input, double epsilon) {
    validate(input);
    check(epsilon > 0.0 && epsilon <= 1.0 && std::isfinite(epsilon), "invalid epsilon");
    Matrix out(input.rows,input.cols);
    for (std::size_t i = 0; i < input.rows; ++i) {
        double power = 0.0;
        for (std::size_t j = 0; j < input.cols; ++j) {
            const double v = input.at(i,j);
            power += (v*v) / static_cast<double>(input.cols);
        }
        const double inv = 1.0/std::sqrt(power + epsilon);
        for (std::size_t j = 0; j < input.cols; ++j) out.at(i,j) = input.at(i,j)*inv;
    }
    validate(out);
    return out;
}
Matrix causal_attention(const Matrix& q, const Matrix& k, const Matrix& v) {
    validate(q); validate(k); validate(v);
    check(q.rows == k.rows && q.rows == v.rows && q.cols == k.cols &&
          q.rows <= 256, "incompatible attention dimensions or sequence too long");
    Matrix out(q.rows,v.cols);
    const double scale = 1.0/std::sqrt(static_cast<double>(q.cols));
    for (std::size_t i = 0; i < q.rows; ++i) {
        std::vector<double> scores(i+1);
        for (std::size_t j = 0; j <= i; ++j) {
            double dot = 0.0;
            for (std::size_t d = 0; d < q.cols; ++d) dot += q.at(i,d)*k.at(j,d);
            scores[j] = dot*scale;
        }
        auto probs = distribution(std::move(scores));
        for (std::size_t j = 0; j <= i; ++j)
            for (std::size_t d = 0; d < v.cols; ++d)
                out.at(i,d) += probs[j]*v.at(j,d);
    }
    validate(out);
    return out;
}

std::string read_language_corpus(const std::string& filename) {
    check(!filename.empty(), "missing corpus file");
    std::error_code ec;
    const auto size = std::filesystem::file_size(filename,ec);
    check(!ec && size >= 16 && size <= 65536, "corpus must be 16..65536 bytes");
    std::ifstream input(filename,std::ios::binary);
    check(bool(input), "cannot open corpus");
    std::string corpus(static_cast<std::size_t>(size),'\0');
    input.read(&corpus[0],static_cast<std::streamsize>(size));
    check(input.gcount()==static_cast<std::streamsize>(size), "truncated corpus");
    encode(corpus); // validate the full file before using it
    return corpus;
}
LanguageModel train_language(const std::string& corpus, const LanguageOptions& opt,
                             double* trailing_loss) {
    check(corpus.size() >= 16 && corpus.size() <= 65536, "corpus must be 16..65536 bytes");
    check(opt.context >= 1 && opt.context <= 64 && opt.hidden >= 2 && opt.hidden <= 64 &&
          opt.steps >= 1 && opt.steps <= 200000, "invalid training budget or dimensions");
    check(std::isfinite(opt.learning_rate) && opt.learning_rate >= 1e-5 &&
          opt.learning_rate <= 0.2, "learning rate out of range");
    const auto data = encode(corpus);
    LanguageModel m;
    m.context=opt.context; m.hidden=opt.hidden;
    m.embedding.resize(vocab*m.hidden);
    m.projection.resize(m.hidden*vocab);
    m.bias.assign(vocab,0.0);
    std::mt19937 rng(opt.seed);
    std::uniform_real_distribution<double> init(-0.03,0.03);
    for (double& x : m.embedding) x = init(rng);
    for (double& x : m.projection) x = init(rng);
    double last=0.0;
    const auto monitor = std::min<std::size_t>(100,opt.steps);
    for (std::size_t step=0; step<opt.steps; ++step) {
        const std::size_t position = static_cast<std::size_t>(rng()) % data.size();
        const auto context = context_at(data,position,m.context);
        std::vector<double> state(m.hidden,0.0);
        for (std::size_t id : context)
            for (std::size_t h=0; h<m.hidden; ++h)
                state[h] += m.embedding[id*m.hidden+h] / static_cast<double>(m.context);
        std::vector<double> logits(m.bias);
        for (std::size_t h=0; h<m.hidden; ++h)
            for (std::size_t j=0; j<vocab; ++j)
                logits[j] += state[h]*m.projection[h*vocab+j];
        auto errors = distribution(std::move(logits));
        if (step >= opt.steps-monitor)
            last -= std::log(std::max(errors[data[position]],1e-15));
        errors[data[position]] -= 1.0;
        std::vector<double> gradient(m.hidden,0.0);
        for (std::size_t h=0; h<m.hidden; ++h)
            for (std::size_t j=0; j<vocab; ++j)
                gradient[h] += m.projection[h*vocab+j]*errors[j];
        for (std::size_t h=0; h<m.hidden; ++h)
            for (std::size_t j=0; j<vocab; ++j)
                m.projection[h*vocab+j] -= opt.learning_rate*state[h]*errors[j];
        for (std::size_t j=0; j<vocab; ++j) m.bias[j] -= opt.learning_rate*errors[j];
        for (std::size_t id : context)
            for (std::size_t h=0; h<m.hidden; ++h)
                m.embedding[id*m.hidden+h] -= opt.learning_rate*
                    std::clamp(gradient[h],-5.0,5.0)/static_cast<double>(m.context);
    }
    validate(m);
    if (trailing_loss) *trailing_loss=last/static_cast<double>(monitor);
    return m;
}
std::string generate_language(const LanguageModel& m, const std::string& prompt,
                              std::size_t max_tokens, double temperature, std::uint32_t seed) {
    validate(m);
    check(prompt.size() <= 1024 && max_tokens >= 1 && max_tokens <= 256,
          "prompt or generation budget out of range");
    check(std::isfinite(temperature) && temperature >= 0.0 && temperature <= 2.0,
          "temperature must be in 0..2");
    auto tokens = encode(prompt);
    std::mt19937 rng(seed);
    std::string continuation;
    continuation.reserve(max_tokens);
    for (std::size_t n=0; n<max_tokens; ++n) {
        const auto context = context_at(tokens,tokens.size(),m.context);
        auto probabilities = predict_distribution(m,context);
        std::size_t next = 0;
        if (temperature == 0.0) {
            next = static_cast<std::size_t>(
                std::max_element(probabilities.begin(),probabilities.end())-probabilities.begin());
        } else {
            std::vector<double> logits(vocab);
            for (std::size_t j=0; j<vocab; ++j)
                logits[j] = std::log(std::max(probabilities[j],1e-300))/temperature;
            probabilities=distribution(std::move(logits));
            const double selection=(static_cast<double>(rng())+0.5)/4294967296.0;
            double cumulative=0.0;
            next=vocab-1;
            for (std::size_t j=0; j<vocab; ++j) {
                cumulative += probabilities[j];
                if (selection < cumulative) { next=j; break; }
            }
        }
        tokens.push_back(next);
        continuation.push_back(decode(next));
    }
    return continuation;
}
void save_language(const LanguageModel& m, const std::string& filename) {
    validate(m);
    check(!filename.empty(), "missing model filename");
    const auto tmp=filename+".pending";
    {
        std::ofstream out(tmp,std::ios::trunc|std::ios::binary);
        check(bool(out), "cannot write model");
        out<<std::setprecision(17)<<"NQLM1 "<<m.context<<" "<<m.hidden<<"\n";
        for (double v : m.embedding) out<<v<<" ";
        out<<"\n";
        for (double v : m.projection) out<<v<<" ";
        out<<"\n";
        for (double v : m.bias) out<<v<<" ";
        out<<"\n";
        out.flush();
        check(bool(out), "model write failed");
    }
    std::error_code error;
    std::filesystem::rename(tmp,filename,error);
    if(error) { std::filesystem::remove(tmp); throw std::runtime_error("Noqeri LM: cannot atomically install model"); }
}
LanguageModel load_language(const std::string& filename) {
    std::error_code ec;
    const auto size=std::filesystem::file_size(filename,ec);
    check(!ec && size < 1000000, "missing or oversized model file");
    std::ifstream in(filename,std::ios::binary);
    check(bool(in), "cannot read model");
    std::string magic;
    LanguageModel m;
    in>>magic>>m.context>>m.hidden;
    check(bool(in) && magic=="NQLM1" && m.context>=1 && m.context<=64 &&
          m.hidden>=2 && m.hidden<=64, "invalid NQLM1 header");
    m.embedding.resize(vocab*m.hidden);
    m.projection.resize(m.hidden*vocab);
    m.bias.resize(vocab);
    for (double& x : m.embedding) in>>x;
    for (double& x : m.projection) in>>x;
    for (double& x : m.bias) in>>x;
    check(bool(in), "truncated NQLM1 model");
    std::string extra;
    check(!(in>>extra), "unexpected trailing model data");
    validate(m);
    return m;
}
} // namespace noqeri_ai

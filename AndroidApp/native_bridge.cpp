#include "noe.hpp"
#include <jni.h>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::mutex evaluationMutex;

jstring toJava(JNIEnv* env, const std::string& message) {
    return env->NewStringUTF(message.c_str());
}

// Noqeri's interpreter can print through std::cout. Serialize calls so that
// redirecting the output stream never races a second app evaluation.
std::string evaluateSource(const std::string& source, bool execute) {
    if (source.empty()) return "Enter a Noqeri program.";
    if (source.size() > 512 * 1024) return "Source is too large (512 KiB limit).";
    const auto result = noe::compileSource(source);
    if (result.diagnostics.hasErrors()) {
        std::ostringstream errors;
        for (const auto& diagnostic : result.diagnostics.items()) {
            errors << diagnostic.code << " at " << diagnostic.span.line << ':'
                   << diagnostic.span.column << ": " << diagnostic.message << '\n';
            if (!diagnostic.help.empty()) errors << "  " << diagnostic.help << '\n';
        }
        return errors.str();
    }
    if (!execute) return "Noqeri: syntax and type checking passed.";
    std::ostringstream output;
    auto* old = std::cout.rdbuf(output.rdbuf());
    int status = 1;
    try {
        status = noe::Interpreter{}.run(result.nir);
    } catch (...) {
        std::cout.rdbuf(old);
        throw;
    }
    std::cout.rdbuf(old);
    output << "\nExit status: " << status;
    return output.str();
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_noqeri_mobile_MainActivity_evaluate(
    JNIEnv* env, jobject /*activity*/, jstring program, jboolean execute) {
    if (!program) return toJava(env, "No Noqeri source provided.");
    const char* bytes = env->GetStringUTFChars(program, nullptr);
    if (!bytes) return nullptr;
    const std::string source(bytes);
    env->ReleaseStringUTFChars(program, bytes);
    std::lock_guard<std::mutex> guard(evaluationMutex);
    try {
        return toJava(env, evaluateSource(source, execute == JNI_TRUE));
    } catch (const std::exception& error) {
        return toJava(env, std::string("Noqeri runtime error: ") + error.what());
    } catch (...) {
        return toJava(env, "Noqeri runtime failed unexpectedly.");
    }
}

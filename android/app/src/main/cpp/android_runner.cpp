#include <jni.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "builtins.h"
#include "errors.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"

namespace {

std::string fromJString(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* raw = env->GetStringUTFChars(value, nullptr);
    std::string out = raw ? raw : "";
    env->ReleaseStringUTFChars(value, raw);
    return out;
}

jstring toJString(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
}

std::string runUmbrly(const std::string& source, const std::string& input) {
    std::ostringstream output;
    std::istringstream inputStream(input);

    auto* oldCout = std::cout.rdbuf(output.rdbuf());
    auto* oldCin = std::cin.rdbuf(inputStream.rdbuf());

    try {
        std::vector<umbrly::Token> tokens = umbrly::tokenize(source);
        umbrly::Block program = umbrly::parseProgram(tokens);

        umbrly::Interpreter interpreter;
        umbrly::registerCoreBuiltins(interpreter);
        interpreter.setExecutionLimits(5000000, 3000);
        interpreter.run(program);
    } catch (const umbrly::UmbrlyError& error) {
        output << "Error " << error.what() << "\n";
    } catch (const std::exception& error) {
        output << "Error: " << error.what() << "\n";
    } catch (...) {
        output << "Error: unknown native failure\n";
    }

    std::cout.rdbuf(oldCout);
    std::cin.rdbuf(oldCin);
    return output.str();
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_umbrly_sandbox_UmbrlyNative_runScript(
        JNIEnv* env,
        jclass,
        jstring source,
        jstring input) {
    return toJString(env, runUmbrly(fromJString(env, source), fromJString(env, input)));
}

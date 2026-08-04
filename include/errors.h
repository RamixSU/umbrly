// Ошибки времени выполнения и разбора Umbrly.
#pragma once

#include <stdexcept>
#include <string>

namespace umbrly {

struct UmbrlyError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Бросить ошибку без номера строки (номер добавит вызывающий уровень).
[[noreturn]] inline void fail(const std::string& msg) {
    throw UmbrlyError(msg);
}

// Бросить ошибку сразу с номером строки — то, что видит пользователь.
[[noreturn]] inline void failAt(int line, const std::string& msg) {
    throw UmbrlyError("(строка " + std::to_string(line) + "): " + msg);
}

}  // namespace umbrly

#pragma once
#include <stdexcept>
#include <string>

namespace chess {

    // Базовый класс всех ошибок
    class Error : public std::runtime_error {
    public:
        explicit Error(const std::string& what_arg)
            : std::runtime_error(what_arg) {}
    };

    // Ошибка при работе с файлами / ресурсами
    class FileError : public Error {
    public:
        explicit FileError(const std::string& msg) : Error("FileError: " + msg) {}
    };

    // Ошибка при загрузке текстур, моделей и т.п.
    class ResourceError : public Error {
    public:
        explicit ResourceError(const std::string& msg) : Error("ResourceError: " + msg) {}
    };

    // Ошибка логики движка
    class RuleError : public Error {
    public:
        explicit RuleError(const std::string& msg) : Error("RuleError: " + msg) {}
    };

    // Ошибка работы AI
    class EngineError : public Error {
    public:
        explicit EngineError(const std::string& msg) : Error("EngineError: " + msg) {}
    };

} 

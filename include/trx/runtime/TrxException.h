#pragma once

#include <stdexcept>
#include <string>
#include <optional>

namespace trx::runtime {

/**
 * Base exception class for TRX runtime errors.
 * Carries additional context information about the error.
 */
class TrxException : public std::runtime_error {
public:
    TrxException(const std::string& message, 
                 const std::string& errorType = "RuntimeError",
                 std::optional<std::string> sourceLocation = std::nullopt)
        : std::runtime_error(message)
        , errorType_(errorType)
        , sourceLocation_(std::move(sourceLocation)) {}

    const std::string& getErrorType() const { return errorType_; }
    const std::optional<std::string>& getSourceLocation() const { return sourceLocation_; }

private:
    std::string errorType_;
    std::optional<std::string> sourceLocation_;
};

/**
 * Exception thrown by explicit THROW statements in TRX code.
 */
class TrxThrowException : public TrxException {
public:
    TrxThrowException(const JsonValue& thrownValue,
                      std::optional<std::string> sourceLocation = std::nullopt)
        : TrxException("Exception thrown by THROW statement", "ThrowException", std::move(sourceLocation))
        , thrownValue_(thrownValue) {}

    const JsonValue& getThrownValue() const { return thrownValue_; }

private:
    JsonValue thrownValue_;
};

/**
 * Exception for type-related errors.
 */
class TrxTypeException : public TrxException {
public:
    TrxTypeException(const std::string& message,
                     std::optional<std::string> sourceLocation = std::nullopt)
        : TrxException(message, "TypeError", std::move(sourceLocation)) {}
};

/**
 * Exception for arithmetic errors (division by zero, etc.).
 */
class TrxArithmeticException : public TrxException {
public:
    TrxArithmeticException(const std::string& message,
                          std::optional<std::string> sourceLocation = std::nullopt)
        : TrxException(message, "ArithmeticError", std::move(sourceLocation)) {}
};

/**
 * Exception for database-related errors.
 */
class TrxDatabaseException : public TrxException {
public:
    TrxDatabaseException(const std::string& message,
                        std::optional<std::string> sourceLocation = std::nullopt)
        : TrxException(message, "DatabaseError", std::move(sourceLocation)) {}
};

} // namespace trx::runtime
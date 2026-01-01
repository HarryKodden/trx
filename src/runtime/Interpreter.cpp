#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-result"

#include "trx/runtime/Interpreter.h"

#include "trx/ast/Expressions.h"
#include "trx/ast/Statements.h"

#include "trx/runtime/DatabaseDriver.h"
#include "trx/runtime/SQLiteDriver.h"
#include "trx/runtime/TrxException.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <sstream>
#include <curl/curl.h>
#include <map>

// Callback function for curl to write response data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Helper function to convert JsonValue to string
static std::string jsonValueToString(const trx::runtime::JsonValue& value) {
    std::stringstream ss;
    ss << value;
    return ss.str();
}

namespace trx::runtime {



namespace {

struct ExecutionContext {
    Interpreter &interpreter;
    std::unordered_map<std::string, JsonValue> variables;
    bool returned{false};
    std::optional<JsonValue> returnValue;
    bool isGlobal{false};
    bool isFunction{false};
    std::optional<std::string> outputType;
};

void debugPrint(const std::string& message) {
    if (getenv("DEBUG")) {
        std::cout << message << std::endl;
    }
}

JsonValue evaluateExpression(const trx::ast::ExpressionPtr &expression, ExecutionContext &context);
JsonValue resolveVariableValue(const trx::ast::VariableExpression &variable, ExecutionContext &context);
JsonValue &resolveVariableTarget(const trx::ast::VariableExpression &variable, ExecutionContext &context);

void executeStatements(const trx::ast::StatementList &statements, ExecutionContext &context);

std::vector<JsonValue> resolveHostVariablesFromAst(const std::vector<trx::ast::VariableExpression>& hostVariables, ExecutionContext &context);

std::vector<SqlParameter> convertHostVarsToParams(const std::vector<JsonValue>& hostVars) {
    std::vector<SqlParameter> params;
    int index = 1;
    for (const auto& value : hostVars) {
        params.push_back({std::to_string(index), value});
        ++index;
    }
    return params;
}

std::vector<JsonValue> resolveHostVariablesFromAst(const std::vector<trx::ast::VariableExpression>& hostVariables, ExecutionContext &context) {
    std::vector<JsonValue> hostVars;
    for (const auto& varExpr : hostVariables) {
        try {
            JsonValue value = resolveVariableValue(varExpr, context);
            hostVars.push_back(value);
        } catch (const std::exception& e) {
            std::cerr << "Failed to resolve host variable: " << e.what() << std::endl;
            // Continue with other variables
        }
    }
    return hostVars;
}

[[maybe_unused]] std::optional<std::string> formatSourceLocation(const trx::ast::SourceLocation& location) {
    if (location.file.empty()) {
        return std::nullopt;
    }
    return std::string(location.file) + ":" + std::to_string(location.line) + ":" + std::to_string(location.column);
}

JsonValue evaluateLiteral(const trx::ast::LiteralExpression &literal) {
    return std::visit(
        Overloaded{
            [](double value) { return JsonValue(value); },
            [](const std::string &value) { return JsonValue(value); },
            [](bool value) { return JsonValue(value); }
        },
        literal.value);
}

JsonValue evaluateUnary(const trx::ast::UnaryExpression &unary, ExecutionContext &context) {
    JsonValue operand = evaluateExpression(unary.operand, context);
    switch (unary.op) {
        case trx::ast::UnaryOperator::Positive:
            if (std::holds_alternative<double>(operand.data)) {
                return operand;
            }
            throw TrxTypeException("Positive operator requires numeric operand");
        case trx::ast::UnaryOperator::Negate:
            if (std::holds_alternative<double>(operand.data)) {
                return JsonValue(-std::get<double>(operand.data));
            }
            throw TrxTypeException("Negate operator requires numeric operand");
        case trx::ast::UnaryOperator::Not:
            if (std::holds_alternative<bool>(operand.data)) {
                return JsonValue(!std::get<bool>(operand.data));
            }
            throw TrxTypeException("Not operator requires boolean operand");
    }
    throw TrxException("Unknown unary operator");
}

JsonValue evaluateBinary(const trx::ast::BinaryExpression &binary, ExecutionContext &context) {
    JsonValue lhs = evaluateExpression(binary.lhs, context);
    JsonValue rhs = evaluateExpression(binary.rhs, context);

    switch (binary.op) {
        case trx::ast::BinaryOperator::Add:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) + std::get<double>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
                return JsonValue(std::get<std::string>(lhs.data) + std::get<std::string>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data)) {
                std::ostringstream oss;
                oss << rhs;
                return JsonValue(std::get<std::string>(lhs.data) + oss.str());
            }
            if (std::holds_alternative<std::string>(rhs.data)) {
                std::ostringstream oss;
                oss << lhs;
                return JsonValue(oss.str() + std::get<std::string>(rhs.data));
            }
            throw TrxTypeException("Add operator requires compatible operands");
        case trx::ast::BinaryOperator::Subtract:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) - std::get<double>(rhs.data));
            }
            throw TrxTypeException("Subtract operator requires numeric operands");
        case trx::ast::BinaryOperator::Multiply:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) * std::get<double>(rhs.data));
            }
            throw TrxTypeException("Multiply operator requires numeric operands");
        case trx::ast::BinaryOperator::Divide:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                double r = std::get<double>(rhs.data);
                if (r == 0.0) throw TrxArithmeticException("Division by zero");
                return JsonValue(std::get<double>(lhs.data) / r);
            }
            throw TrxTypeException("Divide operator requires numeric operands");
        case trx::ast::BinaryOperator::Modulo:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::fmod(std::get<double>(lhs.data), std::get<double>(rhs.data)));
            }
            throw TrxTypeException("Modulo operator requires numeric operands");
        case trx::ast::BinaryOperator::Equal:
            return JsonValue(lhs.data == rhs.data);
        case trx::ast::BinaryOperator::NotEqual:
            return JsonValue(lhs.data != rhs.data);
        case trx::ast::BinaryOperator::Less:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) < std::get<double>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
                return JsonValue(std::get<std::string>(lhs.data) < std::get<std::string>(rhs.data));
            }
            throw std::runtime_error("Less operator requires comparable operands");
        case trx::ast::BinaryOperator::LessEqual:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) <= std::get<double>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
                return JsonValue(std::get<std::string>(lhs.data) <= std::get<std::string>(rhs.data));
            }
            throw std::runtime_error("LessEqual operator requires comparable operands");
        case trx::ast::BinaryOperator::Greater:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) > std::get<double>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
                return JsonValue(std::get<std::string>(lhs.data) > std::get<std::string>(rhs.data));
            }
            throw std::runtime_error("Greater operator requires comparable operands");
        case trx::ast::BinaryOperator::GreaterEqual:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) >= std::get<double>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
                return JsonValue(std::get<std::string>(lhs.data) >= std::get<std::string>(rhs.data));
            }
            throw std::runtime_error("GreaterEqual operator requires comparable operands");
        case trx::ast::BinaryOperator::And:
            if (std::holds_alternative<bool>(lhs.data) && std::holds_alternative<bool>(rhs.data)) {
                return JsonValue(std::get<bool>(lhs.data) && std::get<bool>(rhs.data));
            }
            throw std::runtime_error("And operator requires boolean operands");
        case trx::ast::BinaryOperator::Or:
            if (std::holds_alternative<bool>(lhs.data) && std::holds_alternative<bool>(rhs.data)) {
                return JsonValue(std::get<bool>(lhs.data) || std::get<bool>(rhs.data));
            }
            throw std::runtime_error("Or operator requires boolean operands");
    }
    throw std::runtime_error("Unknown binary operator");
}

JsonValue evaluateFunctionCall(const trx::ast::FunctionCallExpression &call, ExecutionContext &context) {
    // For now, implement some built-in functions
    if (call.functionName == "length" || call.functionName == "len") {
        if (call.arguments.size() != 1) throw std::runtime_error("length/len function takes 1 argument");
        JsonValue arg = evaluateExpression(call.arguments[0], context);
        if (std::holds_alternative<std::string>(arg.data)) {
            return JsonValue(static_cast<double>(std::get<std::string>(arg.data).size()));
        }
        if (arg.isArray()) {
            return JsonValue(static_cast<double>(arg.asArray().size()));
        }
        throw std::runtime_error("length/len function requires string or array");
    }
    if (call.functionName == "append") {
        if (call.arguments.size() != 2) throw std::runtime_error("append function takes 2 arguments");
        if (const auto *var = std::get_if<trx::ast::VariableExpression>(&call.arguments[0]->node)) {
            JsonValue &list = resolveVariableTarget(*var, context);
            JsonValue item = evaluateExpression(call.arguments[1], context);
            if (!list.isArray()) throw std::runtime_error("first argument to append must be an array");
            list.asArray().push_back(item);
            return JsonValue(nullptr);
        } else {
            throw std::runtime_error("append first argument must be a variable");
        }
    }
    if (call.functionName == "substr") {
        if (call.arguments.size() != 3) throw std::runtime_error("substr function takes 3 arguments");
        JsonValue str = evaluateExpression(call.arguments[0], context);
        JsonValue start = evaluateExpression(call.arguments[1], context);
        JsonValue len = evaluateExpression(call.arguments[2], context);
        if (!std::holds_alternative<std::string>(str.data) || !std::holds_alternative<double>(start.data) || !std::holds_alternative<double>(len.data)) {
            throw std::runtime_error("substr arguments must be string, number, number");
        }
        const std::string &s = std::get<std::string>(str.data);
        size_t pos = static_cast<size_t>(std::get<double>(start.data));
        size_t length = static_cast<size_t>(std::get<double>(len.data));
        if (pos >= s.size()) return JsonValue("");
        return JsonValue(s.substr(pos, length));
    }
    if (call.functionName == "debug") {
        if (call.arguments.size() != 1) throw std::runtime_error("debug function takes 1 argument");
        JsonValue arg = evaluateExpression(call.arguments[0], context);
        // std::cout << "DEBUG: " << arg << std::endl;
        return JsonValue(nullptr); // Logging functions return null
    }
    if (call.functionName == "info") {
        if (call.arguments.size() != 1) throw std::runtime_error("info function takes 1 argument");
        JsonValue arg = evaluateExpression(call.arguments[0], context);
        // std::cout << "INFO: " << arg << std::endl;
        return JsonValue(nullptr); // Logging functions return null
    }
    if (call.functionName == "error") {
        if (call.arguments.size() != 1) throw std::runtime_error("error function takes 1 argument");
        JsonValue arg = evaluateExpression(call.arguments[0], context);
        // std::cerr << "ERROR: " << arg << std::endl;
        return JsonValue(nullptr); // Logging functions return null
    }
    if (call.functionName == "trace") {
        if (call.arguments.size() != 1) throw std::runtime_error("trace function takes 1 argument");
        JsonValue arg = evaluateExpression(call.arguments[0], context);
        // std::cout << "TRACE: " << arg << std::endl;
        return JsonValue(nullptr); // Logging functions return null
    }
    if (call.functionName == "http") {
        if (call.arguments.size() != 1) throw std::runtime_error("http function takes 1 argument");
        JsonValue config = evaluateExpression(call.arguments[0], context);
        if (!config.isObject()) throw std::runtime_error("http argument must be an object");

        // Extract configuration
        const auto& configObj = config.asObject();
        
        // Required: method and url
        if (configObj.find("method") == configObj.end()) throw std::runtime_error("http config must include 'method'");
        if (configObj.find("url") == configObj.end()) throw std::runtime_error("http config must include 'url'");
        
        std::string method = configObj.at("method").asString();
        std::string url = configObj.at("url").asString();
        
        // Optional: headers, body, timeout
        std::map<std::string, std::string> headers;
        std::string requestBody;
        long timeout = 30L; // default 30 seconds
        
        if (configObj.find("headers") != configObj.end() && configObj.at("headers").isObject()) {
            const auto& headersObj = configObj.at("headers").asObject();
            for (const auto& [key, value] : headersObj) {
                headers[key] = value.asString();
            }
        }
        
        if (configObj.find("body") != configObj.end()) {
            requestBody = jsonValueToString(configObj.at("body"));
        }
        
        if (configObj.find("timeout") != configObj.end()) {
            timeout = static_cast<long>(configObj.at("timeout").asNumber());
        }

        // Initialize curl
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Failed to initialize HTTP client");

        std::string responseBody;
        std::string responseHeaders;
        long responseCode = 0;
        char errorBuffer[CURL_ERROR_SIZE];
        errorBuffer[0] = 0;

        try {
            // Set URL
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            
            // Set method
            if (method == "GET") {
                curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            } else if (method == "POST") {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                if (!requestBody.empty()) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
                }
            } else if (method == "PUT") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
                if (!requestBody.empty()) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
                }
            } else if (method == "DELETE") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            } else if (method == "PATCH") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
                if (!requestBody.empty()) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
                }
            } else if (method == "HEAD") {
                curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            } else if (method == "OPTIONS") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
            } else {
                curl_easy_cleanup(curl);
                throw std::runtime_error("Unsupported HTTP method: " + method);
            }

            // Set headers
            struct curl_slist* headerList = nullptr;
            for (const auto& [key, value] : headers) {
                std::string header = key + ": " + value;
                headerList = curl_slist_append(headerList, header.c_str());
            }
            if (headerList) {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
            }

            // Set callbacks
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
            
            // Set timeout
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
            
            // Set error buffer
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
            
            // Don't verify SSL certificates for now (in production, this should be configurable)
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

            // Perform request
            CURLcode res = curl_easy_perform(curl);
            
            if (res != CURLE_OK) {
                std::string errorMsg = errorBuffer[0] ? errorBuffer : curl_easy_strerror(res);
                curl_easy_cleanup(curl);
                if (headerList) curl_slist_free_all(headerList);
                throw std::runtime_error("HTTP request failed: " + errorMsg);
            }

            // Get response code
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

            // Cleanup
            curl_easy_cleanup(curl);
            if (headerList) curl_slist_free_all(headerList);

            // Parse response body as JSON if possible
            JsonValue responseBodyJson;
            if (!responseBody.empty() && (responseBody[0] == '{' || responseBody[0] == '[')) {
                try {
                    // For now, store as string - TRX can parse JSON at runtime if needed
                    responseBodyJson = JsonValue(responseBody);
                } catch (...) {
                    // If anything fails, treat as string
                    responseBodyJson = JsonValue(responseBody);
                }
            } else {
                // Not JSON, treat as string
                responseBodyJson = JsonValue(responseBody);
            }

            // Build response
            JsonValue::Object response;
            response["status"] = JsonValue(static_cast<double>(responseCode));
            response["headers"] = JsonValue(JsonValue::Object{{"content-type", JsonValue("application/json")}});
            response["body"] = responseBodyJson;
            
            return JsonValue(response);
            
        } catch (...) {
            curl_easy_cleanup(curl);
            throw;
        }
    }
    // For user-defined procedures
    const auto *proc = context.interpreter.getProcedure(call.functionName);
    if (proc) {
        // if (!proc->output) {
        //     throw std::runtime_error("Procedure calls cannot be used in expressions");
        // }
        bool hasInput = proc->input.has_value();
        if (hasInput) {
            if (call.arguments.size() != 1) throw std::runtime_error("Function call expects 1 argument");
            JsonValue arg = evaluateExpression(call.arguments[0], context);
            auto result = context.interpreter.execute(call.functionName, arg);
            return result.value_or(JsonValue(nullptr));
        } else {
            if (call.arguments.size() != 0) throw std::runtime_error("Function call expects no arguments");
            auto result = context.interpreter.execute(call.functionName, JsonValue(nullptr));
            return result.value_or(JsonValue(nullptr));
        }
    }
    throw std::runtime_error("Function not supported: " + call.functionName);
}

JsonValue evaluateMethodCall(const trx::ast::MethodCallExpression &call, ExecutionContext &context) {
    JsonValue object = evaluateExpression(call.object, context);
    std::vector<JsonValue> args;
    for (const auto &arg : call.arguments) {
        args.push_back(evaluateExpression(arg, context));
    }
    
    if (object.isArray()) {
        if (call.methodName == "append") {
            if (args.size() != 1) throw std::runtime_error("append method takes 1 argument");
            object.asArray().push_back(args[0]);
            return JsonValue(nullptr); // Methods that modify return null
        }
        if (call.methodName == "length") {
            return JsonValue(static_cast<double>(object.asArray().size()));
        }
    }
    std::string typeName = object.isArray() ? "array" : object.isObject() ? "object" : "value";
    throw std::runtime_error("Method not supported: " + call.methodName + " on " + typeName);
}

JsonValue evaluateBuiltin(const trx::ast::BuiltinExpression &builtin, ExecutionContext &context) {
    (void)context;
    switch (builtin.value) {
        case trx::ast::BuiltinValue::SqlCode:
            return JsonValue(context.interpreter.getSqlCode());
        case trx::ast::BuiltinValue::Date: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm *tm = std::localtime(&time);
            char buf[11];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
            return JsonValue(buf);
        }
        case trx::ast::BuiltinValue::Time: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm *tm = std::localtime(&time);
            char buf[9];
            std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
            return JsonValue(buf);
        }
        case trx::ast::BuiltinValue::Week: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm *tm = std::localtime(&time);
            return JsonValue(static_cast<double>(tm->tm_wday));
        }
        case trx::ast::BuiltinValue::WeekDay: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm *tm = std::localtime(&time);
            return JsonValue(static_cast<double>(tm->tm_wday + 1)); // 1-7
        }
        case trx::ast::BuiltinValue::TimeStamp: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            return JsonValue(static_cast<double>(time));
        }
    }
    throw std::runtime_error("Unknown builtin");
}

JsonValue evaluateSqlFragment(const trx::ast::SqlFragmentExpression &sql, ExecutionContext &context) {
    std::string result;
    for (const auto &fragment : sql.fragments) {
        std::visit(
            Overloaded{
                [&](const std::string &str) { result += str; },
                [&](const trx::ast::VariableExpression &var) {
                    JsonValue val = resolveVariableValue(var, context);
                    // Convert to string
                    if (std::holds_alternative<double>(val.data)) {
                        result += std::to_string(std::get<double>(val.data));
                    } else if (std::holds_alternative<std::string>(val.data)) {
                        result += std::get<std::string>(val.data);
                    } else {
                        throw std::runtime_error("Cannot convert variable to string in SQL fragment");
                    }
                }
            },
            fragment.value);
    }
    return JsonValue(result);
}

struct ExpressionEvaluator {
    ExecutionContext &context;

    JsonValue operator()(const trx::ast::LiteralExpression &literal) const;
    JsonValue operator()(const trx::ast::ObjectLiteralExpression &object) const;
    JsonValue operator()(const trx::ast::ArrayLiteralExpression &array) const;
    JsonValue operator()(const trx::ast::VariableExpression &variable) const;
    JsonValue operator()(const trx::ast::UnaryExpression &unary) const;
    JsonValue operator()(const trx::ast::BinaryExpression &binary) const;
    JsonValue operator()(const trx::ast::FunctionCallExpression &call) const;
    JsonValue operator()(const trx::ast::MethodCallExpression &call) const;
    JsonValue operator()(const trx::ast::BuiltinExpression &builtin) const;
    JsonValue operator()(const trx::ast::SqlFragmentExpression &sql) const;
};

JsonValue ExpressionEvaluator::operator()(const trx::ast::LiteralExpression &literal) const {
    return evaluateLiteral(literal);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::ObjectLiteralExpression &object) const {
    JsonValue::Object obj;
    for (const auto &[key, value] : object.properties) {
        obj[key] = evaluateExpression(value, context);
    }
    return JsonValue(obj);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::ArrayLiteralExpression &array) const {
    JsonValue::Array arr;
    for (const auto &element : array.elements) {
        arr.push_back(evaluateExpression(element, context));
    }
    return JsonValue(arr);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::VariableExpression &variable) const {
    return resolveVariableValue(variable, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::UnaryExpression &unary) const {
    return evaluateUnary(unary, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::BinaryExpression &binary) const {
    return evaluateBinary(binary, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::FunctionCallExpression &call) const {
    return evaluateFunctionCall(call, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::MethodCallExpression &call) const {
    return evaluateMethodCall(call, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::BuiltinExpression &builtin) const {
    return evaluateBuiltin(builtin, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::SqlFragmentExpression &sql) const {
    return evaluateSqlFragment(sql, context);
}

JsonValue evaluateExpression(const trx::ast::ExpressionPtr &expression, ExecutionContext &context) {
    if (!expression) {
        throw std::runtime_error("Attempted to evaluate empty expression");
    }

    return std::visit(ExpressionEvaluator{context}, expression->node);
}

JsonValue resolveVariableValue(const trx::ast::VariableExpression &variable, ExecutionContext &context) {
    if (variable.path.empty()) {
        throw std::runtime_error("Variable expression is empty");
    }

    // Check for implicit input/output usage
    const std::string &rootVar = variable.path.front().identifier;
    if (rootVar == "input" || rootVar == "output") {
        throw std::runtime_error("Implicit '" + rootVar + "' variable is not allowed. Declare variables explicitly.");
    }

    const JsonValue *current = nullptr;
    std::string rootVarLower = variable.path.front().identifier;
    std::transform(rootVarLower.begin(), rootVarLower.end(), rootVarLower.begin(), [](unsigned char c) { return std::tolower(c); });
    auto rootIt = context.variables.find(rootVarLower);
    if (rootIt == context.variables.end()) {
        // Check global variables
        auto globalIt = context.interpreter.globalVariables().find(rootVarLower);
        if (globalIt != context.interpreter.globalVariables().end()) {
            current = &globalIt->second;
        }
    } else {
        current = &rootIt->second;
    }

    for (std::size_t i = 0; i < variable.path.size(); ++i) {
        const auto &segment = variable.path[i];
        if (segment.subscript.has_value()) {
            if (!current->isArray()) {
                throw std::runtime_error("Attempted to subscript a non-array value");
            }
            const auto &array = current->asArray();
            JsonValue indexValue = evaluateExpression(segment.subscript.value(), context);
            if (!std::holds_alternative<double>(indexValue.data)) {
                throw std::runtime_error("Array index must be a number");
            }
            size_t index = static_cast<size_t>(std::get<double>(indexValue.data));
            if (index >= array.size()) {
                throw std::runtime_error("Array index out of bounds");
            }
            current = &array[index];
        } else {
            if (i > 0) {  // Not root
                if (!current->isObject()) {
                    throw std::runtime_error("Attempted to access field on non-object value");
                }
                const auto &object = current->asObject();
                std::string fieldLower = segment.identifier;
                std::transform(fieldLower.begin(), fieldLower.end(), fieldLower.begin(), [](unsigned char c) { return std::tolower(c); });
                const auto childIt = object.find(fieldLower);
                if (childIt == object.end()) {
                    throw std::runtime_error("Unknown field: " + segment.identifier);
                }
                current = &childIt->second;
            }
        }
    }

    return *current;
}

JsonValue &resolveVariableTarget(const trx::ast::VariableExpression &variable, ExecutionContext &context) {
    if (variable.path.empty()) {
        throw std::runtime_error("Variable expression is empty");
    }

    // Check for implicit input/output usage
    const std::string &rootVar = variable.path.front().identifier;
    if (rootVar == "input" || rootVar == "output") {
        throw std::runtime_error("Implicit '" + rootVar + "' variable is not allowed. Declare variables explicitly.");
    }

    JsonValue *current = nullptr;
    JsonValue *root = nullptr;
    std::string rootVarLower = variable.path.front().identifier;
    std::transform(rootVarLower.begin(), rootVarLower.end(), rootVarLower.begin(), [](unsigned char c) { return std::tolower(c); });
    auto localIt = context.variables.find(rootVarLower);
    if (localIt != context.variables.end()) {
        root = &localIt->second;
    } else {
        auto globalIt = context.interpreter.globalVariables().find(rootVarLower);
        if (globalIt != context.interpreter.globalVariables().end()) {
            root = &globalIt->second;
        } else {
            // Create in local variables
            root = &context.variables[rootVarLower];
        }
    }
    current = root;

    for (std::size_t i = 0; i < variable.path.size(); ++i) {
        const auto &segment = variable.path[i];
        if (segment.subscript.has_value()) {
            if (!current->isArray()) {
                *current = JsonValue::array();
            }
            auto &array = current->asArray();
            JsonValue indexValue = evaluateExpression(segment.subscript.value(), context);
            if (!std::holds_alternative<double>(indexValue.data)) {
                throw std::runtime_error("Array index must be a number");
            }
            size_t index = static_cast<size_t>(std::get<double>(indexValue.data));
            if (index >= array.size()) {
                array.resize(index + 1);
            }
            current = &array[index];
        } else {
            if (i > 0) {  // Not root
                if (!current->isObject()) {
                    *current = JsonValue::object();
                }
                auto &object = current->asObject();
                std::string fieldLower = segment.identifier;
                std::transform(fieldLower.begin(), fieldLower.end(), fieldLower.begin(), [](unsigned char c) { return std::tolower(c); });
                current = &object[fieldLower];
            }
        }
    }

    return *current;
}

void executeAssignment(const trx::ast::AssignmentStatement &assignment, ExecutionContext &context) {
    debugPrint("ASSIGNMENT: evaluating value for assignment");
    JsonValue value = evaluateExpression(assignment.value, context);
    debugPrint("ASSIGNMENT: value evaluated to " + jsonValueToString(value));
    debugPrint("ASSIGNMENT: resolving target");
    JsonValue &target = resolveVariableTarget(assignment.target, context);
    debugPrint("ASSIGNMENT: target resolved, assigning");
    target = std::move(value);
    debugPrint("ASSIGNMENT: assignment complete");
}

void executeVariableDeclaration(const trx::ast::VariableDeclarationStatement &varDecl, ExecutionContext &context) {
    JsonValue initialValue = JsonValue(nullptr);
    if (varDecl.initializer) {
        initialValue = evaluateExpression(*varDecl.initializer, context);
    } else if (!varDecl.typeName.empty()) {
        if (varDecl.typeName.substr(0, 5) == "LIST(") {
            // Initialize list variables as empty arrays
            initialValue = JsonValue(JsonValue::Array{});
        } else if (varDecl.typeName.substr(0, 5) == "CHAR(") {
            // Initialize CHAR(n) variables as strings of n spaces
            size_t start = varDecl.typeName.find('(');
            size_t end = varDecl.typeName.find(')');
            if (start != std::string::npos && end != std::string::npos && end > start + 1) {
                std::string lenStr = varDecl.typeName.substr(start + 1, end - start - 1);
                try {
                    int len = std::stoi(lenStr);
                    initialValue = JsonValue(std::string(len, ' '));
                } catch (const std::exception&) {
                    // If parsing fails, leave as nullptr
                }
            }
        } else if (varDecl.typeName == "JSON") {
            // Initialize JSON variables as null
            initialValue = JsonValue(nullptr);
        } else if (auto record = context.interpreter.getRecord(varDecl.typeName)) {
            // Initialize record variables with fields set to null
            JsonValue::Object obj;
            for (const auto& field : record->fields) {
                obj[field.name.name] = JsonValue(nullptr);
            }
            initialValue = JsonValue(obj);
        }
    } else if (varDecl.tableName.has_value()) {
        // Initialize table-based variables from database schema
        auto columns = context.interpreter.db().getTableSchema(*varDecl.tableName);
        JsonValue::Object obj;
        for (const auto& column : columns) {
            obj[column.name] = JsonValue(nullptr);
        }
        initialValue = JsonValue(obj);
    }
    if (context.isGlobal) {
        context.interpreter.globalVariables()[varDecl.name.name] = initialValue;
    } else {
        context.variables[varDecl.name.name] = initialValue;
    }
}

void executeIf(const trx::ast::IfStatement &ifStmt, ExecutionContext &context) {
    JsonValue cond = evaluateExpression(ifStmt.condition, context);
    if (std::holds_alternative<bool>(cond.data) && std::get<bool>(cond.data)) {
        executeStatements(ifStmt.thenBranch, context);
    } else {
        executeStatements(ifStmt.elseBranch, context);
    }
}

void executeThrow(const trx::ast::ThrowStatement &throwStmt, ExecutionContext &context) {
    JsonValue value = evaluateExpression(throwStmt.value, context);
    throw TrxThrowException(value, std::nullopt);
}

void executeTryCatch(const trx::ast::TryCatchStatement &tryCatchStmt, ExecutionContext &context) {
    try {
        executeStatements(tryCatchStmt.tryBlock, context);
    } catch (const TrxException &e) {
        // Bind the exception to the catch variable if specified
        if (tryCatchStmt.exceptionVar) {
            JsonValue exceptionValue = JsonValue::object();
            exceptionValue.asObject()["type"] = e.getErrorType();
            exceptionValue.asObject()["message"] = std::string(e.what());
            if (e.getSourceLocation()) {
                exceptionValue.asObject()["location"] = *e.getSourceLocation();
            }
            
            // If it's a TrxThrowException, include the thrown value
            if (const TrxThrowException* throwEx = dynamic_cast<const TrxThrowException*>(&e)) {
                exceptionValue.asObject()["value"] = throwEx->getThrownValue();
            }
            
            context.variables[tryCatchStmt.exceptionVar->path.back().identifier] = exceptionValue;
        }
        executeStatements(tryCatchStmt.catchBlock, context);
    }
}

void executeWhile(const trx::ast::WhileStatement &whileStmt, ExecutionContext &context) {
    static const int MAX_ITERATIONS = []() {
        const char* env = std::getenv("TRX_WHILE_MAX_ITERATIONS");
        if (env) {
            try {
                int val = std::stoi(env);
                return val > 0 ? val : 10000;
            } catch (...) {
                return 10000;
            }
        }
        return 10000;
    }();
    
    int iterations = 0;
    while (true) {
        if (++iterations > MAX_ITERATIONS) {
            throw std::runtime_error("WHILE loop exceeded maximum iterations (" + std::to_string(MAX_ITERATIONS) + ")");
        }
        
        JsonValue cond = evaluateExpression(whileStmt.condition, context);
        if (!std::holds_alternative<bool>(cond.data) || !std::get<bool>(cond.data)) break;
        executeStatements(whileStmt.body, context);
    }
}

void executeFor(const trx::ast::ForStatement &forStmt, ExecutionContext &context) {
    JsonValue collection = evaluateExpression(forStmt.collection, context);
    if (!std::holds_alternative<std::vector<JsonValue>>(collection.data)) {
        throw std::runtime_error("FOR loop collection must be an array");
    }
    const auto& arr = std::get<std::vector<JsonValue>>(collection.data);
    for (const auto& item : arr) {
        resolveVariableTarget(forStmt.loopVar, context) = item;
        executeStatements(forStmt.body, context);
    }
}

void executeBlock(const trx::ast::BlockStatement &block, ExecutionContext &context) {
    executeStatements(block.statements, context);
}

void executeSwitch(const trx::ast::SwitchStatement &switchStmt, ExecutionContext &context) {
    JsonValue selector = evaluateExpression(switchStmt.selector, context);
    for (const auto &case_ : switchStmt.cases) {
        JsonValue match = evaluateExpression(case_.match, context);
        if (selector.data == match.data) {
            executeStatements(case_.body, context);
            return;
        }
    }
    if (switchStmt.defaultBranch) {
        executeStatements(*switchStmt.defaultBranch, context);
    }
}

void executeSort(const trx::ast::SortStatement &sortStmt, ExecutionContext &context) {
    JsonValue &arrayValue = resolveVariableTarget(sortStmt.array, context);
    if (!arrayValue.isArray()) {
        throw std::runtime_error("Sort target must be an array");
    }
    auto &array = arrayValue.asArray();
    if (sortStmt.keys.empty()) return; // No sort
    // For simplicity, sort by first key ascending
    const auto key = sortStmt.keys.front();
    std::sort(array.begin(), array.end(), [key](const JsonValue &a, const JsonValue &b) {
        if (!a.isObject() || !b.isObject()) return false;
        const auto &objA = a.asObject();
        const auto &objB = b.asObject();
        auto itA = objA.find(key.fieldName);
        auto itB = objB.find(key.fieldName);
        if (itA == objA.end() || itB == objB.end()) return false;
        // Compare based on type
        if (std::holds_alternative<double>(itA->second.data) && std::holds_alternative<double>(itB->second.data)) {
            double valA = std::get<double>(itA->second.data);
            double valB = std::get<double>(itB->second.data);
            return key.order > 0 ? valA < valB : valA > valB;
        }
        if (std::holds_alternative<std::string>(itA->second.data) && std::holds_alternative<std::string>(itB->second.data)) {
            const std::string &valA = std::get<std::string>(itA->second.data);
            const std::string &valB = std::get<std::string>(itB->second.data);
            return key.order > 0 ? valA < valB : valA > valB;
        }
        return false;
    });
}

void executeTrace(const trx::ast::TraceStatement &trace, ExecutionContext &context) {
    JsonValue val = evaluateExpression(trace.value, context);
    debugPrint("TRACE: " + jsonValueToString(val));
}

void executeExpression(const trx::ast::ExpressionStatement &exprStmt, ExecutionContext &context) {
    // Evaluate the expression and discard the result
    evaluateExpression(exprStmt.expression, context);
}

void executeSystem(const trx::ast::SystemStatement &systemStmt, ExecutionContext &context) {
    JsonValue cmd = evaluateExpression(systemStmt.command, context);
    if (std::holds_alternative<std::string>(cmd.data)) {
        const std::string &command = std::get<std::string>(cmd.data);
        int result = std::system(command.c_str());
        (void)result;
    } else {
        throw std::runtime_error("System command must be a string");
    }
}

void executeBatch(const trx::ast::BatchStatement &batchStmt, ExecutionContext &context) {
    // For now, just print that batch is called
    std::string msg = "BATCH: " + batchStmt.name;
    if (batchStmt.argument) {
        JsonValue arg = resolveVariableValue(*batchStmt.argument, context);
        msg += " with argument: " + jsonValueToString(arg);
    }
    debugPrint(msg);
    // In a real implementation, this would execute the batch process
}

struct ReturnException : std::exception {
    JsonValue value;
    ReturnException(JsonValue v) : value(std::move(v)) {}
};

void executeReturn(const trx::ast::ReturnStatement &returnStmt, ExecutionContext &context) {
    if (context.isFunction) {
        // Functions must return a value
        if (!returnStmt.value) {
            throw std::runtime_error("Function must return a value");
        }
        // For functions, evaluate the return expression
        JsonValue val = evaluateExpression(returnStmt.value, context);
        throw ReturnException(val);
    } else {
        // Procedures can have RETURN but without a value
        if (returnStmt.value) {
            throw std::runtime_error("Procedures cannot return values. Use RETURN without a value.");
        }
        // Just mark that we returned (for procedures, this ends execution)
        context.returned = true;
        throw ReturnException(JsonValue(nullptr));  // Use null to indicate procedure return
    }
}

void executeValidate(const trx::ast::ValidateStatement &validateStmt, ExecutionContext &context) {
    JsonValue var = resolveVariableValue(validateStmt.variable, context);
    JsonValue ruleResult = evaluateExpression(validateStmt.rule, context);
    // Assume ruleResult is boolean
    bool valid = std::holds_alternative<bool>(ruleResult.data) && std::get<bool>(ruleResult.data);
    const trx::ast::ValidationOutcome &outcome = valid ? validateStmt.success : validateStmt.failure;
    // For now, just print
    debugPrint("VALIDATE: " + std::string(valid ? "SUCCESS" : "FAILURE") + " code=" + std::to_string(outcome.code) + " message=\"" + outcome.message + "\"");
    // Set final outcome
    // Perhaps set a variable, but for now, ignore
}


std::string extractSelectFromDeclare(const std::string &declareSql) {
    debugPrint("DEBUG extractSelectFromDeclare: Input: '" + declareSql + "'");
    std::string upper = declareSql;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    // Find "CURSOR FOR"
    size_t cursorForPos = upper.find("CURSOR FOR");
    if (cursorForPos == std::string::npos) {
        return declareSql; // Fallback
    }
    
    // Skip "CURSOR FOR" and whitespace
    size_t selectPos = cursorForPos + 10; // length of "CURSOR FOR"
    while (selectPos < declareSql.size() && std::isspace(declareSql[selectPos])) {
        ++selectPos;
    }
    
    std::string result = declareSql.substr(selectPos);
    return result;
}
void executeSql(const trx::ast::SqlStatement &sqlStmt, ExecutionContext &context) {
    switch (sqlStmt.kind) {
        case trx::ast::SqlStatementKind::ExecImmediate: {
            std::string sql = sqlStmt.sql;
            // Extract host variables from AST
            std::vector<JsonValue> hostVars = resolveHostVariablesFromAst(sqlStmt.hostVariables, context);

            // Special handling for UPDATE WHERE CURRENT OF: rebuild SQL and params for only resolved variables
            std::string upperSql = sql;
            std::transform(upperSql.begin(), upperSql.end(), upperSql.begin(), ::toupper);
            if (upperSql.find("UPDATE") == 0 && upperSql.find("WHERE CURRENT OF") != std::string::npos) {
                // Parse the UPDATE statement
                size_t setPos = upperSql.find(" SET ");
                size_t wherePos = upperSql.find(" WHERE CURRENT OF ");
                if (setPos != std::string::npos && wherePos != std::string::npos) {
                    std::string setClause = sql.substr(setPos + 5, wherePos - (setPos + 5));
                    std::string whereClause = sql.substr(wherePos);
                    
                    // Parse assignments in SET clause
                    std::vector<std::string> assignments;
                    size_t pos = 0;
                    while (pos < setClause.size()) {
                        size_t commaPos = setClause.find(',', pos);
                        if (commaPos == std::string::npos) {
                            assignments.push_back(setClause.substr(pos));
                            break;
                        }
                        assignments.push_back(setClause.substr(pos, commaPos - pos));
                        pos = commaPos + 1;
                        while (pos < setClause.size() && std::isspace(setClause[pos])) ++pos;
                    }
                    
                    // Rebuild SET clause with only resolved variables
                    std::string newSetClause;
                    std::vector<JsonValue> newHostVars;
                    size_t varIndex = 0;
                    for (size_t i = 0; i < assignments.size(); ++i) {
                        if (varIndex < hostVars.size()) {
                            if (newSetClause.empty()) {
                                newSetClause = assignments[i];
                            } else {
                                newSetClause += ", " + assignments[i];
                            }
                            newHostVars.push_back(hostVars[varIndex]);
                        }
                        ++varIndex;
                    }
                    
                    if (!newSetClause.empty()) {
                        sql = upperSql.substr(0, setPos + 5) + newSetClause + whereClause;
                    }
                    hostVars = newHostVars;
                }
            }

            // Execute using database driver
            auto params = convertHostVarsToParams(hostVars);
            try {
                context.interpreter.db().executeSql(sql, params);
                context.interpreter.setSqlCode(0.0); // Success
                debugPrint("SQL EXEC: " + sqlStmt.sql);
            } catch (const std::exception& e) {
                context.interpreter.setSqlCode(-1.0); // Error
                // std::cerr << "SQL execution failed: " << e.what() << std::endl;
            }
            break;
        }
        
        case trx::ast::SqlStatementKind::DeclareCursor: {
            std::string selectSql = extractSelectFromDeclare(sqlStmt.sql);
            std::vector<JsonValue> hostVars = resolveHostVariablesFromAst(sqlStmt.hostVariables, context);

            try {
                context.interpreter.db().openCursor(sqlStmt.identifier, selectSql, convertHostVarsToParams(hostVars));
                context.interpreter.setSqlCode(0.0); // Success
                debugPrint("SQL DECLARE CURSOR: " + sqlStmt.identifier + " AS " + selectSql);
            } catch (const std::exception& e) {
                context.interpreter.setSqlCode(-1.0); // Error
                // std::cerr << "SQL cursor declare failed: " << e.what() << std::endl;
            }
            break;
        }

        case trx::ast::SqlStatementKind::OpenCursor: {
            if (!sqlStmt.openParameters.empty()) {
                // This is OPEN cursor USING parameters
                std::vector<JsonValue> openParams = resolveHostVariablesFromAst(sqlStmt.openParameters, context);
                try {
                    context.interpreter.db().openDeclaredCursorWithParams(sqlStmt.identifier, convertHostVarsToParams(openParams));
                    context.interpreter.setSqlCode(0.0); // Success
                    debugPrint("SQL OPEN CURSOR WITH PARAMS: " + sqlStmt.identifier);
                } catch (const std::exception& e) {
                    context.interpreter.setSqlCode(-1.0); // Error
                    debugPrint("SQL OPEN CURSOR WITH PARAMS failed: " + std::string(e.what()));
                }
            } else {
                // Regular OPEN cursor (already opened in declare)
                context.interpreter.setSqlCode(0.0); // Success (no actual operation)
                debugPrint("SQL OPEN CURSOR: " + sqlStmt.identifier);
            }
            break;
        }

        case trx::ast::SqlStatementKind::FetchCursor: {
            try {
                debugPrint("FETCH: calling cursorNext for " + sqlStmt.identifier);
                if (context.interpreter.db().cursorNext(sqlStmt.identifier)) {
                    // std::cout << "FETCH: cursorNext returned true, calling cursorGetRow" << std::endl;
                    auto row = context.interpreter.db().cursorGetRow(sqlStmt.identifier);
                    debugPrint("FETCH: cursorGetRow returned row with " + std::to_string(row.size()) + " columns");
                    // Bind results to host variables
                    size_t i = 0;
                    for (const auto& var : sqlStmt.hostVariables) {
                        if (i < row.size()) {
                            // std::cout << "FETCH: binding column " << i << " value " << row[i] << " to variable" << std::endl;
                            resolveVariableTarget(var, context) = row[i];
                            // std::cout << "FETCH: binding complete for column " << i << std::endl;
                        }
                        ++i;
                    }
                    context.interpreter.setSqlCode(0.0); // Success - row found
                    debugPrint("SQL FETCH CURSOR: " + sqlStmt.identifier + " - row found");
                } else {
                    debugPrint("FETCH: cursorNext returned false, no more rows");
                    // std::cout << "FETCH: cursorNext returned false, no more rows" << std::endl;
                    // No more rows - set host variables to null
                    for (const auto& var : sqlStmt.hostVariables) {
                        resolveVariableTarget(var, context) = JsonValue(nullptr);
                    }
                    context.interpreter.setSqlCode(100.0); // No data found
                    debugPrint("SQL FETCH CURSOR: " + sqlStmt.identifier + " - no more rows");
                }
            } catch (const std::runtime_error& e) {
                context.interpreter.setSqlCode(-1.0); // Error
                // std::cerr << "Cursor operation failed: " << e.what() << std::endl;
            }
            break;
        }

        case trx::ast::SqlStatementKind::CloseCursor: {
            try {
                context.interpreter.db().closeCursor(sqlStmt.identifier);
                context.interpreter.setSqlCode(0.0); // Success
                debugPrint("SQL CLOSE CURSOR: " + sqlStmt.identifier);
            } catch (const std::exception& e) {
                context.interpreter.setSqlCode(-1.0); // Error
                // std::cerr << "SQL cursor close failed: " << e.what() << std::endl;
            }
            break;
        }

        case trx::ast::SqlStatementKind::SelectForUpdate: {
            std::string sql = sqlStmt.sql;
            std::vector<JsonValue> hostVars = resolveHostVariablesFromAst(sqlStmt.hostVariables, context);

            // Execute the SELECT statement (FOR UPDATE is mainly a hint for locking in other databases)
            auto params = convertHostVarsToParams(hostVars);
            try {
                auto results = context.interpreter.db().querySql(sql, params);
                if (!results.empty()) {
                    // Bind first row results to host variables if specified
                    const auto& row = results[0];
                    size_t i = 0;
                    for (const auto& var : sqlStmt.hostVariables) {
                        if (i < row.size()) {
                            resolveVariableTarget(var, context) = row[i];
                        }
                        ++i;
                    }
                    context.interpreter.setSqlCode(0.0); // Success
                    // std::cout << "SQL SELECT FOR UPDATE: " << sqlStmt.sql << " - row locked and fetched" << std::endl;
                } else {
                    // No rows found - set host variables to null
                    for (const auto& var : sqlStmt.hostVariables) {
                        resolveVariableTarget(var, context) = JsonValue(nullptr);
                    }
                    context.interpreter.setSqlCode(100.0); // No data found
                    // std::cout << "SQL SELECT FOR UPDATE: " << sqlStmt.sql << " - no rows found" << std::endl;
                }
            } catch (const std::exception& e) {
                context.interpreter.setSqlCode(-1.0); // Error
                // std::cerr << "SQL select for update failed: " << e.what() << std::endl;
            }
            break;
        }

        case trx::ast::SqlStatementKind::SelectInto: {
            std::string sql = sqlStmt.sql;

            // For SELECT INTO, parse the SQL to separate INTO variables from WHERE parameters
            std::string upperSql = sql;
            std::transform(upperSql.begin(), upperSql.end(), upperSql.begin(), ::toupper);
            std::string::size_type intoPos = upperSql.find(" INTO ");
            size_t intoCount = 0;
            if (intoPos != std::string::npos) {
                std::string::size_type fromPos = upperSql.find(" FROM ", intoPos);
                if (fromPos != std::string::npos) {
                    std::string intoPart = upperSql.substr(intoPos + 6, fromPos - (intoPos + 6));
                    size_t pos = 0;
                    while ((pos = intoPart.find('?', pos)) != std::string::npos) {
                        ++intoCount;
                        ++pos;
                    }
                    // Remove INTO clause from SQL
                    sql = upperSql.substr(0, intoPos) + upperSql.substr(fromPos);
                }
            }

            // Resolve only the input host variables (after INTO)
            std::vector<trx::ast::VariableExpression> inputVars(sqlStmt.hostVariables.begin() + intoCount, sqlStmt.hostVariables.end());
            std::vector<JsonValue> inputHostVars = resolveHostVariablesFromAst(inputVars, context);

            // Parameters are the input host variables
            auto params = convertHostVarsToParams(inputHostVars);

            // Execute the SELECT statement and fetch single row into host variables
            try {
                auto results = context.interpreter.db().querySql(sql, params);
                if (!results.empty()) {
                    // Bind first row results to INTO host variables
                    const auto& row = results[0];
                    size_t i = 0;
                    for (size_t j = 0; j < intoCount && i < row.size(); ++j) {
                        resolveVariableTarget(sqlStmt.hostVariables[j], context) = row[i++];
                    }
                    context.interpreter.setSqlCode(0.0); // Success
                    debugPrint("SQL SELECT INTO: " + sqlStmt.sql);
                } else {
                    // No rows found - set INTO host variables to null
                    for (size_t j = 0; j < intoCount; ++j) {
                        resolveVariableTarget(sqlStmt.hostVariables[j], context) = JsonValue(nullptr);
                    }
                    context.interpreter.setSqlCode(100.0); // No data found
                    debugPrint("SQL SELECT INTO: " + sqlStmt.sql + " - no rows found");
                }
            } catch (const std::exception& e) {
                context.interpreter.setSqlCode(-1.0); // Error
                // std::cerr << "SQL select into failed: " << e.what() << std::endl;
            }
            break;
        }
    }
}

void executeStatement(const trx::ast::Statement &statement, ExecutionContext &context) {
    std::visit(
        Overloaded{
            [&](const trx::ast::AssignmentStatement &assignment) { executeAssignment(assignment, context); },
            [&](const trx::ast::VariableDeclarationStatement &varDecl) { executeVariableDeclaration(varDecl, context); },
            [&](const trx::ast::ThrowStatement &throwStmt) { executeThrow(throwStmt, context); },
            [&](const trx::ast::TryCatchStatement &tryCatchStmt) { executeTryCatch(tryCatchStmt, context); },
            [&](const trx::ast::IfStatement &ifStmt) { executeIf(ifStmt, context); },
            [&](const trx::ast::WhileStatement &whileStmt) { executeWhile(whileStmt, context); },
            [&](const trx::ast::ForStatement &forStmt) { executeFor(forStmt, context); },
            [&](const trx::ast::BlockStatement &block) { executeBlock(block, context); },
            [&](const trx::ast::SwitchStatement &switchStmt) { executeSwitch(switchStmt, context); },
            [&](const trx::ast::SortStatement &sortStmt) { executeSort(sortStmt, context); },
            [&](const trx::ast::TraceStatement &trace) { executeTrace(trace, context); },
            [&](const trx::ast::ExpressionStatement &exprStmt) { executeExpression(exprStmt, context); },
            [&](const trx::ast::SystemStatement &systemStmt) { executeSystem(systemStmt, context); },
            [&](const trx::ast::BatchStatement &batchStmt) { executeBatch(batchStmt, context); },
            [&](const trx::ast::ReturnStatement &returnStmt) { executeReturn(returnStmt, context); },
            [&](const trx::ast::ValidateStatement &validateStmt) { executeValidate(validateStmt, context); },
            [&](const trx::ast::SqlStatement &sqlStmt) { executeSql(sqlStmt, context); },
            [&](const auto &) {
                throw std::runtime_error("Statement type not supported by interpreter yet");
            }
        },
        statement.node);
}

void executeStatements(const trx::ast::StatementList &statements, ExecutionContext &context) {
    for (const auto &statement : statements) {
        executeStatement(statement, context);
    }
}

} // namespace

Interpreter::Interpreter(const trx::ast::Module &module, std::unique_ptr<DatabaseDriver> dbDriver)
    : module_{module}, dbDriver_{std::move(dbDriver)} {
    for (const auto &decl : module.declarations) {
        if (std::holds_alternative<ast::ProcedureDecl>(decl)) {
            const auto &proc = std::get<ast::ProcedureDecl>(decl);
            procedures_[proc.name.baseName] = &proc;
        } else if (std::holds_alternative<ast::RecordDecl>(decl)) {
            const auto &record = std::get<ast::RecordDecl>(decl);
            records_[record.name.name] = &record;
        } else if (std::holds_alternative<ast::TableDecl>(decl)) {
            const auto &table = std::get<ast::TableDecl>(decl);
            
            // Convert AST TableColumn to runtime TableColumn
            std::vector<TableColumn> columns;
            for (const auto& astCol : table.columns) {
                TableColumn col;
                col.name = astCol.name.name;
                col.typeName = astCol.typeName;
                col.isPrimaryKey = astCol.isPrimaryKey;
                col.isNullable = astCol.isNullable;
                col.length = astCol.length;
                col.scale = astCol.scale;
                col.defaultValue = astCol.defaultValue;
                columns.push_back(col);
            }
            
            // Create or migrate the table
            dbDriver_->createOrMigrateTable(table.name.name, columns);
        }
    }

    // Initialize database driver (use environment variables or SQLite by default if none provided)
    if (!dbDriver_) {
        const char* dbType = std::getenv("DATABASE_TYPE");
        if (dbType && std::string(dbType) == "ODBC") {
            DatabaseConfig config;
            config.type = DatabaseType::ODBC;
            const char* connStr = std::getenv("DATABASE_CONNECTION_STRING");
            if (connStr) {
                config.connectionString = connStr;
            } else {
                throw std::runtime_error("DATABASE_CONNECTION_STRING environment variable must be set for ODBC");
            }
            dbDriver_ = createDatabaseDriver(config);
        } else {
            DatabaseConfig config;
            config.type = DatabaseType::SQLITE;
            config.databasePath = ":memory:"; // In-memory database
            dbDriver_ = createDatabaseDriver(config);
        }
    }

    dbDriver_->initialize();

    // Resolve TYPE FROM TABLE declarations
    for (auto &decl : const_cast<trx::ast::Module&>(module_).declarations) {
        if (std::holds_alternative<ast::RecordDecl>(decl)) {
            auto &record = std::get<ast::RecordDecl>(decl);
            if (record.tableName && record.fields.empty()) {
                // Resolve from database schema
                auto columns = dbDriver_->getTableSchema(*record.tableName);
                for (const auto& col : columns) {
                    ast::RecordField field;
                    field.name = {.name = col.name, .location = record.name.location};
                    field.typeName = col.typeName;
                    field.length = col.length.value_or(0);
                    field.scale = col.scale;
                    field.dimension = 1; // Default
                    field.jsonName = col.name; // Use column name as JSON name
                    field.jsonOmitEmpty = false;
                    field.hasExplicitJsonName = false;
                    record.fields.push_back(field);
                }
            }
        }
    }

    // Execute global statements (variable declarations and function calls)
    ExecutionContext globalContext{*this, {}, false, std::nullopt, true, false, std::nullopt};
    for (const auto &decl : module.declarations) {
        if (std::holds_alternative<ast::VariableDeclarationStatement>(decl)) {
            executeVariableDeclaration(std::get<ast::VariableDeclarationStatement>(decl), globalContext);
        } else if (std::holds_alternative<ast::ExpressionStatement>(decl)) {
            executeExpression(std::get<ast::ExpressionStatement>(decl), globalContext);
        } else if (std::holds_alternative<ast::ProcedureDecl>(decl)) {
            procedures_[std::get<ast::ProcedureDecl>(decl).name.baseName] = &std::get<ast::ProcedureDecl>(decl);
        } else if (std::holds_alternative<ast::TableDecl>(decl)) {
            // Handle table declarations if needed
        } else if (std::holds_alternative<ast::RecordDecl>(decl)) {
            // Handle record declarations if needed
        } else if (std::holds_alternative<ast::ConstantDecl>(decl)) {
            // Handle constant declarations if needed
        } else if (std::holds_alternative<ast::IncludeDecl>(decl)) {
            // Handle include declarations if needed
        } else if (std::holds_alternative<ast::ExternalProcedureDecl>(decl)) {
            // Handle external procedure declarations if needed
        }
    }
}

Interpreter::~Interpreter() = default;

const trx::ast::ProcedureDecl* Interpreter::getProcedure(const std::string &name) const {
    auto it = procedures_.find(name);
    return it != procedures_.end() ? it->second : nullptr;
}

const trx::ast::RecordDecl* Interpreter::getRecord(const std::string &name) const {
    auto it = records_.find(name);
    return it != records_.end() ? it->second : nullptr;
}

std::optional<JsonValue> Interpreter::execute(const std::string &procedureName, const JsonValue &input) {
    return execute(procedureName, input, {});
}

std::optional<JsonValue> Interpreter::execute(const std::string &procedureName, const JsonValue &input, const std::map<std::string, std::string> &pathParams) {
    // std::cout << "DEBUG execute: Executing procedure '" << procedureName << "' with " << pathParams.size() << " path parameters" << std::endl;
    // for (const auto& [key, value] : pathParams) {
    //     std::cout << "DEBUG execute: Path param '" << key << "' = '" << value << "'" << std::endl;
    // }
    if (procedureName.empty()) {
        // Execute module statements
        ExecutionContext ctx{*this, {}, false, {}, false, true, "JSON"};
        try {
            for (const auto &stmt : module_.statements) {
                executeStatement(stmt, ctx);
                if (ctx.returned) break;
            }
        } catch (const ReturnException &e) {
            if (ctx.isFunction) {
                ctx.returnValue = e.value;
                ctx.returned = true;
            } else {
                throw TrxException("Procedures cannot return values. Use RETURN without a value.");
            }
        }
        if (ctx.returned) {
            return ctx.returnValue;
        }
        return {};
    }
    auto it = procedures_.find(procedureName);
    if (it == procedures_.end()) {
        throw TrxException("Procedure not found: " + procedureName);
    }
    const auto *procedure = it->second;

    // Check if we're already in a transaction
    bool alreadyInTransaction = dbDriver_->isInTransaction();
    std::string savepointName;
    
    if (alreadyInTransaction) {
        // Use a savepoint for nested transaction
        savepointName = "trx_savepoint_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        dbDriver_->executeSql("SAVEPOINT " + savepointName);
    } else {
        // Begin a new transaction
        dbDriver_->beginTransaction();
    }

    // Create execution context
    ExecutionContext context{*this, {}, false, std::nullopt, false, false, std::nullopt};
    context.isFunction = procedure->isFunction;

    // Automatically instantiate path parameters as local variables
    for (size_t i = 0; i < procedure->name.pathParameters.size(); ++i) {
        const auto &paramDecl = procedure->name.pathParameters[i];
        std::string paramName = paramDecl.name.name;
        std::string paramType = paramDecl.type.name;
        // std::cout << "DEBUG execute: Processing path parameter '" << paramName << "' of type '" << paramType << "'" << std::endl;
        
        // Find the corresponding path parameter value
        auto pathParamIt = pathParams.find(paramName);
        if (pathParamIt != pathParams.end()) {
            std::string valueStr = pathParamIt->second;
            // std::cout << "DEBUG execute: Found path param value '" << valueStr << "' for '" << paramName << "'" << std::endl;
            JsonValue paramValue;
            
            // Convert string value to appropriate type based on parameter type
            if (paramType == "INTEGER") {
                try {
                    paramValue = JsonValue(std::stoi(valueStr));
                } catch (...) {
                    paramValue = JsonValue(0); // Default to 0 on conversion error
                }
            } else if (paramType == "DECIMAL" || paramType == "DOUBLE") {
                try {
                    paramValue = JsonValue(std::stod(valueStr));
                    // std::cout << "DEBUG execute: Converted to double: " << std::get<double>(paramValue.data) << std::endl;
                } catch (...) {
                    paramValue = JsonValue(0.0); // Default to 0.0 on conversion error
                    //std::cout << "DEBUG execute: Conversion to double failed, using default 0.0" << std::endl;
                }
            } else if (paramType == "BOOLEAN") {
                paramValue = JsonValue(valueStr == "true" || valueStr == "TRUE" || valueStr == "1");
                // std::cout << "DEBUG execute: Converted to boolean: " << std::get<bool>(paramValue.data) << std::endl;
            } else {
                // Default to string for unknown types
                paramValue = JsonValue(valueStr);
                // std::cout << "DEBUG execute: Using as string: '" << std::get<std::string>(paramValue.data) << "'" << std::endl;
            }
            
            context.variables[paramName] = paramValue;
        } else {
            // std::cout << "DEBUG execute: Path parameter '" << paramName << "' not found in pathParams" << std::endl;
        }
    }
    
    // Set the input parameter as a local variable
    if (procedure->input) {
        context.variables[procedure->input->name.name] = input;
    }
    
    // Bind path parameters to explicit function parameters
    if (procedure->input && !pathParams.empty()) {
        // For functions with path parameters, bind path params to explicit params
        JsonValue paramInput = JsonValue::object();
        auto &obj = std::get<JsonValue::Object>(paramInput.data);
        
        // Copy path parameters
        for (const auto &[key, value] : pathParams) {
            obj[key] = JsonValue(value);
        }
        
        // Copy any body parameters
        if (std::holds_alternative<JsonValue::Object>(input.data)) {
            const auto &inputObj = std::get<JsonValue::Object>(input.data);
            for (const auto &[key, value] : inputObj) {
                obj[key] = value;
            }
        }
        
        context.variables[procedure->input->name.name] = paramInput;
    } else if (procedure->input) {
        // For functions without path parameters, use the input as-is
        context.variables[procedure->input->name.name] = input;
    }

    try {
        executeStatements(procedure->body, context);
        // For functions, execution without return is an error
        if (procedure->output) {
            throw std::runtime_error("Function must return a value");
        }
        // Commit on successful completion
        if (alreadyInTransaction) {
            dbDriver_->executeSql("RELEASE SAVEPOINT " + savepointName);
        } else {
            dbDriver_->commitTransaction();
        }
    } catch (const ReturnException &ret) {
        // For functions, return the value
        if (procedure->output) {
            // The return validation is already done in executeReturn
            // Commit on successful completion (even with return)
            if (alreadyInTransaction) {
                dbDriver_->executeSql("RELEASE SAVEPOINT " + savepointName);
            } else {
                dbDriver_->commitTransaction();
            }
            return ret.value;
        } else {
            // For procedures, RETURN just ends execution (no value returned)
            if (alreadyInTransaction) {
                dbDriver_->executeSql("RELEASE SAVEPOINT " + savepointName);
            } else {
                dbDriver_->commitTransaction();
            }
            return std::nullopt;  // Procedures don't return values
        }
    } catch (...) {
        // Rollback on any exception
        if (alreadyInTransaction) {
            dbDriver_->executeSql("ROLLBACK TO SAVEPOINT " + savepointName);
        } else {
            try {
                dbDriver_->rollbackTransaction();
            } catch (...) {
                // Ignore rollback errors
            }
        }
        throw;
    }

    if (procedure->output || context.variables.count("output")) {
        const auto outIt = context.variables.find("output");
        if (outIt == context.variables.end()) {
            throw std::runtime_error("Function execution did not produce output");
        }
        return outIt->second;
    } else {
        return std::nullopt;
    }
}

std::optional<JsonValue> Interpreter::execute(const ast::ProcedureDecl *procedure, const JsonValue &input, const std::map<std::string, std::string> &pathParams) {
    // Check if we're already in a transaction
    bool alreadyInTransaction = dbDriver_->isInTransaction();
    std::string savepointName;
    
    if (alreadyInTransaction) {
        // Use a savepoint for nested transaction
        savepointName = "trx_savepoint_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        dbDriver_->executeSql("SAVEPOINT " + savepointName);
    } else {
        // Begin a new transaction
        dbDriver_->beginTransaction();
    }

    try {
        // Create execution context
        ExecutionContext context{*this, {}, false, std::nullopt, false, false, std::nullopt};
        context.isFunction = procedure->isFunction;
        
        // Automatically instantiate path parameters as local variables
        for (size_t i = 0; i < procedure->name.pathParameters.size(); ++i) {
            const auto &paramDecl = procedure->name.pathParameters[i];
            std::string paramName = paramDecl.name.name;
            std::string paramType = paramDecl.type.name;
            
            // Find the corresponding path parameter value
            auto pathParamIt = pathParams.find(paramName);
            if (pathParamIt != pathParams.end()) {
                std::string valueStr = pathParamIt->second;
                JsonValue paramValue;
                
                // Convert string value to appropriate type based on parameter type
                if (paramType == "INTEGER") {
                    try {
                        paramValue = JsonValue(std::stoi(valueStr));
                    } catch (...) {
                        paramValue = JsonValue(0); // Default to 0 on conversion error
                    }
                } else if (paramType == "DECIMAL" || paramType == "DOUBLE") {
                    try {
                        paramValue = JsonValue(std::stod(valueStr));
                    } catch (...) {
                        paramValue = JsonValue(0.0); // Default to 0.0 on conversion error
                    }
                } else if (paramType == "BOOLEAN") {
                    paramValue = JsonValue(valueStr == "true" || valueStr == "1");
                } else {
                    // Default to string for other types
                    paramValue = JsonValue(valueStr);
                }
                
                context.variables[paramName] = paramValue;
            }
        }
        
        // Bind input parameters
        if (procedure->input) {
            if (!std::holds_alternative<JsonValue::Object>(input.data)) {
                throw std::runtime_error("Input must be a JSON object");
            }
            context.variables[procedure->input->name.name] = input;
        }
        
        // Execute the procedure body
        for (const auto &stmt : procedure->body) {
            executeStatement(stmt, context);
            if (context.returned) {
                break;
            }
        }
        
        if (procedure->output) {
            throw std::runtime_error("Function must return a value");
        }
        // Commit on successful completion
        if (alreadyInTransaction) {
            dbDriver_->executeSql("RELEASE SAVEPOINT " + savepointName);
        } else {
            dbDriver_->commitTransaction();
        }
    } catch (const ReturnException &ret) {
        // For functions, return the value
        if (procedure->output) {
            // The return validation is already done in executeReturn
            // Commit on successful completion (even with return)
            if (alreadyInTransaction) {
                dbDriver_->executeSql("RELEASE SAVEPOINT " + savepointName);
            } else {
                dbDriver_->commitTransaction();
            }
            return ret.value;
        } else {
            // For procedures, RETURN just ends execution (no value returned)
            if (alreadyInTransaction) {
                dbDriver_->executeSql("RELEASE SAVEPOINT " + savepointName);
            } else {
                dbDriver_->commitTransaction();
            }
            return std::nullopt;  // Procedures don't return values
        }
    } catch (...) {
        // Rollback on any exception
        if (alreadyInTransaction) {
            dbDriver_->executeSql("ROLLBACK TO SAVEPOINT " + savepointName);
        } else {
            try {
                dbDriver_->rollbackTransaction();
            } catch (...) {
                // Ignore rollback errors
            }
        }
        throw;
    }

    if (procedure->output) {
        // This should not happen - functions should return via ReturnException
        throw std::runtime_error("Function did not return a value");
    }
    return std::nullopt;
}

} // namespace trx::runtime

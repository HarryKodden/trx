#include "Server.h"

#include "trx/ast/Nodes.h"
#include "trx/ast/Statements.h"
#include "trx/parsing/ParserDriver.h"
#include "trx/runtime/Interpreter.h"
#include "trx/runtime/ThreadPool.h"
#include "trx/runtime/TrxException.h"
#include "trx/diagnostics/DiagnosticEngine.h"

#include <algorithm>
#include <atomic>
#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <set>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace trx::cli {
namespace {

// Helper function to match path against template and extract parameters
std::optional<std::pair<const trx::ast::ProcedureDecl *, std::map<std::string, std::string>>> matchPathTemplate(
    const std::string &requestPath, 
    const std::string &requestMethod,
    const std::map<std::string, const trx::ast::ProcedureDecl *> &procedureLookup) {
    
    // Strip leading slash and /api/ prefix from request path for matching
    std::string path = requestPath;
    if (!path.empty() && path[0] == '/') {
        path = path.substr(1);
    }
    if (path.starts_with("api/")) {
        path = path.substr(4);
    }
    
    for (const auto &[key, proc] : procedureLookup) {
        const std::string &templatePath = proc->name.pathTemplate;
        
        // Check HTTP method first
        std::string defaultMethod = proc->input ? "POST" : "GET";
        std::string expectedMethod = proc->httpMethod.value_or(defaultMethod);
        if (requestMethod != expectedMethod) {
            continue; // Method doesn't match, skip this procedure
        }
        
        // Simple exact match for now (backward compatibility)
        if (path == templatePath) {
            return std::make_pair(proc, std::map<std::string, std::string>{});
        }
        
        // Check if template has parameters
        if (!proc->name.pathParameters.empty()) {
            // Build regex pattern from template
            std::string pattern = templatePath;
            // Escape special regex characters except {
            std::string escaped;
            for (char c : pattern) {
                if (c == '{' || c == '}') {
                    escaped += c;
                } else if (std::string(".*+?^$()[]{}|\\").find(c) != std::string::npos) {
                    escaped += '\\';
                    escaped += c;
                } else {
                    escaped += c;
                }
            }
            // Replace {param} with regex capture group
            size_t pos = 0;
            while ((pos = escaped.find("{", pos)) != std::string::npos) {
                size_t endPos = escaped.find("}", pos);
                if (endPos != std::string::npos) {
                    std::string paramName = escaped.substr(pos + 1, endPos - pos - 1);
                    escaped.replace(pos, endPos - pos + 1, "([^/]+)");
                    pos += std::string("([^/]+)").length();
                } else {
                    break;
                }
            }
            
            std::regex pathRegex("^" + escaped + "$");
            std::smatch matches;
            if (std::regex_match(path, matches, pathRegex)) {
                std::map<std::string, std::string> params;
                // matches[0] is the full match, captures start from matches[1]
                for (size_t i = 1; i < matches.size() && i - 1 < proc->name.pathParameters.size(); ++i) {
                    params[proc->name.pathParameters[i - 1].name.name] = matches[i].str();
                }
                return std::make_pair(proc, params);
            }
        }
    }
    
    return std::nullopt;
}

struct Metrics {
    std::atomic<size_t> totalRequests{0};
    std::atomic<size_t> activeRequests{0};
    std::atomic<size_t> errorRequests{0};
    std::mutex durationMutex;
    std::vector<double> requestDurations; // in milliseconds
    double averageDuration = 0.0;
};

Metrics g_metrics;

std::atomic<bool> g_stopServer{false};

void handleSignal(int signum) {
    if (signum == SIGINT) {
        g_stopServer.store(true);
    }
}

bool hasTrxExtension(const std::filesystem::path &path) {
    const std::string extension = path.extension().string();
    std::string lowered = extension;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered == ".trx";
}

std::vector<std::filesystem::path> collectSourceFiles(const std::filesystem::path &root, std::error_code &error) {
    std::vector<std::filesystem::path> files;
    error.clear();

    if (!std::filesystem::exists(root, error)) {
        if (!error) {
            error = std::make_error_code(std::errc::no_such_file_or_directory);
        }
        return files;
    }

    if (std::filesystem::is_regular_file(root, error)) {
        if (!error) {
            files.push_back(root);
        }
        return files;
    }

    if (error) {
        return files;
    }

    if (!std::filesystem::is_directory(root, error)) {
        if (!error) {
            error = std::make_error_code(std::errc::invalid_argument);
        }
        return files;
    }

    std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error);
    std::filesystem::recursive_directory_iterator end;
    while (!error && it != end) {
        const auto &entry = *it;
        if (entry.is_regular_file(error) && !error && hasTrxExtension(entry.path())) {
            files.push_back(entry.path());
        }
        if (error) {
            break;
        }
        it.increment(error);
    }

    if (!error) {
        std::sort(files.begin(), files.end());
    }

    return files;
}

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status{200};
    std::string contentType{"text/plain"};
    std::string body;
    std::vector<std::pair<std::string, std::string>> extraHeaders;
};

std::string statusMessage(int status) {
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default: return "Unknown";
    }
}

void sendHttpResponse(int clientFd, const HttpResponse &response) {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << response.status << ' ' << statusMessage(response.status) << "\r\n";
    stream << "Content-Type: " << response.contentType << "\r\n";
    stream << "Access-Control-Allow-Origin: *\r\n";
    for (const auto &header : response.extraHeaders) {
        stream << header.first << ": " << header.second << "\r\n";
    }

    const std::string &body = response.body;
    stream << "Content-Length: " << body.size() << "\r\n";
    stream << "Connection: close\r\n\r\n";
    stream << body;

    const std::string data = stream.str();
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t written = ::send(clientFd, data.data() + sent, data.size() - sent, 0);
        if (written <= 0) {
            break;
        }
        sent += static_cast<std::size_t>(written);
    }
}

bool readHttpRequest(int clientFd, HttpRequest &request) {
    std::string buffer;
    buffer.reserve(4096);

    std::string headers;
    std::string body;

    std::optional<std::size_t> contentLength;
    while (true) {
        char chunk[2048];
        const ssize_t received = ::recv(clientFd, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return false;
        }
        buffer.append(chunk, static_cast<std::size_t>(received));

        const auto headerEnd = buffer.find("\r\n\r\n");
        if (!contentLength.has_value() && headerEnd != std::string::npos) {
            headers = buffer.substr(0, headerEnd);
            body = buffer.substr(headerEnd + 4);

            std::istringstream headerStream(headers);
            std::string requestLine;
            if (!std::getline(headerStream, requestLine)) {
                return false;
            }
            if (!requestLine.empty() && requestLine.back() == '\r') {
                requestLine.pop_back();
            }
            std::istringstream lineStream(requestLine);
            std::string version;
            if (!(lineStream >> request.method >> request.path >> version)) {
                return false;
            }

            std::string headerLine;
            while (std::getline(headerStream, headerLine)) {
                if (!headerLine.empty() && headerLine.back() == '\r') {
                    headerLine.pop_back();
                }
                if (headerLine.empty()) {
                    continue;
                }
                const auto colonPos = headerLine.find(':');
                if (colonPos == std::string::npos) {
                    continue;
                }
                std::string key = headerLine.substr(0, colonPos);
                std::string value = headerLine.substr(colonPos + 1);
                while (!value.empty() && value.front() == ' ') {
                    value.erase(value.begin());
                }
                // normalize header names to lowercase for lookups
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                request.headers.emplace(std::move(key), std::move(value));
            }

            auto contentLengthIt = request.headers.find("content-length");
            if (contentLengthIt != request.headers.end()) {
                try {
                    contentLength = static_cast<std::size_t>(std::stoul(contentLengthIt->second));
                } catch (const std::exception &) {
                    return false;
                }
            } else {
                contentLength = body.size();
            }
        }

        if (contentLength.has_value() && body.size() >= contentLength.value()) {
            break;
        }

        if (contentLength.has_value() && body.size() < contentLength.value()) {
            const std::size_t missing = contentLength.value() - body.size();
            if (missing > 0) {
                continue;
            }
        }
    }

    if (!contentLength.has_value()) {
        contentLength = 0;
    }

    const auto headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return false;
    }
    headers = buffer.substr(0, headerEnd);
    body = buffer.substr(headerEnd + 4);
    if (body.size() > contentLength.value()) {
        body.resize(contentLength.value());
    }

    // trim query string from path
    if (const auto question = request.path.find('?'); question != std::string::npos) {
        request.path.erase(question);
    }

    request.body = std::move(body);
    return true;
}

struct JsonParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    trx::runtime::JsonValue parse() {
        skipWhitespace();
        auto value = parseValue();
        skipWhitespace();
        if (position_ != text_.size()) {
            throw JsonParseError("Unexpected trailing data in JSON payload");
        }
        return value;
    }

private:
    std::string_view text_;
    std::size_t position_{0};

    bool eof() const {
        return position_ >= text_.size();
    }

    char peek() const {
        if (eof()) {
            return '\0';
        }
        return text_[position_];
    }

    char consume() {
        if (eof()) {
            throw JsonParseError("Unexpected end of JSON payload");
        }
        return text_[position_++];
    }

    void expect(char expected) {
        const char actual = consume();
        if (actual != expected) {
            throw JsonParseError("Unexpected character in JSON payload");
        }
    }

    void skipWhitespace() {
        while (!eof()) {
            const char c = peek();
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                ++position_;
            } else {
                break;
            }
        }
    }

    trx::runtime::JsonValue parseValue() {
        if (eof()) {
            throw JsonParseError("Unexpected end of JSON payload");
        }
        const char c = peek();
        if (c == '"') {
            return trx::runtime::JsonValue(parseString());
        }
        if (c == '{') {
            return parseObject();
        }
        if (startsWithRemaining("true")) {
            position_ += 4;
            return trx::runtime::JsonValue(true);
        }
        if (startsWithRemaining("false")) {
            position_ += 5;
            return trx::runtime::JsonValue(false);
        }
        if (startsWithRemaining("null")) {
            position_ += 4;
            return trx::runtime::JsonValue();
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parseNumber();
        }
        throw JsonParseError("Unsupported JSON token encountered");
    }

    bool startsWithRemaining(std::string_view literal) const {
        if (text_.size() - position_ < literal.size()) {
            return false;
        }
        return text_.substr(position_, literal.size()) == literal;
    }

    std::string parseString() {
        expect('"');
        std::string result;
        while (true) {
            if (eof()) {
                throw JsonParseError("Unterminated string literal");
            }
            char c = consume();
            if (c == '"') {
                break;
            }
            if (c == '\\') {
                if (eof()) {
                    throw JsonParseError("Invalid escape sequence");
                }
                c = consume();
                switch (c) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    unsigned int codepoint = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (eof()) {
                            throw JsonParseError("Invalid Unicode escape");
                        }
                        const char hex = consume();
                        codepoint <<= 4;
                        if (hex >= '0' && hex <= '9') {
                            codepoint |= static_cast<unsigned int>(hex - '0');
                        } else if (hex >= 'a' && hex <= 'f') {
                            codepoint |= static_cast<unsigned int>(hex - 'a' + 10);
                        } else if (hex >= 'A' && hex <= 'F') {
                            codepoint |= static_cast<unsigned int>(hex - 'A' + 10);
                        } else {
                            throw JsonParseError("Invalid Unicode escape");
                        }
                    }
                    appendCodepoint(result, codepoint);
                    break;
                }
                default:
                    throw JsonParseError("Invalid escape sequence");
                }
            } else {
                result.push_back(c);
            }
        }
        return result;
    }

    static void appendCodepoint(std::string &output, unsigned int codepoint) {
        if (codepoint <= 0x7F) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F))); 
        } else if (codepoint <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    trx::runtime::JsonValue parseNumber() {
        const std::size_t start = position_;
        if (peek() == '-') {
            ++position_;
        }
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
            ++position_;
        }
        if (!eof() && peek() == '.') {
            ++position_;
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                ++position_;
            }
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            ++position_;
            if (!eof() && (peek() == '+' || peek() == '-')) {
                ++position_;
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                ++position_;
            }
        }
        const std::string number{text_.substr(start, position_ - start)};
        try {
            const double numeric = std::stod(number);
            return trx::runtime::JsonValue(numeric);
        } catch (const std::exception &) {
            throw JsonParseError("Invalid numeric literal in JSON payload");
        }
    }

    trx::runtime::JsonValue parseObject() {
        expect('{');
        trx::runtime::JsonValue::Object object;
        skipWhitespace();
        if (peek() == '}') {
            consume();
            return trx::runtime::JsonValue(std::move(object));
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                throw JsonParseError("Object keys must be strings");
            }
            std::string key = parseString();
            // Convert key to uppercase for case-insensitive matching
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            skipWhitespace();
            expect(':');
            skipWhitespace();
            trx::runtime::JsonValue value = parseValue();
            object.emplace(std::move(key), std::move(value));
            skipWhitespace();
            const char delimiter = consume();
            if (delimiter == '}') {
                break;
            }
            if (delimiter != ',') {
                throw JsonParseError("Expected comma in object literal");
            }
            skipWhitespace();
        }
        return trx::runtime::JsonValue(std::move(object));
    }
};

std::string escapeJsonString(std::string_view input) {
    std::string result;
    result.reserve(input.size() + 8);
    for (const unsigned char c : input) {
        switch (c) {
        case '"': result.append("\\\""); break;
        case '\\': result.append("\\\\"); break;
        case '\b': result.append("\\b"); break;
        case '\f': result.append("\\f"); break;
        case '\n': result.append("\\n"); break;
        case '\r': result.append("\\r"); break;
        case '\t': result.append("\\t"); break;
        default:
            if (c < 0x20) {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04X", c);
                result.append(buffer);
            } else {
                result.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return result;
}

std::string serializeJsonValue(const trx::runtime::JsonValue &value);

std::string serializeObject(const trx::runtime::JsonValue::Object &object) {
    std::string result{"{"};
    bool first = true;
    for (const auto &entry : object) {
        if (!first) {
            result.push_back(',');
        }
        first = false;
        result.push_back('"');
        result.append(escapeJsonString(entry.first));
        result.append("\":");
        result.append(serializeJsonValue(entry.second));
    }
    result.push_back('}');
    return result;
}

std::string serializeJsonValue(const trx::runtime::JsonValue &value) {
    if (std::holds_alternative<std::nullptr_t>(value.data)) {
        return "null";
    }
    if (std::holds_alternative<bool>(value.data)) {
        return std::get<bool>(value.data) ? "true" : "false";
    }
    if (std::holds_alternative<double>(value.data)) {
        std::ostringstream stream;
        stream.setf(std::ios::fmtflags(0), std::ios::floatfield);
        stream.precision(15);
        stream << std::get<double>(value.data);
        return stream.str();
    }
    if (std::holds_alternative<std::string>(value.data)) {
        std::string result = "\"";
        result.append(escapeJsonString(std::get<std::string>(value.data)));
        result.push_back('"');
        return result;
    }
    if (std::holds_alternative<trx::runtime::JsonValue::Array>(value.data)) {
        const auto& array = std::get<trx::runtime::JsonValue::Array>(value.data);
        std::string result{"["};
        bool first = true;
        for (const auto& item : array) {
            if (!first) {
                result.push_back(',');
            }
            first = false;
            result.append(serializeJsonValue(item));
        }
        result.push_back(']');
        return result;
    }
    if (std::holds_alternative<trx::runtime::JsonValue::Object>(value.data)) {
        return serializeObject(std::get<trx::runtime::JsonValue::Object>(value.data));
    }
    throw std::runtime_error("Unsupported JsonValue variant");
}

// Helper function to map TRX types to OpenAPI types
std::string mapTrxTypeToOpenApi(const std::string &trxType) {
    if (trxType == "CHAR" || trxType == "_CHAR" || trxType == "STRING" || trxType == "_STRING") {
        return "string";
    } else if (trxType == "INTEGER" || trxType == "_INTEGER" || trxType == "SMALLINT" || trxType == "_SMALLINT") {
        return "integer";
    } else if (trxType == "DECIMAL" || trxType == "_DECIMAL") {
        return "number";
    } else if (trxType == "BOOLEAN" || trxType == "_BOOLEAN") {
        return "boolean";
    } else if (trxType == "DATE" || trxType == "_DATE" || trxType == "TIME" || trxType == "_TIME") {
        return "string"; // Dates and times are typically represented as strings in OpenAPI
    } else if (trxType == "JSON") {
        return "object";
    } else if (trxType == "FILE" || trxType == "_FILE" || trxType == "BLOB" || trxType == "_BLOB") {
        return "string"; // Files and blobs are typically base64 encoded strings
    } else {
        // For custom types (records), return as-is (they'll be defined in components/schemas)
        return trxType;
    }
}

std::string buildSwaggerSpec(const std::map<std::string, const trx::ast::ProcedureDecl *> &procedureLookup, const std::vector<const trx::ast::RecordDecl *> &records, [[maybe_unused]] int port) {
    std::ostringstream spec;
    spec << R"(
{
  "openapi": "3.0.0",
  "info": {
    "title": "TRX Procedure Playground",
    "version": "0.1.0"
  },
  "paths": {)";

    // Group procedures by path template
    std::map<std::string, std::vector<const trx::ast::ProcedureDecl *>> proceduresByPath;
    for (const auto &[key, proc] : procedureLookup) {
        proceduresByPath[proc->name.pathTemplate].push_back(proc);
    }

    // Add paths for each unique path template
    bool firstPath = true;
    for (const auto &[pathTemplate, procedures] : proceduresByPath) {
        if (!firstPath) {
            spec << ",";
        }
        firstPath = false;
        spec << "\n    \"/api/" << escapeJsonString(pathTemplate) << "\": {";
        
        // Add operations for this path
        bool firstOperation = true;
        for (const auto *proc : procedures) {
            if (!firstOperation) {
                spec << ",";
            }
            firstOperation = false;
            spec << "\n";
            
            // Use custom HTTP method if specified, otherwise default based on input
            std::string defaultMethod = "GET"; // proc->input ? "POST" : "GET";
            std::string httpMethod = proc->httpMethod.value_or(defaultMethod);
            std::string lowerMethod = httpMethod;
            std::transform(lowerMethod.begin(), lowerMethod.end(), lowerMethod.begin(), ::tolower);
            spec << "      \"" << escapeJsonString(lowerMethod) << "\": {\n";
            
            spec << "        \"summary\": \"Execute " << escapeJsonString(proc->name.baseName) << " procedure\",\n";
            
            // Add path parameters if any
            if (!proc->name.pathParameters.empty()) {
                spec << "        \"parameters\": [\n";
                bool firstParam = true;
                for (const auto &param : proc->name.pathParameters) {
                    if (!firstParam) {
                        spec << ",\n";
                    }
                    firstParam = false;
                    spec << "          {\n";
                    spec << "            \"name\": \"" << escapeJsonString(param.name.name) << "\",\n";
                    spec << "            \"in\": \"path\",\n";
                    spec << "            \"required\": true,\n";
                    spec << "            \"schema\": {\n";
                    spec << "              \"type\": \"" << mapTrxTypeToOpenApi(param.type.name) << "\"\n";
                    spec << "            }\n";
                    spec << "          }";
                }
                spec << "\n        ],\n";
            }
            
            // Only add requestBody for methods that support it
            if (httpMethod != "GET" && httpMethod != "HEAD" && httpMethod != "DELETE") {
                spec << "        \"requestBody\": {\n";
                spec << "          \"required\": true,\n";
                spec << "          \"content\": {\n";
                spec << "            \"application/json\": {\n";
                spec << "              \"schema\": ";
                if (proc->input) {
                    spec << "{\"$ref\": \"#/components/schemas/" << escapeJsonString(proc->input->type.name) << "\"}";
                } else {
                    spec << "{\"type\": \"object\"}";
                }
                spec << "\n            }\n";
                spec << "          }\n";
                spec << "        },\n";
            }
            
            spec << "        \"responses\": {\n";
            spec << "          \"200\": {\n";
            spec << "            \"description\": \"Execution succeeded\",\n";
            spec << "            \"content\": {\n";
            spec << "              \"application/json\": {\n";
            spec << "                \"schema\": ";
            if (proc->output) {
                    spec << "{\"$ref\": \"#/components/schemas/" << escapeJsonString(proc->output->type.name) << "\"}";
                } else {
                    spec << "{\"type\": \"object\"}";
                }
                spec << "\n              }\n";
                spec << "            }\n";
                spec << "          },\n";
                spec << "          \"400\": {\n";
            spec << "            \"description\": \"Invalid request\"\n";
            spec << "          },\n";
            spec << "          \"500\": {\n";
            spec << "            \"description\": \"Execution error\"\n";
            spec << "          }\n";
            spec << "        }\n";
            spec << "      }";
        }
        spec << "\n    }";
    }

    spec << R"(
  },
  "components": {
    "schemas": {)";

    // Add schemas for records
    bool firstSchema = true;
    for (const auto *record : records) {
        if (!firstSchema) {
            spec << ",";
        }
        firstSchema = false;
        spec << "\n      \"" << escapeJsonString(record->name.name) << "\": {\n";
        spec << "        \"type\": \"object\",\n";
        spec << "        \"properties\": {";
        bool firstField = true;
        for (const auto &field : record->fields) {
            if (!firstField) {
                spec << ",";
            }
            firstField = false;
            spec << "\n          \"" << escapeJsonString(field.jsonName) << "\": {\n";
            spec << "            \"type\": \"" << mapTrxTypeToOpenApi(field.typeName) << "\"";
            if (field.typeName == "CHAR" || field.typeName == "_CHAR" || field.typeName == "STRING" || field.typeName == "_STRING") {
                spec << ",\n            \"maxLength\": " << field.length;
            }
            spec << "\n          }";
        }
        spec << "\n        },\n";
        spec << "        \"required\": [";
        bool firstReq = true;
        for (const auto &field : record->fields) {
            if (!firstReq) {
                spec << ",";
            }
            firstReq = false;
            spec << "\n          \"" << escapeJsonString(field.jsonName) << "\"";
        }
        spec << "\n        ]\n";
        spec << "      }";
    }

    // Add schemas for builtin types
    const std::vector<std::string> builtinTypes = {
        "CHAR", "_CHAR", "STRING", "_STRING", "INTEGER", "_INTEGER", 
        "SMALLINT", "_SMALLINT", "DECIMAL", "_DECIMAL", "BOOLEAN", "_BOOLEAN",
        "DATE", "_DATE", "TIME", "_TIME", "JSON", "FILE", "_FILE", "BLOB", "_BLOB"
    };
    
    for (const auto &builtinType : builtinTypes) {
        spec << ",";
        spec << "\n      \"" << escapeJsonString(builtinType) << "\": {\n";
        spec << "        \"type\": \"" << mapTrxTypeToOpenApi(builtinType) << "\"";
        if (builtinType == "CHAR" || builtinType == "_CHAR" || builtinType == "STRING" || builtinType == "_STRING") {
            spec << ",\n        \"maxLength\": 255"; // Default max length
        }
        spec << "\n      }";
    }

    spec << R"(
    }
  }
}
)";

    return spec.str();
}

std::string buildProceduresPayload(const std::vector<std::string> &procedures, const std::string &defaultProcedure) {
    std::ostringstream out;
    out << "{\"procedures\":[";
    for (std::size_t i = 0; i < procedures.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << '\"' << escapeJsonString(procedures[i]) << '\"';
    }
    out << "],\"default\":\"" << escapeJsonString(defaultProcedure) << "\"}";
    return out.str();
}

std::string buildSwaggerIndexPage() {
    return R"(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"/><title>TRX Swagger Playground</title><link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/swagger-ui/4.15.5/swagger-ui.css"/></head><body><div id="swagger-ui"></div><script src="https://cdnjs.cloudflare.com/ajax/libs/swagger-ui/4.15.5/swagger-ui-bundle.js"></script><script>window.onload=function(){const ui = SwaggerUIBundle({url: '/swagger.json', dom_id: '#swagger-ui', deepLinking: true, presets: [SwaggerUIBundle.presets.apis]});};</script></body></html>)";
}

std::vector<const trx::ast::ProcedureDecl *> collectCallableProcedures(const trx::ast::Module &module) {
    std::vector<const trx::ast::ProcedureDecl *> procedures;
    for (const auto &decl : module.declarations) {
        if (const auto *proc = std::get_if<trx::ast::ProcedureDecl>(&decl)) {
            if (proc->isExported) {
                procedures.push_back(proc);
            }
        }
    }
    return procedures;
}

std::vector<const trx::ast::RecordDecl *> collectRecords(const trx::ast::Module &module) {
    std::map<std::string, const trx::ast::RecordDecl *> recordMap;
    for (const auto &decl : module.declarations) {
        if (const auto *record = std::get_if<trx::ast::RecordDecl>(&decl)) {
            // Use the last definition of each record name (later definitions override earlier ones)
            recordMap[record->name.name] = record;
        }
    }
    std::vector<const trx::ast::RecordDecl *> records;
    records.reserve(recordMap.size());
    for (const auto &[_, record] : recordMap) {
        records.push_back(record);
    }
    return records;
}

HttpResponse makeErrorResponse(int status, std::string_view message) {
    HttpResponse response;
    response.status = status;
    response.contentType = "application/json";
    response.body = std::string("{\"error\":\"") + escapeJsonString(message) + "\"}";
    response.extraHeaders.emplace_back("Access-Control-Allow-Origin", "*");
    response.extraHeaders.emplace_back("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response.extraHeaders.emplace_back("Access-Control-Allow-Headers", "Content-Type");
    return response;
}

// Helper function to get appropriate HTTP status code for successful responses
int getSuccessStatusCode(const std::string& method) {
    if (method == "POST") {
        return 201; // Created
    } else if (method == "PUT" || method == "PATCH") {
        return 200; // OK
    } else if (method == "DELETE") {
        return 204; // No Content
    } else {
        return 200; // OK (for GET, HEAD, etc.)
    }
}

HttpResponse handleExecuteProcedure(const HttpRequest &request,
                                   const trx::ast::ProcedureDecl *procedure,
                                   trx::runtime::Interpreter &interpreter,
                                   const std::map<std::string, std::string> &pathParams = {}) {
    // Check HTTP method - use custom method if specified, otherwise default based on input
    std::string defaultMethod = procedure->input ? "POST" : "GET";
    std::string expectedMethod = procedure->httpMethod.value_or(defaultMethod);
    if (request.method != expectedMethod) {
        return makeErrorResponse(405, "Method " + request.method + " not allowed. Expected " + expectedMethod);
    }
    
    // For methods that don't typically have a body, skip content-type check
    if (expectedMethod != "GET" && expectedMethod != "HEAD" && expectedMethod != "DELETE") {
        if (!request.headers.count("content-type") || request.headers.at("content-type").find("application/json") == std::string::npos) {
            return makeErrorResponse(400, "Content-Type must be application/json");
        }
    }
    
    try {
        trx::runtime::JsonValue input;
        
        // For methods that support request body, parse it
        if (expectedMethod != "GET" && expectedMethod != "HEAD" && expectedMethod != "DELETE") {
            if (request.body.empty()) {
                input = trx::runtime::JsonValue::object();
            } else {
                JsonParser parser(request.body);
                input = parser.parse();
                if (!std::holds_alternative<trx::runtime::JsonValue::Object>(input.data)) {
                    return makeErrorResponse(400, "Request payload must be a JSON object");
                }
            }
        } else {
            // For methods without body, use empty object
            input = trx::runtime::JsonValue::object();
        }

        // Execute the procedure using the procedure pointer
        std::optional<trx::runtime::JsonValue> outputOpt = interpreter.execute(procedure, input, pathParams);
        if (procedure->output) {
            if (!outputOpt) {
                return makeErrorResponse(500, "Function does not return a value");
            }
            const trx::runtime::JsonValue output = *outputOpt;

            HttpResponse response;
            response.status = getSuccessStatusCode(expectedMethod);
            response.contentType = "application/json";
            response.body = serializeJsonValue(output);
            response.extraHeaders.emplace_back("Access-Control-Allow-Origin", "*");
            response.extraHeaders.emplace_back("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS");
            response.extraHeaders.emplace_back("Access-Control-Allow-Headers", "Content-Type");
            return response;
        } else {
            // Procedure does not return output
            HttpResponse response;
            response.status = getSuccessStatusCode(expectedMethod);
            response.contentType = "application/json";
            response.body = "{}";
            response.extraHeaders.emplace_back("Access-Control-Allow-Origin", "*");
            response.extraHeaders.emplace_back("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            response.extraHeaders.emplace_back("Access-Control-Allow-Headers", "Content-Type");
            return response;
        }
    } catch (const JsonParseError &error) {
        return makeErrorResponse(400, error.what());
    } catch (const trx::runtime::TrxException &error) {
        return makeErrorResponse(400, error.what());
    } catch (const std::exception &error) {
        return makeErrorResponse(500, error.what());
    }
}

HttpResponse handleOptions(const HttpRequest &) {
    HttpResponse response;
    response.status = 204;
    response.contentType = "text/plain";
    response.body.clear();
    response.extraHeaders.emplace_back("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS");
    response.extraHeaders.emplace_back("Access-Control-Allow-Headers", "Content-Type");
    return response;
}

} // anonymous namespace

int runServer(const std::vector<std::filesystem::path> &sourcePaths, ServeOptions options) {

    std::vector<std::filesystem::path> allSourceFiles;
    for (const auto &sourcePath : sourcePaths) {
        std::cout << "[DEBUG] Resolving source path: " << sourcePath << std::endl;
        std::error_code fsError;
        const auto sourceFiles = collectSourceFiles(sourcePath, fsError);
        std::cout << "[DEBUG] collectSourceFiles found " << sourceFiles.size() << " .trx files from " << sourcePath << std::endl;
        if (fsError) {
            std::cerr << "Unable to load TRX sources from " << sourcePath << ": " << fsError.message() << "\n";
            return 1;
        }
        allSourceFiles.insert(allSourceFiles.end(), sourceFiles.begin(), sourceFiles.end());
    }

    if (allSourceFiles.empty()) {
        std::cerr << "No TRX files found in the specified paths\n";
        return 1;
    }

    std::sort(allSourceFiles.begin(), allSourceFiles.end());
    auto last = std::unique(allSourceFiles.begin(), allSourceFiles.end());
    allSourceFiles.erase(last, allSourceFiles.end()); // Remove duplicates

    std::cout << "[DEBUG] Total unique .trx files: " << allSourceFiles.size() << std::endl;

    // Parse all files with separate drivers to avoid context conflicts
    std::vector<trx::ast::Module> modules;
    for (const auto &file : allSourceFiles) {
        trx::parsing::ParserDriver driver;
        if (!driver.parseFile(file)) {
            std::cerr << "Failed to parse " << file << "\n";
            const auto &diagnostics = driver.diagnostics().messages();
            for (const auto &diag : diagnostics) {
                std::cerr << "  - ";
                if (!diag.location.file.empty()) {
                    std::cerr << diag.location.file;
                    if (diag.location.line != 0) {
                        std::cerr << ':' << diag.location.line;
                        if (diag.location.column != 0) {
                            std::cerr << ':' << diag.location.column;
                        }
                    }
                    std::cerr << ' ';
                }
                std::cerr << diag.message << "\n";
            }
            return 1;
        }
        modules.push_back(driver.context().module());
    }

    // Merge modules
    trx::ast::Module combinedModule;
    for (const auto &module : modules) {
        combinedModule.declarations.insert(combinedModule.declarations.end(), module.declarations.begin(), module.declarations.end());
    }

    const auto callableProcedures = collectCallableProcedures(combinedModule);
    const auto records = collectRecords(combinedModule);
    if (callableProcedures.empty()) {
        if (allSourceFiles.size() == 1) {
            std::cerr << "No callable procedures (with matching input/output) were found in " << allSourceFiles.front() << "\n";
        } else {
            std::cerr << "No callable procedures (with matching input/output) were found across " << allSourceFiles.size() << " TRX files in the specified paths\n";
        }
        return 1;
    }

    std::vector<std::string> procedureNames;
    procedureNames.reserve(callableProcedures.size());
    std::map<std::string, const trx::ast::ProcedureDecl *> procedureLookup;
    for (const auto *procedure : callableProcedures) {
        // Use pathTemplate + httpMethod as key to handle multiple procedures with same path but different methods
        std::string defaultMethod = procedure->input ? "POST" : "GET";
        std::string httpMethod = procedure->httpMethod.value_or(defaultMethod);
        std::string key = procedure->name.pathTemplate + "|" + httpMethod;
        auto [_, inserted] = procedureLookup.insert_or_assign(key, procedure);
        if (inserted) {
            procedureNames.push_back(procedure->name.baseName);
        }
    }

    std::string defaultProcedure = procedureNames.front();
    if (options.procedure) {
        const auto it = procedureLookup.find(*options.procedure);
        if (it == procedureLookup.end()) {
            std::cerr << "Procedure '" << *options.procedure << "' not found in module\n";
            return 1;
        }
        defaultProcedure = *options.procedure;
    }

    trx::runtime::Interpreter interpreter(combinedModule, trx::runtime::createDatabaseDriver(options.dbConfig));
    const std::string swaggerSpec = buildSwaggerSpec(procedureLookup, records, options.port);
    const std::string swaggerIndex = buildSwaggerIndexPage();
    const std::string proceduresPayload = buildProceduresPayload(procedureNames, defaultProcedure);

    std::cout << "Loaded " << procedureNames.size() << " procedure(s) from " << allSourceFiles.size() << " source file(s)." << std::endl;

    std::signal(SIGINT, handleSignal);

    const int serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        std::cerr << "Failed to create socket: " << std::strerror(errno) << "\n";
        return 1;
    }

    const int reuse = 1;
    ::setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<unsigned short>(options.port));
    if (::bind(serverFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        std::cerr << "Failed to bind to port " << options.port << ": " << std::strerror(errno) << "\n";
        ::close(serverFd);
        return 1;
    }

    if (::listen(serverFd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen on socket: " << std::strerror(errno) << "\n";
        ::close(serverFd);
        return 1;
    }

    std::cout << "Swagger playground available at http://localhost:" << options.port << "/" << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;

    ThreadPool threadPool(options.threadCount);
    std::mutex interpreterMutex;

    while (!g_stopServer.load()) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        const int clientFd = ::accept(serverFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Accept failed: " << std::strerror(errno) << "\n";
            break;
        }

        threadPool.enqueueTask([clientFd, &procedureLookup, &interpreter, &interpreterMutex, &swaggerIndex, &swaggerSpec, &proceduresPayload]() {
            auto start = std::chrono::high_resolution_clock::now();
            g_metrics.activeRequests++;
            g_metrics.totalRequests++;

            HttpRequest request;
            if (!readHttpRequest(clientFd, request)) {
                g_metrics.activeRequests--;
                ::close(clientFd);
                return;
            }

            HttpResponse response;
            if (request.method == "OPTIONS") {
                response = handleOptions(request);
            } else if (request.path == "/") {
                response.status = 200;
                response.contentType = "text/html; charset=utf-8";
                response.body = swaggerIndex;
            } else if (request.path == "/swagger.json") {
                response.status = 200;
                response.contentType = "application/json";
                response.body = swaggerSpec;
            } else if (request.path == "/procedures") {
                response.status = 200;
                response.contentType = "application/json";
                response.body = proceduresPayload;
            } else if (request.path == "/metrics") {
                response.status = 200;
                response.contentType = "text/plain; version=0.0.4; charset=utf-8";
                std::ostringstream oss;
                oss << "# HELP trx_total_requests Total number of requests processed\n";
                oss << "# TYPE trx_total_requests counter\n";
                oss << "trx_total_requests " << g_metrics.totalRequests.load() << "\n\n";

                oss << "# HELP trx_active_requests Number of currently active requests\n";
                oss << "# TYPE trx_active_requests gauge\n";
                oss << "trx_active_requests " << g_metrics.activeRequests.load() << "\n\n";

                oss << "# HELP trx_error_requests Number of requests that resulted in errors\n";
                oss << "# TYPE trx_error_requests counter\n";
                oss << "trx_error_requests " << g_metrics.errorRequests.load() << "\n\n";

                oss << "# HELP trx_average_duration_ms Average request duration in milliseconds\n";
                oss << "# TYPE trx_average_duration_ms gauge\n";
                oss << "trx_average_duration_ms " << g_metrics.averageDuration << "\n";
                response.body = oss.str();
            } else {
                // Check if path matches a procedure
                auto matchResult = matchPathTemplate(request.path, request.method, procedureLookup);
                if (matchResult) {
                    const auto &[procedure, pathParams] = *matchResult;
                    std::lock_guard<std::mutex> lock(interpreterMutex);
                    response = handleExecuteProcedure(request, procedure, interpreter, pathParams);
                } else {
                    response = makeErrorResponse(404, "Route not found");
                }
            }

            if (response.status >= 400) {
                g_metrics.errorRequests++;
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            {
                std::lock_guard<std::mutex> lock(g_metrics.durationMutex);
                g_metrics.requestDurations.push_back(duration);
                if (g_metrics.requestDurations.size() > 1000) {
                    g_metrics.requestDurations.erase(g_metrics.requestDurations.begin());
                }
                double sum = 0.0;
                for (auto d : g_metrics.requestDurations) sum += d;
                g_metrics.averageDuration = sum / g_metrics.requestDurations.size();
            }

            sendHttpResponse(clientFd, response);
            g_metrics.activeRequests--;
            ::close(clientFd);
        });
    }

    ::close(serverFd);
    std::cout << "Server stopped" << std::endl;
    return 0;
}

} // namespace trx::cli

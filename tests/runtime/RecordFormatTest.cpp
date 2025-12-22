#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runRecordFormatTests() {
    constexpr const char *source = R"TRX(
        TYPE FORMATTEST {
            CUSTOMER_ID INTEGER;
            FULL_NAME CHAR(64) json:"fullName,omitempty";
            STATUS_FLAG CHAR(16) json:"status_flag";
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "record_format.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto *record = findRecord(driver.context().module(), "FORMATTEST");
    if (!expect(record != nullptr, "FORMATTEST record not found")) {
        return false;
    }

    if (!expect(record->fields.size() == 3, "FORMATTEST field count mismatch")) {
        return false;
    }

    const auto &idField = record->fields[0];
    if (!expect(idField.jsonName == "customer_id", "CUSTOMER_ID default json name mismatch") ||
        !expect(!idField.jsonOmitEmpty, "CUSTOMER_ID default omitEmpty should be false") ||
        !expect(!idField.hasExplicitJsonName, "CUSTOMER_ID should not be marked explicit")) {
        return false;
    }

    const auto &nameField = record->fields[1];
    if (!expect(nameField.jsonName == "fullName", "FULL_NAME json name mismatch") ||
        !expect(nameField.jsonOmitEmpty, "FULL_NAME omitempty not detected") ||
        !expect(nameField.hasExplicitJsonName, "FULL_NAME should be marked explicit")) {
        return false;
    }

    const auto &statusField = record->fields[2];
    if (!expect(statusField.jsonName == "status_flag", "STATUS_FLAG json name mismatch") ||
        !expect(!statusField.jsonOmitEmpty, "STATUS_FLAG omitempty should be false") ||
        !expect(statusField.hasExplicitJsonName, "STATUS_FLAG should be marked explicit")) {
        return false;
    }

    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runRecordFormatTests()) {
        std::cerr << "Record format tests failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}
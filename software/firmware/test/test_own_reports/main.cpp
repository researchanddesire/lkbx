#include <unity.h>

#include <string>

#include "../../src/utils/ownReports.h"

void setUp() {}
void tearDown() {}

namespace {

// Shapes of what the box sends and receives on its topic.
const std::string kReport =
    R"({"v":3,"ts":1789188000000,"k":"state","n":"nonce","iv":"iv","ct":"report","sig":"mac"})";
const std::string kCommand =
    R"({"v":3,"ts":1789188000000,"iv":"iv","ct":"command","sig":"mac"})";

bool matches(const OwnReports& reports, const std::string& frame) {
    return reports.matches(frame.data(), frame.size());
}

void remember(OwnReports& reports, const std::string& report) {
    reports.remember(report.data(), report.size());
}

}  // namespace

void test_nothing_matches_before_anything_was_sent() {
    OwnReports reports;
    TEST_ASSERT_FALSE(matches(reports, kReport));
    TEST_ASSERT_FALSE(matches(reports, kCommand));
    TEST_ASSERT_FALSE(reports.matches("", 0));
    TEST_ASSERT_FALSE(reports.matches(nullptr, 0));
    TEST_ASSERT_FALSE(reports.matches(nullptr, 12));
}

void test_a_report_the_box_sent_is_recognised_when_it_comes_back() {
    OwnReports reports;
    remember(reports, kReport);
    TEST_ASSERT_TRUE(matches(reports, kReport));
    // The broker may hand it back more than once.
    TEST_ASSERT_TRUE(matches(reports, kReport));
}

void test_a_command_is_never_taken_for_a_report() {
    OwnReports reports;
    remember(reports, kReport);
    TEST_ASSERT_FALSE(matches(reports, kCommand));
}

void test_only_the_exact_bytes_match() {
    OwnReports reports;
    remember(reports, kReport);
    // One byte different, at either end or in the middle.
    for (size_t index : {size_t{0}, kReport.size() / 2, kReport.size() - 1}) {
        std::string changed = kReport;
        changed[index] = changed[index] == 'x' ? 'y' : 'x';
        TEST_ASSERT_FALSE(matches(reports, changed));
    }
    // Shorter, longer.
    TEST_ASSERT_FALSE(reports.matches(kReport.data(), kReport.size() - 1));
    TEST_ASSERT_FALSE(matches(reports, kReport + " "));
}

void test_the_last_reports_are_remembered_and_older_ones_forgotten() {
    OwnReports reports;
    const size_t sent = OwnReports::kRemembered + 3;
    for (size_t index = 0; index < sent; ++index) {
        remember(reports, kReport + std::to_string(index));
    }
    for (size_t index = 0; index < sent; ++index) {
        const bool expected = index >= sent - OwnReports::kRemembered;
        TEST_ASSERT_EQUAL_INT(
            expected ? 1 : 0,
            matches(reports, kReport + std::to_string(index)) ? 1 : 0);
    }
}

void test_an_empty_report_is_not_remembered() {
    OwnReports reports;
    reports.remember("", 0);
    reports.remember(nullptr, 5);
    TEST_ASSERT_FALSE(reports.matches("", 0));
    TEST_ASSERT_FALSE(matches(reports, kReport));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_matches_before_anything_was_sent);
    RUN_TEST(test_a_report_the_box_sent_is_recognised_when_it_comes_back);
    RUN_TEST(test_a_command_is_never_taken_for_a_report);
    RUN_TEST(test_only_the_exact_bytes_match);
    RUN_TEST(test_the_last_reports_are_remembered_and_older_ones_forgotten);
    RUN_TEST(test_an_empty_report_is_not_remembered);
    return UNITY_END();
}

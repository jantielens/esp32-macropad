#include <gtest/gtest.h>
#include "web_portal_timing.h"

#include <limits>
#include <string>

static uint32_t clock_us = 0;

extern "C" unsigned long micros() {
    return clock_us;
}

struct TimingResponse {
    std::string header_name;
    std::string header_value;

    void addHeader(const char *name, const char *value) {
        header_name = name;
        header_value = value;
    }
};

TEST(WebPortalTiming, ReportsSequentialPhasesAndTotalInMilliseconds) {
    clock_us = 1000;
    WebPortalTiming timing;
    clock_us = 2234;
    timing.mark("auth");
    clock_us = 12234;
    timing.mark("metadata");
    clock_us = 13234;
    TimingResponse response;
    timing.attach(&response);
    EXPECT_EQ(response.header_name, "Server-Timing");
    EXPECT_EQ(response.header_value, "auth;dur=1.234, metadata;dur=10.000, total;dur=12.234");
}

TEST(WebPortalTiming, HandlesMicrosecondClockWraparound) {
    clock_us = std::numeric_limits<uint32_t>::max() - 100;
    WebPortalTiming timing;
    clock_us = 149;
    TimingResponse response;
    timing.attach(&response);
    EXPECT_EQ(response.header_value, "total;dur=0.250");
}

TEST(WebPortalTiming, BoundsHeaderAndReservesSpaceForTotal) {
    clock_us = 0;
    WebPortalTiming timing;
    for (unsigned int phase = 0; phase < 100; ++phase) {
        clock_us += 1000;
        timing.mark("response_preparation");
    }
    TimingResponse response;
    timing.attach(&response);
    EXPECT_LT(response.header_value.size(), 384u);
    EXPECT_NE(response.header_value.find(", total;dur=100.000"), std::string::npos);
    EXPECT_EQ(response.header_value.back(), '0');
    timing.mark("after_attach");
    timing.attach(&response);
    EXPECT_LT(response.header_value.size(), 384u);
}

TEST(WebPortalTiming, AllowsResponseOnlyTotalLabel) {
    clock_us = 0;
    WebPortalTiming timing;
    clock_us = 100;
    TimingResponse response;
    timing.attach(&response, "response_total");
    EXPECT_EQ(response.header_value, "response_total;dur=0.100");
}

TEST(WebPortalTiming, RejectsInvalidAndOversizedMetricNames) {
    clock_us = 0;
    WebPortalTiming timing;
    timing.mark("bad\r\nheader");
    timing.mark("bad;metric");
    timing.mark(nullptr);
    timing.mark("");
    const std::string oversized(400, 'x');
    timing.mark(oversized.c_str());
    timing.mark("valid_phase");
    TimingResponse response;
    timing.attach(&response);
    EXPECT_EQ(response.header_value, "valid_phase;dur=0.000, total;dur=0.000");
}

TEST(WebPortalTiming, KeepsRequestsIndependent) {
    clock_us = 1000;
    WebPortalTiming first;
    clock_us = 2000;
    WebPortalTiming second;
    clock_us = 3000;
    first.mark("first");
    clock_us = 4000;
    second.mark("second");
    TimingResponse first_response;
    TimingResponse second_response;
    first.attach(&first_response);
    second.attach(&second_response);
    EXPECT_EQ(first_response.header_value, "first;dur=2.000, total;dur=3.000");
    EXPECT_EQ(second_response.header_value, "second;dur=2.000, total;dur=2.000");
    first.attach(static_cast<TimingResponse *>(nullptr));
}
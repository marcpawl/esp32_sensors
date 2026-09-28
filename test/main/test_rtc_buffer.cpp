// Tests for the RTC sample buffer (§4.1, §4.2, §4.3, §8).
//
// The buffer is the device's durable log: it accumulates samples across deep
// sleep and is only cleared after a successful upload. Its FIFO overflow rule
// (§4.2) and the record layout (§4.1) are therefore safety-critical — a bug
// here silently loses or corrupts readings.
#include "unity.h"

#include <cstring>

#include "constants.hpp"
#include "rtc_state.hpp"
#include "temp_format.hpp"

using thermo::rtc_buffer_clear;
using thermo::rtc_buffer_push;
using thermo::rtc_wifi_cache_clear;
using thermo::RtcState;
using thermo::SampleRecord;
using thermo::limits::kMaxBufferRecords;

namespace {

SampleRecord make_record(std::uint32_t age, std::uint16_t batt,
                         std::int16_t t1) {
    SampleRecord rec{};
    rec.age_base = age;
    rec.batt_mv = batt;
    rec.t1 = t1;
    rec.t2 = thermo::kTempNull;
    rec.t3 = thermo::kTempNull;
    return rec;
}

}  // namespace

// --- Record layout (§4.1) ---------------------------------------------------

TEST_CASE("rtc: SampleRecord layout is stable at 12 bytes", "[rtc_buffer]") {
    // §4.1 / §8: the RTC block layout must not drift between firmware
    // versions, or retained state is misinterpreted after an update.
    TEST_ASSERT_EQUAL_UINT(12, sizeof(SampleRecord));
    TEST_ASSERT_EQUAL_UINT(4, alignof(SampleRecord));
}

// --- Appending --------------------------------------------------------------

TEST_CASE("rtc: push appends and increments buffer_len", "[rtc_buffer]") {
    RtcState st{};
    TEST_ASSERT_EQUAL_UINT16(0, st.buffer_len);

    rtc_buffer_push(st, make_record(10, 3800, 100));
    TEST_ASSERT_EQUAL_UINT16(1, st.buffer_len);

    rtc_buffer_push(st, make_record(20, 3810, 200));
    TEST_ASSERT_EQUAL_UINT16(2, st.buffer_len);

    TEST_ASSERT_EQUAL_UINT32(10, st.buffer[0].age_base);
    TEST_ASSERT_EQUAL_UINT32(20, st.buffer[1].age_base);
    TEST_ASSERT_EQUAL_INT16(100, st.buffer[0].t1);
    TEST_ASSERT_EQUAL_INT16(200, st.buffer[1].t1);
}

TEST_CASE("rtc: records are retained field-for-field", "[rtc_buffer]") {
    RtcState st{};
    SampleRecord rec = make_record(123, 3950, -437);
    rec.t2 = 2315;
    rec.t3 = 8500;
    rtc_buffer_push(st, rec);

    TEST_ASSERT_EQUAL_UINT32(123, st.buffer[0].age_base);
    TEST_ASSERT_EQUAL_UINT16(3950, st.buffer[0].batt_mv);
    TEST_ASSERT_EQUAL_INT16(-437, st.buffer[0].t1);
    TEST_ASSERT_EQUAL_INT16(2315, st.buffer[0].t2);
    TEST_ASSERT_EQUAL_INT16(8500, st.buffer[0].t3);
}

// --- FIFO overflow (§4.2) ---------------------------------------------------

TEST_CASE("rtc: buffer fills to capacity exactly", "[rtc_buffer]") {
    RtcState st{};
    for (std::size_t i = 0; i < kMaxBufferRecords; ++i) {
        rtc_buffer_push(st, make_record(static_cast<std::uint32_t>(i), 3000,
                                        static_cast<std::int16_t>(i)));
    }
    TEST_ASSERT_EQUAL_UINT16(kMaxBufferRecords, st.buffer_len);
    // Oldest at index 0, newest at the end.
    TEST_ASSERT_EQUAL_UINT32(0, st.buffer[0].age_base);
    TEST_ASSERT_EQUAL_UINT32(kMaxBufferRecords - 1,
                             st.buffer[kMaxBufferRecords - 1].age_base);
}

TEST_CASE("rtc: overflow evicts the oldest sample (FIFO)", "[rtc_buffer]") {
    RtcState st{};
    for (std::size_t i = 0; i < kMaxBufferRecords; ++i) {
        rtc_buffer_push(st,
                        make_record(static_cast<std::uint32_t>(i), 3000, 0));
    }
    // One more sample: capacity is unchanged and the oldest (age 0) is gone.
    rtc_buffer_push(st, make_record(9999, 3000, 0));

    TEST_ASSERT_EQUAL_UINT16(kMaxBufferRecords, st.buffer_len);
    TEST_ASSERT_EQUAL_UINT32(1, st.buffer[0].age_base);  // was index 1
    TEST_ASSERT_EQUAL_UINT32(9999, st.buffer[kMaxBufferRecords - 1].age_base);
}

TEST_CASE("rtc: continued pushes stay at capacity and keep newest",
          "[rtc_buffer]") {
    RtcState st{};
    for (std::uint32_t i = 0; i < kMaxBufferRecords * 2; ++i) {
        rtc_buffer_push(st, make_record(i, 3000, 0));
    }
    TEST_ASSERT_EQUAL_UINT16(kMaxBufferRecords, st.buffer_len);
    // After 2x capacity pushes, the buffer holds the last kMax records.
    TEST_ASSERT_EQUAL_UINT32(kMaxBufferRecords, st.buffer[0].age_base);
    TEST_ASSERT_EQUAL_UINT32(kMaxBufferRecords * 2 - 1,
                             st.buffer[kMaxBufferRecords - 1].age_base);
}

TEST_CASE("rtc: overflow preserves field integrity", "[rtc_buffer]") {
    RtcState st{};
    for (std::size_t i = 0; i < kMaxBufferRecords; ++i) {
        rtc_buffer_push(st, make_record(0, 0, 0));
    }
    SampleRecord rec = make_record(7, 3999, -1234);
    rec.t2 = 4321;
    rec.t3 = thermo::kTempNull;
    rtc_buffer_push(st, rec);

    const SampleRecord& last = st.buffer[kMaxBufferRecords - 1];
    TEST_ASSERT_EQUAL_UINT32(7, last.age_base);
    TEST_ASSERT_EQUAL_UINT16(3999, last.batt_mv);
    TEST_ASSERT_EQUAL_INT16(-1234, last.t1);
    TEST_ASSERT_EQUAL_INT16(4321, last.t2);
    TEST_ASSERT_EQUAL_INT16(thermo::kTempNull, last.t3);
}

// --- Clearing ---------------------------------------------------------------

TEST_CASE("rtc: clear resets buffer_len but not capacity", "[rtc_buffer]") {
    RtcState st{};
    rtc_buffer_push(st, make_record(1, 3000, 1));
    rtc_buffer_push(st, make_record(2, 3000, 2));
    rtc_buffer_clear(st);
    TEST_ASSERT_EQUAL_UINT16(0, st.buffer_len);

    // The buffer is reusable after a clear.
    rtc_buffer_push(st, make_record(3, 3000, 3));
    TEST_ASSERT_EQUAL_UINT16(1, st.buffer_len);
    TEST_ASSERT_EQUAL_UINT32(3, st.buffer[0].age_base);
}

// --- Wi-Fi cache invalidation (§3.10) --------------------------------------

TEST_CASE("rtc: wifi cache clear zeroes the cache and validity",
          "[rtc_buffer]") {
    RtcState st{};
    st.wifi.bssid[0] = 0xAA;
    st.wifi.bssid[5] = 0xBB;
    st.wifi.channel = 6;
    st.wifi.ip = 0xC0A80101;
    st.wifi.valid = 1;

    rtc_wifi_cache_clear(st);

    TEST_ASSERT_EQUAL_UINT8(0, st.wifi.valid);
    TEST_ASSERT_EQUAL_UINT8(0, st.wifi.channel);
    TEST_ASSERT_EQUAL_UINT32(0, st.wifi.ip);
    for (std::uint8_t b : st.wifi.bssid) {
        TEST_ASSERT_EQUAL_UINT8(0, b);
    }
}

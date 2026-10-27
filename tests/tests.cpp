#include <cstdio>
#include <cstdlib>

#include "../src/PendingWrites.h"
#include "../src/VcpMath.h"

namespace
{
int g_failures = 0;
int g_checks = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        ++g_checks;                                                             \
        if (!(condition))                                                       \
        {                                                                       \
            ++g_failures;                                                       \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
        }                                                                       \
    } while (false)

void TestVcpMath()
{
    CHECK(PercentToRaw(0, 100) == 0);
    CHECK(PercentToRaw(100, 100) == 100);
    CHECK(PercentToRaw(50, 100) == 50);
    CHECK(PercentToRaw(0, 255) == 0);
    CHECK(PercentToRaw(100, 255) == 255);
    CHECK(PercentToRaw(50, 255) == 128);

    CHECK(PercentToRaw(-5, 100) == 0);
    CHECK(PercentToRaw(150, 100) == 100);
    CHECK(PercentToRaw(50, 0) == 0);

    CHECK(RawToPercent(0, 100) == 0);
    CHECK(RawToPercent(100, 100) == 100);
    CHECK(RawToPercent(255, 255) == 100);
    CHECK(RawToPercent(128, 255) == 50);
    CHECK(RawToPercent(300, 255) == 100);
    CHECK(RawToPercent(10, 0) == 0);

    // Round trip stays within one percent for any max a monitor might report.
    for (uint32_t max : { 1u, 7u, 50u, 100u, 255u, 1000u, 65535u })
    {
        for (int percent = 0; percent <= 100; ++percent)
        {
            const int back = RawToPercent(PercentToRaw(percent, max), max);
            if (max >= 100)
                CHECK(std::abs(back - percent) <= 1);
            CHECK(back >= 0 && back <= 100);
        }
    }
}

void TestPendingWrites()
{
    PendingWrites writes;
    CHECK(writes.Empty());

    writes.Set({ .monitor = 0, .code = 0x10 }, 10);
    writes.Set({ .monitor = 0, .code = 0x10 }, 20);
    writes.Set({ .monitor = 0, .code = 0x10 }, 30);
    writes.Set({ .monitor = 0, .code = 0x12 }, 70);
    writes.Set({ .monitor = 1, .code = 0x10 }, 40);
    CHECK(!writes.Empty());

    const PendingWrites::Map taken = writes.TakeAll();
    CHECK(writes.Empty());
    CHECK(taken.size() == 3);
    CHECK(taken.at({ .monitor = 0, .code = 0x10 }) == 30);
    CHECK(taken.at({ .monitor = 0, .code = 0x12 }) == 70);
    CHECK(taken.at({ .monitor = 1, .code = 0x10 }) == 40);

    CHECK(writes.TakeAll().empty());
}
} // namespace

int main()
{
    TestVcpMath();
    TestPendingWrites();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#include <cstdio>
#include <cstdlib>

#include "../src/CommandLine.h"
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

bool Fails(std::vector<std::wstring> args)
{
    return !ParseCommandLine(args).error.empty();
}

void TestParseCommandLine()
{
    CHECK(ParseCommandLine({}).error.empty());
    CHECK(ParseCommandLine({}).command.action == Command::Action::Tray);

    ParseResult result = ParseCommandLine({ L"--brightness", L"40" });
    CHECK(result.error.empty());
    CHECK(result.command.action == Command::Action::Apply);
    CHECK(result.command.monitor.empty());
    CHECK(result.command.changes.size() == 1);
    CHECK(result.command.changes[0].code == kVcpBrightness);
    CHECK(result.command.changes[0].value == 40);
    CHECK(!result.command.changes[0].relative);

    result = ParseCommandLine({ L"-m", L"2", L"-B", L"+10", L"--contrast", L"-5" });
    CHECK(result.error.empty());
    CHECK(result.command.monitor == L"2");
    CHECK(result.command.changes.size() == 2);
    CHECK(result.command.changes[0].value == 10 && result.command.changes[0].relative);
    CHECK(result.command.changes[1].code == kVcpContrast);
    CHECK(result.command.changes[1].value == -5 && result.command.changes[1].relative);

    result = ParseCommandLine({ L"--monitor", L"DELL U2414H", L"--brightness", L"0" });
    CHECK(result.error.empty());
    CHECK(result.command.monitor == L"DELL U2414H");
    CHECK(result.command.changes[0].value == 0);

    CHECK(ParseCommandLine({ L"--list" }).command.action == Command::Action::List);
    CHECK(ParseCommandLine({ L"-l" }).error.empty());
    CHECK(ParseCommandLine({ L"--help" }).command.action == Command::Action::Help);
    CHECK(ParseCommandLine({ L"/?" }).command.action == Command::Action::Help);
    CHECK(ParseCommandLine({ L"-b", L"40", L"-h" }).command.action == Command::Action::Help);

    CHECK(Fails({ L"--brightness", L"101" }));
    CHECK(Fails({ L"--brightness", L"+101" }));
    CHECK(Fails({ L"--brightness", L"abc" }));
    CHECK(Fails({ L"--brightness", L"+" }));
    CHECK(Fails({ L"--brightness", L"5%" }));
    CHECK(Fails({ L"--brightness" }));
    CHECK(Fails({ L"--brightness", L"" }));
    CHECK(Fails({ L"--brightness", L"10", L"-b", L"20" }));
    CHECK(Fails({ L"--monitor", L"1" }));
    CHECK(Fails({ L"-m", L"1", L"-m", L"2", L"-b", L"5" }));
    CHECK(Fails({ L"--volume", L"5" }));
    CHECK(Fails({ L"--list", L"-b", L"5" }));
    CHECK(Fails({ L"--list", L"-m", L"1" }));
}

void TestSelectMonitors()
{
    const std::vector<std::wstring> names{ L"DELL U2414H", L"LG 27UK850", L"Dell U2414H" };
    CHECK(SelectMonitors(L"", names) == (std::vector<size_t>{ 0, 1, 2 }));
    CHECK(SelectMonitors(L"2", names) == (std::vector<size_t>{ 1 }));
    CHECK(SelectMonitors(L"3", names) == (std::vector<size_t>{ 2 }));
    CHECK(SelectMonitors(L"0", names).empty());
    CHECK(SelectMonitors(L"9", names).empty());
    CHECK(SelectMonitors(L"99999999999", names).empty());
    CHECK(SelectMonitors(L"dell u2414h", names) == (std::vector<size_t>{ 0, 2 }));
    CHECK(SelectMonitors(L"lg 27uk850", names) == (std::vector<size_t>{ 1 }));
    CHECK(SelectMonitors(L"DELL", names).empty());
    CHECK(SelectMonitors(L"", {}).empty());
}

void TestResolvePercent()
{
    CHECK(ResolvePercent(50, { .value = 30 }) == 30);
    CHECK(ResolvePercent(50, { .value = 10, .relative = true }) == 60);
    CHECK(ResolvePercent(50, { .value = -10, .relative = true }) == 40);
    CHECK(ResolvePercent(95, { .value = 10, .relative = true }) == 100);
    CHECK(ResolvePercent(5, { .value = -10, .relative = true }) == 0);
    CHECK(ResolvePercent(0, { .value = 100 }) == 100);
}

void TestArgumentPacking()
{
    const std::vector<std::wstring> args{ L"--monitor", L"DELL U2414H", L"", L"-b", L"+10" };
    CHECK(SplitArguments(JoinArguments(args)) == args);
    CHECK(SplitArguments(JoinArguments({})).empty());
    CHECK(SplitArguments(L"a") == (std::vector<std::wstring>{ L"a" }));
}
} // namespace

int main()
{
    TestVcpMath();
    TestPendingWrites();
    TestParseCommandLine();
    TestSelectMonitors();
    TestResolvePercent();
    TestArgumentPacking();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

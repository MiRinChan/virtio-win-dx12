// SPDX-License-Identifier: MIT
// Count kernel vblank and flip events without a user thread waiting on each vblank.
// Run elevated: vsyncprobe [seconds]
#include <windows.h>
#include <evntcons.h>
#include <evntrace.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <thread>
#include <vector>

// Microsoft-Windows-DxgKrnl
static const GUID dxgkrnl = {0x802ec45a, 0x1e99, 0x4b83, {0x99, 0x20, 0x87, 0xc9, 0x82, 0x77, 0xba, 0x9d}};
static std::vector<LONGLONG> vblanks, flips;

static void WINAPI on_event(EVENT_RECORD* record)
{
    if (!IsEqualGUID(record->EventHeader.ProviderId, dxgkrnl))
        return;
    const auto id = record->EventHeader.EventDescriptor.Id;
    if (id == 17)
        vblanks.push_back(record->EventHeader.TimeStamp.QuadPart);
    if (id == 116)
        flips.push_back(record->EventHeader.TimeStamp.QuadPart);
}

int main(int argc, char** argv)
{
    const unsigned seconds = argc == 2 ? std::strtoul(argv[1], nullptr, 10) : 20;
    if (!seconds || seconds > 120)
        return 2;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    wchar_t name[64];
    std::swprintf(name, 64, L"vgd-vsyncprobe-%lu", GetCurrentProcessId());
    struct Properties { EVENT_TRACE_PROPERTIES p; wchar_t name[64]; } props = {};
    props.p.Wnode.BufferSize = sizeof(props);
    props.p.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props.p.Wnode.ClientContext = 1;
    props.p.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props.p.BufferSize = 64;
    props.p.MinimumBuffers = 4;
    props.p.FlushTimer = 1;
    props.p.LoggerNameOffset = offsetof(Properties, name);
    TRACEHANDLE session = 0;
    ULONG status = StartTraceW(&session, name, &props.p);
    if (status) {
        std::printf("FAIL StartTrace=%lu (run elevated)\n", status);
        return 1;
    }
    status = EnableTraceEx2(session, &dxgkrnl, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
                            0x8000000, 0, 0, nullptr);
    if (status) {
        ControlTraceW(session, nullptr, &props.p, EVENT_TRACE_CONTROL_STOP);
        std::printf("FAIL EnableTrace=%lu\n", status);
        return 1;
    }
    EVENT_TRACE_LOGFILEW logfile = {};
    logfile.LoggerName = name;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = on_event;
    TRACEHANDLE consumer = OpenTraceW(&logfile);
    if (consumer == INVALID_PROCESSTRACE_HANDLE) {
        status = GetLastError();
        ControlTraceW(session, nullptr, &props.p, EVENT_TRACE_CONTROL_STOP);
        std::printf("FAIL OpenTrace=%lu\n", status);
        return 1;
    }
    std::thread reader([&] { ProcessTrace(&consumer, 1, nullptr, nullptr); });
    std::printf("trace_ready seconds=%u\n", seconds);
    Sleep(seconds * 1000);
    status = ControlTraceW(session, nullptr, &props.p, EVENT_TRACE_CONTROL_STOP);
    reader.join();
    CloseTrace(consumer);
    std::printf("trace_stop=%lu events_lost=%lu buffers_lost=%lu\n", status,
                props.p.EventsLost, props.p.RealTimeBuffersLost);
    // ProcessTrace converts event timestamps to 100 ns system time units.
    for (const auto t : vblanks)
        std::printf("vsync_filetime=%lld\n", t);
    for (const auto t : flips)
        std::printf("flip_filetime=%lld\n", t);
    std::printf("vsync_events=%zu flip_events=%zu\n", vblanks.size(), flips.size());
    return status || props.p.EventsLost || props.p.RealTimeBuffersLost || vblanks.empty() ? 1 : 0;
}

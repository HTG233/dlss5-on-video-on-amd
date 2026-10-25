#include "AmdNrBackend.h"

#include <windows.h>

#include <cwchar>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace {

void CopyText(char* dst, size_t dstSize, const char* text)
{
    if (!dst || dstSize == 0) return;
    if (!text) { dst[0] = '\0'; return; }
    strncpy_s(dst, dstSize, text, _TRUNCATE);
}

void JoinPath(wchar_t* dst, size_t dstChars, const wchar_t* dir, const wchar_t* name)
{
    if (!dst || dstChars == 0) return;
    if (!dir || !name) { dst[0] = L'\0'; return; }
    std::swprintf(dst, dstChars, L"%s\\%s", dir, name);
}

}  // namespace

AmdNrBackend::~AmdNrBackend()
{
    Shutdown();
}

void AmdNrBackend::SetError(const char* text)
{
    CopyText(m_lastError, sizeof m_lastError, text);
}

void AmdNrBackend::RefreshLastError(int32_t status, const char* stage)
{
    char detail[400] = {};
    if (m_api.GetLastError) m_api.GetLastError(detail, static_cast<uint32_t>(sizeof detail));
    std::snprintf(m_lastError, sizeof m_lastError, "%s failed status=%d%s%s", stage, status,
                  detail[0] ? ": " : "", detail);
}

bool AmdNrBackend::ResolveAbi()
{
    m_api.struct_size = static_cast<uint32_t>(sizeof(amdnr::Api));
    m_api.abi_version = 0;

    using FnGetApi = amdnr::FnGetApi;
    const auto getApi = reinterpret_cast<FnGetApi>(GetProcAddress(static_cast<HMODULE>(m_module), amdnr::kExportGetApi));
    if (!getApi) { SetError("MochizukiNrGetApi not exported"); return false; }

    const int32_t status = getApi(amdnr::kAbiVersion, &m_api);
    if (status != amdnr::Ok) { RefreshLastError(status, "GetApi"); return false; }
    if (m_api.abi_version != amdnr::kAbiVersion)
    {
        std::snprintf(m_lastError, sizeof m_lastError, "abi_version %u, expected %u",
                      m_api.abi_version, amdnr::kAbiVersion);
        return false;
    }

    // Everything the record/submit path below calls. A partial table is a host
    // and runtime version mismatch, which the runtime's own host refuses too.
    if (!m_api.QueryCapabilities || !m_api.Create || !m_api.Destroy || !m_api.PrepareSession ||
        !m_api.RecordInputs || !m_api.EnqueueHip || !m_api.RecordOutputs ||
        !m_api.Poll || !m_api.Retire || !m_api.GetStatus || !m_api.GetLastError)
    {
        SetError("runtime ABI is missing required entry points");
        return false;
    }

    m_prepareFrame = reinterpret_cast<amdnr::FnPrepareFrame>(
        GetProcAddress(static_cast<HMODULE>(m_module), amdnr::kExportPrepareFrame));
    m_getControlDefaults = reinterpret_cast<amdnr::FnGetControlDefaults>(
        GetProcAddress(static_cast<HMODULE>(m_module), amdnr::kExportGetControlDefaults));
    m_setControls = reinterpret_cast<amdnr::FnSetControls>(
        GetProcAddress(static_cast<HMODULE>(m_module), amdnr::kExportSetControls));
    m_getBuildProgress = reinterpret_cast<amdnr::FnGetBuildProgress>(
        GetProcAddress(static_cast<HMODULE>(m_module), amdnr::kExportGetBuildProgress));
    if (!m_prepareFrame || !m_getControlDefaults || !m_setControls)
    {
        SetError("Mochizuki frame or controls export missing");
        return false;
    }
    return true;
}

bool AmdNrBackend::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                              const wchar_t* assetsDir, uint32_t width, uint32_t height,
                              uint32_t controlsBytes)
{
    Shutdown();
    SetError("");
    if (!device || !queue || !assetsDir)
    {
        SetError("Initialize needs a device, a queue and an assets directory");
        return false;
    }

    wchar_t dllPath[MAX_PATH] = {};
    JoinPath(dllPath, MAX_PATH, assetsDir, L"MochizukiNrRuntime.dll");
    if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES)
    {
        std::snprintf(m_lastError, sizeof m_lastError, "runtime not found: %ls", dllPath);
        return false;
    }

    // LOAD_WITH_ALTERED_SEARCH_PATH so the runtime resolves its own dependencies
    // out of its folder rather than the player's.
    m_module = LoadLibraryExW(dllPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m_module)
    {
        std::snprintf(m_lastError, sizeof m_lastError, "LoadLibrary failed (%lu): %ls",
                      GetLastError(), dllPath);
        return false;
    }

    m_device = device;

    if (!ResolveAbi()) { Shutdown(); return false; }

    // The runtime's own log is the only place a rejected frame explains itself.
    if (const auto setLogging = reinterpret_cast<amdnr::FnSetLogging>(
            GetProcAddress(static_cast<HMODULE>(m_module), amdnr::kExportSetLogging)))
        setLogging(1);

    amdnr::Capabilities caps{};
    caps.struct_size = static_cast<uint32_t>(sizeof caps);
    if (m_api.QueryCapabilities(&caps) != amdnr::Ok)
    {
        SetError("QueryCapabilities failed");
        Shutdown();
        return false;
    }
    if (width > caps.max_input_width || height > caps.max_input_height)
    {
        std::snprintf(m_lastError, sizeof m_lastError, "%ux%u exceeds the runtime limit %ux%u",
                      width, height, caps.max_input_width, caps.max_input_height);
        Shutdown();
        return false;
    }

    amdnr::CreateInfo info{};
    info.struct_size = static_cast<uint32_t>(sizeof info);
    info.device = device;
    info.queue = queue;
    info.assets_directory = assetsDir;
    info.flags = amdnr::kCreateFlagRecordingLeases;

    int32_t status = m_api.Create(&info, &m_session);
    if (status != amdnr::Ok || !m_session)
    {
        RefreshLastError(status, "Create");
        m_session = nullptr;
        Shutdown();
        return false;
    }

    // Controls must be pushed before PrepareSession: the runtime builds the
    // network from them, and PrepareFrame rejects a session that never had any.
    // The shipped DLL may be older than the header this host was written against,
    // so the controls size is a parameter: the caller can probe what it accepts.
    std::vector<unsigned char> storage(controlsBytes ? controlsBytes : amdnr::kControlsBytes, 0);
    struct ControlsHead { uint32_t struct_size; };
    auto* controls = reinterpret_cast<ControlsHead*>(storage.data());
    controls->struct_size = static_cast<uint32_t>(storage.size());
    status = m_getControlDefaults(controls);
    if (status != amdnr::Ok)
    {
        RefreshLastError(status, "GetControlDefaults");
        Shutdown();
        return false;
    }
    if (controls->struct_size != static_cast<uint32_t>(storage.size()))
    {
        std::snprintf(m_lastError, sizeof m_lastError, "controls size %u, host sent %u",
                      controls->struct_size, static_cast<unsigned>(storage.size()));
        Shutdown();
        return false;
    }
    status = m_setControls(m_session, controls);
    if (status != amdnr::Ok)
    {
        RefreshLastError(status, "SetControls");
        Shutdown();
        return false;
    }

    status = m_api.PrepareSession(m_session);
    if (status != amdnr::Ok)
    {
        RefreshLastError(status, "PrepareSession");
        Shutdown();
        return false;
    }

    if (!WaitForBuild(180000)) { Shutdown(); return false; }

    {
        wchar_t passesText[16] = {};
        if (GetEnvironmentVariableW(L"AMDNR_PASSES", passesText, 16) > 0)
        {
            const int requested = _wtoi(passesText);
            if (requested >= 1 && requested <= 3) m_passes = static_cast<uint32_t>(requested);
        }
    }

    m_width = width;
    m_height = height;
    m_frameId = 0;
    m_evaluations = 0;
    return true;
}

// PrepareSession returns before the network exists: the runtime builds it from
// the weights on its own thread and rejects every frame until that finishes.
bool AmdNrBackend::WaitForBuild(uint32_t timeoutMs)
{
    if (!m_getBuildProgress) return true;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    uint32_t lastTotal = 0;
    uint32_t lastDone = 0;
    while (GetTickCount64() < deadline)
    {
        amdnr::BuildProgress progress{};
        progress.struct_size = static_cast<uint32_t>(sizeof progress);
        const int32_t status = m_getBuildProgress(m_session, &progress);
        if (status != amdnr::Ok) return true;  // not reported: treat as ready
        // total == 0 and nothing active means no build is running: nothing to wait for.
        if (progress.active == 0 && (progress.total == 0 || progress.completed >= progress.total)) return true;
        if (progress.total != lastTotal || progress.completed != lastDone)
        {
            lastTotal = progress.total;
            lastDone = progress.completed;
            std::printf("  build %u/%u %s\n", progress.completed, progress.total, progress.stage);
            std::fflush(stdout);
        }
        Sleep(200);
    }
    std::snprintf(m_lastError, sizeof m_lastError, "network build did not finish in %u ms", timeoutMs);
    return false;
}

bool AmdNrBackend::Evaluate(ID3D12GraphicsCommandList* cmd, ID3D12CommandQueue* queue,
                            ID3D12Resource* color, D3D12_RESOURCE_STATES colorState,
                            ID3D12Resource* output,
                            ID3D12Resource* depth, D3D12_RESOURCE_STATES depthState,
                            ID3D12Resource* motion, D3D12_RESOURCE_STATES motionState,
                            bool reset, float /*frameTimeMs*/)
{
    if (!m_session || !cmd || !queue || !color || !output || !depth || !motion)
    {
        SetError("Evaluate called with an incomplete frame");
        return false;
    }

    (void)output;  // the runtime allocates its own output; this stays for the caller's contract
    const D3D12_RESOURCE_DESC colorDesc = color->GetDesc();
    const D3D12_RESOURCE_DESC depthDesc = depth->GetDesc();
    const D3D12_RESOURCE_DESC motionDesc = motion->GetDesc();

    amdnr::FrameInfo frame{};
    frame.struct_size = m_frameStructSize;
    frame.frame_id = 0;
    frame.color = color;
    frame.color_width = static_cast<uint32_t>(colorDesc.Width);
    frame.color_height = colorDesc.Height;
    frame.color_state = static_cast<uint32_t>(colorState);
    frame.flags = amdnr::kFrameFlagStrength;
    frame.transfer_strength = 1.0f;
    frame.color_strength = 1.0f;
    frame.model_scale = 1.0f;
    frame.passes = m_passes;
    // Guides start out absent: the reference host proves the path with colour
    // alone, and each guide is added back only once it is shown to be accepted.
    if (m_useGuides)
    {
        frame.motion = motion;
        frame.motion_state = static_cast<uint32_t>(motionState);
        frame.motion_width = static_cast<uint32_t>(motionDesc.Width);
        frame.motion_height = motionDesc.Height;
        frame.motion_scale_x = 1.0f;
        frame.motion_scale_y = 1.0f;
        frame.depth = depth;
        frame.depth_state = static_cast<uint32_t>(depthState);
        frame.depth_width = static_cast<uint32_t>(depthDesc.Width);
        frame.depth_height = depthDesc.Height;
        frame.reset = reset ? 1u : 0u;
        frame.flags |= amdnr::kFrameFlagTemporal;
    }

    amdnr::Job job{};
    job.struct_size = static_cast<uint32_t>(sizeof job);
    // The runtime allocates its own output and publishes it in job.private_output;
    // the host leaves the slot null and reads it back after PrepareFrame.

    // The first frame starts the network build. While it runs the runtime
    // answers UNAVAILABLE, which is a "come back later", not a rejection.
    int32_t status = amdnr::Unavailable;
    const ULONGLONG deadline = GetTickCount64() + 600000;
    while (status == amdnr::Unavailable && GetTickCount64() < deadline)
    {
        status = m_prepareFrame(m_session, &frame, &job);
        if (status == amdnr::Unavailable) Sleep(200);
    }
    if (status == amdnr::Unavailable)
    {
        SetError("network build still running after 10 minutes");
        return false;
    }
    if (status != amdnr::Ok)
    {
        RefreshLastError(status, "PrepareFrame");
        char detail[400] = {};
        if (m_api.GetStatus) m_api.GetStatus(m_session, detail, static_cast<uint32_t>(sizeof detail));
        if (detail[0]) std::snprintf(m_lastError, sizeof m_lastError, "%s | status: %s", m_lastError, detail);
        return false;
    }
    if (!job.handle)
    {
        SetError("PrepareFrame returned no job handle");
        return false;
    }

    // Everything after PrepareFrame addresses the job by the handle it published.
    status = m_api.RecordInputs(m_session, job.handle, cmd);
    if (status != amdnr::Ok) { RefreshLastError(status, "RecordInputs"); return false; }

    status = m_api.EnqueueHip(m_session, job.handle, queue);
    if (status != amdnr::Ok) { RefreshLastError(status, "EnqueueHip"); return false; }

    status = m_api.RecordOutputs(m_session, job.handle, cmd);
    if (status != amdnr::Ok) { RefreshLastError(status, "RecordOutputs"); return false; }

    ++m_frameId;
    ++m_evaluations;
    return true;
}


bool AmdNrBackend::EnsureLists()
{
    if (m_inList) return true;
    if (!m_device) return false;
    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_inAllocator)))) return false;
    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_outAllocator)))) return false;
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_inAllocator, nullptr, IID_PPV_ARGS(&m_inList)))) return false;
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_outAllocator, nullptr, IID_PPV_ARGS(&m_outList)))) return false;
    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) return false;
    m_inList->Close();
    m_outList->Close();
    return true;
}

// The order is the runtime's own test host's: record both lists, Begin on the
// queue, submit inputs, enqueue the network, submit outputs, signal, End.
bool AmdNrBackend::EvaluateFrame(ID3D12CommandQueue* queue, ID3D12Resource* color,
                                 D3D12_RESOURCE_STATES colorState, bool reset,
                                 ID3D12Resource* motion, D3D12_RESOURCE_STATES motionState)
{
    if (!m_session || !queue || !color) { SetError("EvaluateFrame needs a session, queue and colour"); return false; }
    if (!EnsureLists()) { SetError("command lists could not be created"); return false; }

    // GetTickCount64 only advances every ~15.6 ms, so a small frame measures as
    // zero and the runner's per-frame GPU-time floor then reads it as "the neural
    // pass never ran". QPC has the resolution to tell the two apart.
    LARGE_INTEGER passFrequency{}, passStart{}, passEnd{};
    QueryPerformanceFrequency(&passFrequency);
    QueryPerformanceCounter(&passStart);
    amdnr::FrameInfo frame{};
    frame.struct_size = m_frameStructSize;
    frame.color = color;
    frame.color_width = static_cast<uint32_t>(color->GetDesc().Width);
    frame.color_height = color->GetDesc().Height;
    frame.color_state = static_cast<uint32_t>(colorState);
    frame.flags = amdnr::kFrameFlagStrength | amdnr::kFrameFlagInlineExecution;
    frame.transfer_strength = 1.0f;
    frame.color_strength = 1.0f;
    frame.model_scale = 1.0f;
    frame.passes = m_passes;
    frame.reset = reset ? 1u : 0u;
    // Motion vectors live inside the 88-byte contract; depth does not, because
    // the shipped runtime predates those fields.
    if (motion)
    {
        const D3D12_RESOURCE_DESC md = motion->GetDesc();
        frame.motion = motion;
        frame.motion_state = static_cast<uint32_t>(motionState);
        frame.motion_width = static_cast<uint32_t>(md.Width);
        frame.motion_height = md.Height;
        frame.motion_scale_x = 1.0f;
        frame.motion_scale_y = 1.0f;
        frame.flags |= amdnr::kFrameFlagTemporal;
    }

    amdnr::Job job{};
    job.struct_size = static_cast<uint32_t>(sizeof(amdnr::Job));

    int32_t status = amdnr::Unavailable;
    const ULONGLONG deadline = GetTickCount64() + 600000;
    while (status == amdnr::Unavailable && GetTickCount64() < deadline)
    {
        status = m_prepareFrame(m_session, &frame, &job);
        if (status == amdnr::Unavailable) Sleep(200);
    }
    if (status != amdnr::Ok || !job.handle)
    {
        RefreshLastError(status, "PrepareFrame");
        return false;
    }

    m_lastOutput = static_cast<ID3D12Resource*>(job.private_output);

    m_inAllocator->Reset();
    m_outAllocator->Reset();
    m_inList->Reset(m_inAllocator, nullptr);
    m_outList->Reset(m_outAllocator, nullptr);

    status = m_api.RecordInputs(m_session, job.handle, m_inList);
    if (status != amdnr::Ok) { RefreshLastError(status, "RecordInputs"); return false; }
    status = m_api.RecordOutputs(m_session, job.handle, m_outList);
    if (status != amdnr::Ok) { RefreshLastError(status, "RecordOutputs"); return false; }
    if (FAILED(m_inList->Close()) || FAILED(m_outList->Close())) { SetError("command list close failed"); return false; }

    status = m_api.BeginRecordingExecution(m_session, job.handle, queue);
    if (status != amdnr::Ok) { RefreshLastError(status, "BeginRecordingExecution"); return false; }

    ID3D12CommandList* inLists[] = { m_inList };
    queue->ExecuteCommandLists(1, inLists);

    status = m_api.EnqueueHip(m_session, job.handle, queue);
    if (status != amdnr::Ok) { RefreshLastError(status, "EnqueueHip"); return false; }

    ID3D12CommandList* outLists[] = { m_outList };
    queue->ExecuteCommandLists(1, outLists);

    const uint64_t value = ++m_fenceValue;
    const HRESULT signal = queue->Signal(m_fence, value);
    status = m_api.EndRecordingExecution(m_session, job.handle, queue, 3, m_fence, value, static_cast<int32_t>(signal));
    if (status != amdnr::Ok) { RefreshLastError(status, "EndRecordingExecution"); return false; }

    if (SUCCEEDED(signal) && m_fence->GetCompletedValue() < value)
    {
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event)
        {
            m_fence->SetEventOnCompletion(value, event);
            WaitForSingleObject(event, 60000);
            CloseHandle(event);
        }
    }

    if (m_api.InvalidateRecording) m_api.InvalidateRecording(m_session, job.handle);
    if (m_api.CollectRecording) m_api.CollectRecording(m_session, job.handle);

    QueryPerformanceCounter(&passEnd);
    m_lastGpuMs = passFrequency.QuadPart
        ? double(passEnd.QuadPart - passStart.QuadPart) * 1000.0 / double(passFrequency.QuadPart)
        : 0.0;
    ++m_evaluations;
    return true;
}

void AmdNrBackend::Shutdown()
{
    if (m_session && m_api.Destroy)
    {
        if (m_api.Drain) m_api.Drain(m_session);
        m_api.Destroy(m_session);
    }
    m_session = nullptr;
    m_api = amdnr::Api{};

    if (m_inList) { m_inList->Release(); m_inList = nullptr; }
    if (m_outList) { m_outList->Release(); m_outList = nullptr; }
    if (m_inAllocator) { m_inAllocator->Release(); m_inAllocator = nullptr; }
    if (m_outAllocator) { m_outAllocator->Release(); m_outAllocator = nullptr; }
    if (m_fence) { m_fence->Release(); m_fence = nullptr; }
    m_device = nullptr;

    if (m_module)
    {
        FreeLibrary(static_cast<HMODULE>(m_module));
        m_module = nullptr;
    }
    m_width = 0;
    m_height = 0;
}

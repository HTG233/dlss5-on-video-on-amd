#pragma once

#include <d3d12.h>
#include <cstdint>

#include "AmdNrAbi.h"

// AMD neural-rendering backend: the AMD counterpart of DLSSBackend.
//
// Where DLSSBackend fills an NGX parameter block and calls EvaluateFeature, this
// class fills the AMD runtime's LmxxfNrFrameInfo and drives its job state machine
// (PrepareFrame -> RecordInputs -> EnqueueHip -> RecordOutputs). The inputs are
// the same four resources the NGX path uses, which is what makes the substitution
// a mapping rather than a rewrite:
//
//   NGX Parameter_Color         -> FrameInfo.color
//   NGX Parameter_Depth         -> FrameInfo.depth
//   NGX Parameter_MotionVectors -> FrameInfo.motion
//   NGX Parameter_Reset         -> FrameInfo.reset
//   NGX MV_Scale_X/Y = 1.0      -> FrameInfo.motion_scale_x/y
//
// The NGX path pins jitter to zero and the AMD runtime has no jitter input at all,
// so nothing has to be synthesised for that.
//
// Status: first cut. It builds and fails loudly; it has not been run against a
// real runtime and weights yet, so treat every return path as unverified until
// the DLL and dlssnr.bin are staged beside the worker.
class AmdNrBackend {
public:
    ~AmdNrBackend();

    // assetsDir is the folder holding MochizukiNrRuntime.dll and dlssnr.bin.
    // The runtime is loaded from there, never from the player's own directory,
    // so the player stays free of the proxy the shipped build uses.
    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                    const wchar_t* assetsDir, uint32_t width, uint32_t height,
                    uint32_t controlsBytes = amdnr::kControlsBytes);

    // Records the neural pass into cmd. The caller submits cmd and queue; the
    // runtime's own output is copied into `output`, which must be in
    // D3D12_RESOURCE_STATE_COPY_DEST before this call.
    bool Evaluate(ID3D12GraphicsCommandList* cmd, ID3D12CommandQueue* queue,
                  ID3D12Resource* color, D3D12_RESOURCE_STATES colorState,
                  ID3D12Resource* output,
                  ID3D12Resource* depth, D3D12_RESOURCE_STATES depthState,
                  ID3D12Resource* motion, D3D12_RESOURCE_STATES motionState,
                  bool reset, float frameTimeMs);

    // Self-contained frame: records, submits and waits on its own lists, then
    // retires the job. This is the sequence the runtime's own test host uses.
    bool EvaluateFrame(ID3D12CommandQueue* queue, ID3D12Resource* color,
                       D3D12_RESOURCE_STATES colorState, bool reset,
                       ID3D12Resource* motion = nullptr,
                       D3D12_RESOURCE_STATES motionState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    void Shutdown();

    // The shipped DLL can be older than the header this host was written
    // against, so the frame struct size is overridable for probing.
    void SetFrameStructSize(uint32_t bytes) { m_frameStructSize = bytes; }
    bool Available() const { return m_session != nullptr; }
    uint64_t EvaluationCount() const { return m_evaluations; }
    // The resource the runtime rendered into, published by PrepareFrame and
    // released when the next frame retires it.
    ID3D12Resource* LastOutput() const { return m_lastOutput; }
    // Wall time of the last pass, from PrepareFrame to the fence it waited on.
    // The neural floor gate needs a per-frame cost; this includes the submit
    // wait, so it is an upper bound rather than a pure kernel time.
    double LastGpuMs() const { return m_lastGpuMs; }
    // Passes through the network per frame; 1 is the per-frame reconstruction,
    // 2 and 3 add the deeper passes the preset asks for.
    void SetPasses(uint32_t passes) { m_passes = passes ? passes : 1; }
    const char* LastError() const { return m_lastError; }
    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }

private:
    bool ResolveAbi();
    void RefreshLastError(int32_t status, const char* stage);
    void SetError(const char* text);

    ID3D12Device* m_device = nullptr;
    void* m_module = nullptr;
    amdnr::Api m_api{};
    // The Mochizuki build takes its own frame struct through this export, not
    // through Api::PrepareFrame: that table slot is the lmxxf contract and the
    // runtime refuses it with "Use MochizukiNrPrepareFrame from the current
    // package".
    amdnr::FnPrepareFrame m_prepareFrame = nullptr;
    amdnr::FnGetControlDefaults m_getControlDefaults = nullptr;
    amdnr::FnSetControls m_setControls = nullptr;
    amdnr::FnGetBuildProgress m_getBuildProgress = nullptr;
    bool WaitForBuild(uint32_t timeoutMs);
    void* m_session = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint64_t m_evaluations = 0;
    uint64_t m_frameId = 0;
    // Guides are opt-in while the frame contract is being brought up.
    bool m_useGuides = false;
    uint32_t m_frameStructSize = amdnr::kFrameBytes;
    ID3D12CommandAllocator* m_inAllocator = nullptr;
    ID3D12CommandAllocator* m_outAllocator = nullptr;
    ID3D12GraphicsCommandList* m_inList = nullptr;
    ID3D12GraphicsCommandList* m_outList = nullptr;
    ID3D12Fence* m_fence = nullptr;
    uint64_t m_fenceValue = 0;
    ID3D12Resource* m_lastOutput = nullptr;
    double m_lastGpuMs = 0.0;
    uint32_t m_passes = 1;
    bool EnsureLists();
    char m_lastError[512] = {};
};

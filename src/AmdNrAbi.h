#pragma once

// Interface declarations for the AMD neural-rendering runtime (Mochizuki build).
//
// These are the plain-C layouts the runtime exports. They were taken from the
// public ABI description of the community runtime and independently confirmed
// against MochizukiNrRuntime.dll with dumpbin: MochizukiNrGetApi writes
// struct_size 0xB0 (176) and abi_version 6, which is exactly
// 8 + 21 * sizeof(void*) for the table below. No implementation is copied
// here - only the interface, so the layouts must not be edited to "tidy" them.

#include <cstdint>

namespace amdnr {

constexpr uint32_t kAbiVersion = 6;
constexpr uint32_t kCreateFlagZeroOutputFallback = 1u << 0;
constexpr uint32_t kCreateFlagRecordingLeases = 1u << 1;
constexpr uint32_t kCreateFlagInlineQueue = 1u << 2;

constexpr uint32_t kFrameFlagStrength = 1u << 0;
constexpr uint32_t kFrameFlagTemporal = 1u << 8;
constexpr uint32_t kFrameFlagInlineExecution = 1u << 9;

enum Status : int32_t {
    Ok = 0,
    UnsupportedAbi = 1,
    InvalidArgument = 2,
    NotImplemented = 3,
    Unavailable = 4,
    Failed = 5,
    DeviceLost = 6,
};

struct Capabilities {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t max_input_width;
    uint32_t max_input_height;
    uint32_t history_supported;
    uint32_t overlap_supported;
    uint32_t graph_supported;
    uint32_t gfx1201_target;
};

struct CreateInfo {
    uint32_t struct_size;
    void* device;   // ID3D12Device*
    void* queue;    // ID3D12CommandQueue*, same device
    const wchar_t* assets_directory;
    uint32_t flags;
};

struct Job {
    uint32_t struct_size;
    void* handle;             // opaque runtime handle
    void* private_output;     // ID3D12Resource* the runtime renders into (SR)
    void* first_pass_output;  // optional FP16 view, same lease as private_output
};

struct FrameInfo {
    uint32_t struct_size;
    uint64_t frame_id;
    void* color;
    uint32_t color_width, color_height, color_state, flags;
    float transfer_strength, color_strength, model_scale;
    uint32_t passes;
    void* motion;
    uint32_t motion_state, motion_width, motion_height;
    float motion_scale_x, motion_scale_y;
    uint32_t reset;
    void* depth;
    uint32_t depth_state, depth_width, depth_height;
    uint32_t depth_inverted;
};

struct Api {
    uint32_t struct_size;
    uint32_t abi_version;
    int32_t (*QueryCapabilities)(Capabilities* out);
    int32_t (*Create)(const CreateInfo* info, void** context);
    int32_t (*Destroy)(void* context);
    int32_t (*PrepareSession)(void* context);
    int32_t (*PrepareFrame)(void* context, const FrameInfo* info, Job* job);
    int32_t (*RecordInputs)(void* context, void* job, void* command_list);
    int32_t (*EnqueueHip)(void* context, void* job, void* command_queue);
    int32_t (*RecordOutputs)(void* context, void* job, void* command_list);
    int32_t (*ExecuteAfterProducer)(void* context, void* job, void* command_queue);
    int32_t (*CancelUnsubmitted)(void* context, void* job);
    int32_t (*Poll)(void* context, void* job, uint32_t* state);
    int32_t (*Retire)(void* context, void* job);
    int32_t (*ResetHistory)(void* context);
    int32_t (*Drain)(void* context);
    int32_t (*GetStatus)(void* context, char* buf, uint32_t buf_chars);
    int32_t (*GetLastError)(char* buf, uint32_t buf_chars);
    int32_t (*BeginRecordingExecution)(void* context, void* job, void* actual_queue);
    int32_t (*EndRecordingExecution)(void* context, void* job, void* actual_queue,
                                     uint32_t submitted_flags, void* fence, uint64_t value,
                                     int32_t signal_status);
    int32_t (*InvalidateRecording)(void* context, void* job);
    int32_t (*CollectRecording)(void* context, void* job);
    int32_t (*GetTimings)(void* context, void* out);
};

using FnGetApi = int32_t (*)(uint32_t, Api*);
using FnPrepareFrame = int32_t (*)(void*, const FrameInfo*, Job*);
using FnSetLogging = void (*)(uint32_t);

// sizeof(MochizukiNrControls) as shipped by this runtime. The struct is large
// and still growing, so this host passes it as an opaque, exactly-sized blob
// filled by MochizukiNrGetControlDefaults rather than mirroring every field.
// A mismatch is reported by the runtime, never silently accepted.
// sizeof(MochizukiNrFrameInfo) as the shipped DLL checks it: it compares
// frame->struct_size against 0x58 (88) and job->struct_size against 0x20 (32),
// then rejects anything else as "bad arguments". 88 bytes ends at `reset`, so
// the shipped runtime predates the depth fields the current header declares;
// this host keeps the wider struct locally and declares only what it sent.
constexpr uint32_t kFrameBytes = 88;

constexpr uint32_t kControlsBytes = 528;  // measured from the shipped DLL; the header now says 532

struct BuildProgress {
    uint32_t struct_size;
    uint32_t active;
    uint32_t completed;
    uint32_t total;
    uint64_t start_tick;
    uint64_t update_tick;
    char stage[96];
};

using FnGetBuildProgress = int32_t (*)(void* context, BuildProgress* out);
constexpr const char* kExportGetBuildProgress = "MochizukiNrGetBuildProgress";

using FnGetControlDefaults = int32_t (*)(void* controls);
using FnSetControls = int32_t (*)(void* context, const void* controls);

constexpr const char* kExportGetControlDefaults = "MochizukiNrGetControlDefaults";
constexpr const char* kExportSetControls = "MochizukiNrSetControls";

// Exported by the Mochizuki runtime beside the ABI table above.
constexpr const char* kExportGetApi = "MochizukiNrGetApi";
constexpr const char* kExportPrepareFrame = "MochizukiNrPrepareFrame";
constexpr const char* kExportSetLogging = "MochizukiNrSetLogging";
constexpr const char* kExportGetInfo = "MochizukiNrGetInfo";

static_assert(sizeof(void*) == 8, "the AMD NR ABI is x64 only");
static_assert(sizeof(Api) == 0xB0, "LmxxfNrApi must be 176 bytes; the runtime rejects anything else");

}  // namespace amdnr

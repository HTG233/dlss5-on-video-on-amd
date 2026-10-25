// Smoke test for the AMD neural-rendering backend.
//
// Two stages, both driven exactly the way the player would drive them:
//   1. Initialise: load the DLL, take the ABI table, query capabilities,
//      Create, PrepareSession.
//   2. Evaluate one frame: allocate the same four resources the player's NGX
//      contract uses (FP16 colour, R32 depth, R16G16 motion, FP16 output),
//      then PrepareFrame -> RecordInputs -> EnqueueHip -> RecordOutputs and
//      submit.
//
// It writes no result to disk; a successful run proves the per-frame path is
// reachable, not that the picture is right.
//
// Usage: AmdNrSmoke <assets-dir>

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <cwchar>

#include "AmdNrBackend.h"
#include "AmdNrAbi.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace {

constexpr uint32_t kWidth = 1280;
constexpr uint32_t kHeight = 720;

ID3D12Resource* MakeTexture(ID3D12Device* device, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = flags;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ID3D12Resource* resource = nullptr;
    const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                      D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                      IID_PPV_ARGS(&resource));
    if (FAILED(hr))
    {
        std::printf("CreateCommittedResource failed hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return nullptr;
    }
    return resource;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* assets = (argc > 1) ? argv[1] : L"amd-nr-assets";

    ID3D12Device* device = nullptr;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if (FAILED(hr))
    {
        std::printf("D3D12CreateDevice failed hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    {
        DXGI_ADAPTER_DESC desc{};
        IDXGIFactory4* factory = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        {
            IDXGIAdapter1* adapter = nullptr;
            if (SUCCEEDED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))))
            {
                adapter->GetDesc(&desc);
                std::wprintf(L"adapter: %s  vendor=0x%04X  vram=%llu MB\n", desc.Description, desc.VendorId,
                             static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024 * 1024)));
                adapter->Release();
            }
            factory->Release();
        }
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    hr = device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue));
    if (FAILED(hr)) { std::printf("CreateCommandQueue failed\n"); return 1; }

    AmdNrBackend backend;

    // Diagnostic: find the controls struct size the installed DLL accepts. The
    // shipped package can be older than the header this host was written against,
    // and a mismatch is only ever reported as "bad arguments".
    if (argc > 2 && std::wcscmp(argv[2], L"--probe") == 0)
    {
        for (uint32_t n = 320; n <= 900; n += 4)
        {
            AmdNrBackend probe;
            if (probe.Initialize(device, queue, assets, kWidth, kHeight, n))
            {
                std::printf("accepted controls size = %u\n", n);
                probe.Shutdown();
                queue->Release();
                device->Release();
                return 0;
            }
            probe.Shutdown();
        }
        std::printf("no controls size accepted in 320..900\n");
        queue->Release();
        device->Release();
        return 3;
    }

    if (argc > 2 && std::wcscmp(argv[2], L"--probe-frame") == 0)
    {
        if (!backend.Initialize(device, queue, assets, kWidth, kHeight))
        {
            std::printf("Initialize failed: %s\n", backend.LastError());
            return 2;
        }
        ID3D12Resource* c = MakeTexture(device, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_NONE);
        ID3D12CommandAllocator* a = nullptr; ID3D12GraphicsCommandList* l = nullptr;
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a));
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a, nullptr, IID_PPV_ARGS(&l));
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = c; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        l->ResourceBarrier(1, &b);
        for (uint32_t n = 80; n <= 160; n += 4)
        {
            backend.SetFrameStructSize(n);
            if (backend.Evaluate(l, queue, c, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, c, c,
                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, c,
                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false, 33.3f))
            {
                std::printf("accepted frame struct size = %u\n", n);
                return 0;
            }
            const char* e = backend.LastError();
            if (n == 80) std::printf("first error: %s\n", e);
        }
        std::printf("no frame struct size accepted (last: %s)\n", backend.LastError());
        return 3;
    }

    bool ok = backend.Initialize(device, queue, assets, kWidth, kHeight);
    std::printf("Initialize : %s\n", ok ? "OK" : "FAILED");
    if (!ok) std::printf("  %s\n", backend.LastError());

    if (ok)
    {
        // The player's neural contract, format for format.
        ID3D12Resource* color = MakeTexture(device, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                            D3D12_RESOURCE_FLAG_NONE);
        ID3D12Resource* depth = MakeTexture(device, DXGI_FORMAT_R32_FLOAT,
                                            D3D12_RESOURCE_FLAG_NONE);
        ID3D12Resource* motion = MakeTexture(device, DXGI_FORMAT_R16G16_FLOAT,
                                             D3D12_RESOURCE_FLAG_NONE);
        ID3D12Resource* output = MakeTexture(device, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                             D3D12_RESOURCE_FLAG_NONE);

        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* list = nullptr;
        ID3D12Fence* fence = nullptr;
        if (color && depth && motion && output &&
            SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
            SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list))) &&
            SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
        {
            auto transition = [&](ID3D12Resource* res, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
                if (from == to) return;
                D3D12_RESOURCE_BARRIER b{};
                b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource = res;
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b.Transition.StateBefore = from;
                b.Transition.StateAfter = to;
                list->ResourceBarrier(1, &b);
            };

            const auto srv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            transition(color, D3D12_RESOURCE_STATE_COMMON, srv);

            // The colour has to be readable before the runtime records its own
            // copies, so the transition is submitted and awaited first.
            if (SUCCEEDED(list->Close()))
            {
                ID3D12CommandList* lists[] = { list };
                queue->ExecuteCommandLists(1, lists);
                if (SUCCEEDED(queue->Signal(fence, 1)) && fence->GetCompletedValue() < 1)
                {
                    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                    fence->SetEventOnCompletion(1, event);
                    WaitForSingleObject(event, 60000);
                    CloseHandle(event);
                }
                std::printf("upload     : submitted\n");
            }

            ok = backend.EvaluateFrame(queue, color, srv, true);
            std::printf("Evaluate   : %s\n", ok ? "OK" : "FAILED");
            if (!ok) std::printf("  %s\n", backend.LastError());

            // Proof the network produced pixels rather than an accepted-but-empty
            // frame: copy the runtime's own output back and look at the values.
            if (ok && backend.LastOutput())
            {
                ID3D12Resource* out = backend.LastOutput();
                const D3D12_RESOURCE_DESC od = out->GetDesc();
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
                UINT64 total = 0;
                device->GetCopyableFootprints(&od, 0, 1, 0, &fp, nullptr, nullptr, &total);

                D3D12_HEAP_PROPERTIES rh{}; rh.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
                bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                ID3D12Resource* rb = nullptr;
                if (SUCCEEDED(device->CreateCommittedResource(&rh, D3D12_HEAP_FLAG_NONE, &bd,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb))))
                {
                    allocator->Reset();
                    list->Reset(allocator, nullptr);
                    D3D12_RESOURCE_BARRIER b2{};
                    b2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    b2.Transition.pResource = out;
                    b2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    b2.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                    b2.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                    list->ResourceBarrier(1, &b2);
                    D3D12_TEXTURE_COPY_LOCATION src{ out, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 };
                    D3D12_TEXTURE_COPY_LOCATION dst{ rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, fp };
                    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                    if (SUCCEEDED(list->Close()))
                    {
                        ID3D12CommandList* l2[] = { list };
                        queue->ExecuteCommandLists(1, l2);
                        queue->Signal(fence, 5);
                        if (fence->GetCompletedValue() < 5)
                        {
                            HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                            fence->SetEventOnCompletion(5, ev);
                            WaitForSingleObject(ev, 60000);
                            CloseHandle(ev);
                        }
                        void* mapped = nullptr;
                        if (SUCCEEDED(rb->Map(0, nullptr, &mapped)))
                        {
                            const unsigned char* p = static_cast<const unsigned char*>(mapped);
                            size_t nonZero = 0; double sum = 0;
                            const size_t n = static_cast<size_t>(total);
                            for (size_t i = 0; i < n; ++i) { if (p[i]) ++nonZero; sum += p[i]; }
                            std::printf("output     : %llu bytes, non-zero %zu (%.1f%%), mean %.2f\n",
                                        static_cast<unsigned long long>(n), nonZero,
                                        n ? 100.0 * static_cast<double>(nonZero) / static_cast<double>(n) : 0.0,
                                        n ? sum / static_cast<double>(n) : 0.0);
                            rb->Unmap(0, nullptr);
                        }
                    }
                    rb->Release();
                }
            }

            list->Release();
            allocator->Release();
            fence->Release();
        }
        else
        {
            std::printf("resource or command setup failed\n");
            ok = false;
        }

        if (color) color->Release();
        if (depth) depth->Release();
        if (motion) motion->Release();
        if (output) output->Release();
    }

    backend.Shutdown();
    queue->Release();
    device->Release();
    std::printf("result: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 2;
}

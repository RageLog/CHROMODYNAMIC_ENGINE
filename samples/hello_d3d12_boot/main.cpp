// =============================================================================
// CHROMODYNAMIC — samples/hello_d3d12_boot
//
// v0.27.0 boot-only milestone: the D3D12 backend can create an
// ID3D12Device + DXGI adapter + direct command queue. This sample
// proves the path is alive by:
//   1. calling cd::rhi_d3d12::create_d3d12_device()
//   2. printing the chosen adapter name + backend enum
//   3. invoking wait_idle() (exercises the fence + event signal path)
//   4. exiting 0
//
// No swapchain, no draw — those land in v0.27.x patches. If this
// sample exits 0, the D3D12 boot surface is sound.
// =============================================================================
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Version.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_d3d12/D3D12Device.hpp>

#include <cstdio>

int main()
{
    std::fprintf(stdout,
                 "CHROMODYNAMIC %u.%u.%u — hello_d3d12_boot\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));

    cd::rhi_d3d12::D3D12CreateInfo info {};
    info.app_name = "hello_d3d12_boot";
    info.prefer_discrete_gpu = true;
    // Validation off by default — debug runtime requires the Windows
    // SDK debug component to be installed; without it the DXGI debug
    // factory fails. Production samples don't need it; uncomment for
    // local development.
    info.enable_validation = false;

    auto dev_r = cd::rhi_d3d12::create_d3d12_device(info);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr,
                     "[d3d12] create_d3d12_device failed: %.*s\n",
                     static_cast<int>(dev_r.error().message.size()),
                     dev_r.error().message.data());
        return 1;
    }
    auto& device = **dev_r;

    const auto backend = device.backend();
    const auto adapter = device.adapter_name();
    std::fprintf(stdout,
                 "[d3d12] backend=%u  adapter=%.*s\n",
                 static_cast<unsigned>(backend),
                 static_cast<int>(adapter.size()),
                 adapter.data());

    // wait_idle exercises the fence + event path. On a fresh queue
    // the wait returns immediately because no work has been submitted,
    // but the signal/wait pair still proves the device-fence wiring
    // is sound.
    device.wait_idle();

    std::fprintf(stdout, "[d3d12] boot OK\n");
    return 0;
}

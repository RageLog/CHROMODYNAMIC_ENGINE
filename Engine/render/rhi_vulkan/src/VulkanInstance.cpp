// =============================================================================
// CHROMODYNAMIC — cd/rhi_vulkan/VulkanInstance.cpp
// =============================================================================
#include "VulkanInternal.hpp"
#include "VulkanMacros.hpp"

#include <cstdio>
#include <cstring>

namespace cd::rhi_vulkan
{

namespace
{

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/
)
{
    // Validation messages land on stderr so they surface in any console
    // host. Engines that want a richer transport (file log, in-game console)
    // can install their own messenger via the same `enable_validation`
    // path — this default keeps the diagnostic loop closed for samples and
    // unit tests without an extra wiring step.
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && data != nullptr && data->pMessage != nullptr)
    {
        const char* tag = (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "VK ERROR" : "VK WARN";
        std::fprintf(stderr, "[%s] %s\n", tag, data->pMessage);
        std::fflush(stderr);
    }
    return VK_FALSE;
}

bool has_validation_layer()
{
    std::uint32_t count { 0 };
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS)
        return false;
    for (const auto& l : layers)
    {
        if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            return true;
    }
    return false;
}

}  // namespace

VulkanInstance::~VulkanInstance()
{
    if (debug_messenger != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
    {
        if (auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")
            );
            fn != nullptr)
        {
            fn(instance, debug_messenger, nullptr);
        }
    }
    if (instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

cd::core::Result<void> create_instance(
    VulkanInstance& out,
    const std::string& app_name,
    std::uint32_t app_version,
    bool enable_validation,
    const std::vector<std::string>& extra_extensions
)
{
    if (!out.volk_initialized)
    {
        if (volkInitialize() != VK_SUCCESS)
        {
            return std::unexpected(
                cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kBackendInitFailed,
                    "volkInitialize failed; no Vulkan ICD installed"
                )
            );
        }
        out.volk_initialized = true;
    }

    // Designated initialisers silence -Wmissing-field-initializers under GCC.
    // pNext/pApplicationName etc. zero-init when omitted; this is the canonical
    // pattern for Vulkan structs.
    const VkApplicationInfo app {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = app_name.c_str(),
        .applicationVersion = app_version,
        .pEngineName = "CHROMODYNAMIC",
        .engineVersion = vkv::make_version(0, 1, 0),
        .apiVersion = vkv::kApiVersion12,
    };

    std::vector<const char*> exts;
    exts.reserve(extra_extensions.size() + 4);
    for (const auto& e : extra_extensions)
        exts.push_back(e.c_str());

    // Surface extensions: always enabled so consumers can create a swapchain
    // later. If no driver supports them, vkCreateInstance fails up front rather
    // than letting create_swapchain fail later in a confusing way.
    exts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(_WIN32)
    exts.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

    std::vector<const char*> layers;
    bool requested_validation = enable_validation && has_validation_layer();
    if (requested_validation)
    {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    out.validation_enabled = requested_validation;

    const VkInstanceCreateInfo ci {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app,
        .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<std::uint32_t>(exts.size()),
        .ppEnabledExtensionNames = exts.data(),
    };

    if (vkCreateInstance(&ci, nullptr, &out.instance) != VK_SUCCESS)
    {
        return std::unexpected(
            cd::rhi::rhi_errors::make(cd::rhi::rhi_errors::Code::kBackendInitFailed, "vkCreateInstance failed")
        );
    }
    volkLoadInstance(out.instance);

    if (requested_validation)
    {
        const VkDebugUtilsMessengerCreateInfoEXT dci {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debug_callback,
            .pUserData = nullptr,
        };
        if (auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(out.instance, "vkCreateDebugUtilsMessengerEXT")
            );
            fn != nullptr)
        {
            fn(out.instance, &dci, nullptr, &out.debug_messenger);
        }
    }
    return {};
}

}  // namespace cd::rhi_vulkan

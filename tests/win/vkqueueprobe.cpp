// SPDX-License-Identifier: MIT
// Load a specified ICD directly and compare WDDM counters during idle and GPU work.
// Usage: vkqueueprobe <absolute ICD DLL path> [load seconds, 1..120]
// Each submission fills 32 MiB sixteen times. Fences and a full readback check completion.
#include <windows.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define REQUIRE(call)                                                                                        \
    do {                                                                                                     \
        VkResult r = (call);                                                                                 \
        if (r != VK_SUCCESS) {                                                                               \
            std::printf("FAIL %s: %d\n", #call, r);                                                          \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)
int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2 || argc > 3) {
        std::printf("Usage: vkqueueprobe <absolute ICD DLL path> [load seconds, 1..120]\n");
        return 2;
    }
    char *end = nullptr;
    const unsigned long seconds = argc == 3 ? std::strtoul(argv[2], &end, 10) : 10;
    if (seconds < 1 || seconds > 120 || (end && (*end || end == argv[2]))) {
        std::printf("Load duration must be 1..120 seconds\n");
        return 2;
    }
    HMODULE module = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        std::printf("LoadLibrary failed %lu\n", GetLastError());
        return 1;
    }
    auto gipa =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(module, "vk_icdGetInstanceProcAddr"));
    auto negotiate = reinterpret_cast<VkResult(VKAPI_PTR *)(uint32_t *)>(
        GetProcAddress(module, "vk_icdNegotiateLoaderICDInterfaceVersion"));
    if (!gipa) {
        std::printf("ICD has no vk_icdGetInstanceProcAddr\n");
        return 1;
    }
    uint32_t version = 5;
    if (negotiate)
        REQUIRE(negotiate(&version));
    std::printf("pid %lu ICD %s interface %u\n", GetCurrentProcessId(), argv[1], version);
    auto vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "vkqueueprobe";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance;
    REQUIRE(vkCreateInstance(&ici, nullptr, &instance));
#define IFN(f) auto f = reinterpret_cast<PFN_##f>(gipa(instance, #f))
    IFN(vkEnumeratePhysicalDevices);
    IFN(vkGetPhysicalDeviceProperties);
    IFN(vkGetPhysicalDeviceMemoryProperties);
    IFN(vkGetPhysicalDeviceQueueFamilyProperties);
    IFN(vkCreateDevice);
    IFN(vkGetDeviceProcAddr);
    IFN(vkDestroyInstance);
    uint32_t count = 0;
    REQUIRE(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    std::vector<VkPhysicalDevice> gpus(count);
    REQUIRE(vkEnumeratePhysicalDevices(instance, &count, gpus.data()));
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    for (auto g : gpus) {
        vkGetPhysicalDeviceProperties(g, &props);
        if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
            gpu = g;
            break;
        }
    }
    if (!gpu) {
        std::printf("No hardware Vulkan device\n");
        return 1;
    }
    std::printf("gpu %s, %lu seconds\n", props.deviceName, seconds);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        family++;
    if (family == count) {
        std::printf("No graphics queue family\n");
        return 1;
    }
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    VkDevice device;
    REQUIRE(vkCreateDevice(gpu, &dci, nullptr, &device));
#define DFN(f) auto f = reinterpret_cast<PFN_##f>(vkGetDeviceProcAddr(device, #f))
    DFN(vkGetDeviceQueue);
    DFN(vkCreateBuffer);
    DFN(vkGetBufferMemoryRequirements);
    DFN(vkAllocateMemory);
    DFN(vkBindBufferMemory);
    DFN(vkMapMemory);
    DFN(vkUnmapMemory);
    DFN(vkCreateCommandPool);
    DFN(vkAllocateCommandBuffers);
    DFN(vkResetCommandBuffer);
    DFN(vkBeginCommandBuffer);
    DFN(vkEndCommandBuffer);
    DFN(vkCmdFillBuffer);
    DFN(vkCmdPipelineBarrier);
    DFN(vkCreateFence);
    DFN(vkResetFences);
    DFN(vkQueueSubmit);
    DFN(vkWaitForFences);
    DFN(vkDeviceWaitIdle);
    DFN(vkDestroyFence);
    DFN(vkDestroyCommandPool);
    DFN(vkDestroyBuffer);
    DFN(vkFreeMemory);
    DFN(vkDestroyDevice);
    DFN(vkCreateQueryPool);
    DFN(vkDestroyQueryPool);
    DFN(vkCmdResetQueryPool);
    DFN(vkCmdWriteTimestamp);
    DFN(vkGetQueryPoolResults);
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    constexpr VkDeviceSize size = 32u * 1024u * 1024u;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer;
    REQUIRE(vkCreateBuffer(device, &bci, nullptr, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(gpu, &mem);
    uint32_t mt = UINT32_MAX;
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((req.memoryTypeBits & (1u << i)) &&
            ((mem.memoryTypes[i].propertyFlags &
              (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            mt = i;
            break;
        }
    if (mt == UINT32_MAX) {
        std::printf("No coherent host-visible buffer memory\n");
        return 1;
    }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;
    VkDeviceMemory memory;
    REQUIRE(vkAllocateMemory(device, &mai, nullptr, &memory));
    REQUIRE(vkBindBufferMemory(device, buffer, memory, 0));
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = family;
    VkCommandPool pool;
    REQUIRE(vkCreateCommandPool(device, &pci, nullptr, &pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    REQUIRE(vkAllocateCommandBuffers(device, &cai, &cmd));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    REQUIRE(vkCreateFence(device, &fci, nullptr, &fence));
    const uint32_t timestamp_bits = families[family].timestampValidBits;
    VkQueryPool queries = VK_NULL_HANDLE;
    if (timestamp_bits) {
        VkQueryPoolCreateInfo query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_info.queryCount = 2;
        REQUIRE(vkCreateQueryPool(device, &query_info, nullptr, &queries));
    }
    std::printf("device ready, buffer %llu bytes, memory type %u flags 0x%x\n", (unsigned long long)size, mt,
                mem.memoryTypes[mt].propertyFlags);
    std::printf("idle before load 4 seconds\n");
    Sleep(4000);
    const auto start = GetTickCount64();
    auto last = start;
    uint32_t batches = 0, expected = 0;
    double gpu_ns = 0;
    do {
        REQUIRE(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        REQUIRE(vkBeginCommandBuffer(cmd, &begin));
        if (queries) {
            vkCmdResetQueryPool(cmd, queries, 0, 2);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries, 0);
        }
        for (unsigned fill = 0; fill < 16; fill++) {
            expected = 0x5a000000u + (batches * 16 + fill);
            vkCmdFillBuffer(cmd, buffer, 0, size, expected);
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier,
                                 0, nullptr, 0, nullptr);
        }
        if (queries)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries, 1);
        REQUIRE(vkEndCommandBuffer(cmd));
        REQUIRE(vkResetFences(device, 1, &fence));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        REQUIRE(vkQueueSubmit(queue, 1, &si, fence));
        REQUIRE(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
        if (queries) {
            uint64_t ticks[2];
            REQUIRE(vkGetQueryPoolResults(device, queries, 0, 2, sizeof(ticks), ticks, sizeof(uint64_t),
                                         VK_QUERY_RESULT_64_BIT));
            const uint64_t mask = timestamp_bits == 64 ? UINT64_MAX : (uint64_t(1) << timestamp_bits) - 1;
            gpu_ns += double((ticks[1] - ticks[0]) & mask) * props.limits.timestampPeriod;
        }
        batches++;
        const auto now = GetTickCount64();
        if (now - last >= 1000) {
            std::printf("completed %u batches at %llu ms\n", batches, (unsigned long long)(now - start));
            last = now;
        }
    } while (GetTickCount64() - start < seconds * 1000ull);
    const auto wall_ms = GetTickCount64() - start;
    if (queries)
        std::printf("GPU timestamp intervals %.3f ms, load wall time %llu ms, ratio %.2f percent\n",
                    gpu_ns / 1e6, (unsigned long long)wall_ms, gpu_ns / (double(wall_ms) * 1e4));
    else
        std::printf("GPU timestamps unavailable for this queue family\n");
    void *mapped;
    REQUIRE(vkMapMemory(device, memory, 0, size, 0, &mapped));
    const auto words = static_cast<const uint32_t *>(mapped);
    for (size_t i = 0; i < size / 4; i++)
        if (words[i] != expected) {
            std::printf("FAIL readback[%zu]=%08x expected %08x\n", i, words[i], expected);
            return 1;
        }
    vkUnmapMemory(device, memory);
    REQUIRE(vkDeviceWaitIdle(device));
    std::printf("PASS %u submissions, %llu MiB filled, all %llu words correct\n", batches,
                (unsigned long long)batches * 16 * 32, (unsigned long long)size / 4);
    std::printf("idle after load 4 seconds\n");
    Sleep(4000);
    vkDestroyFence(device, fence, nullptr);
    if (queries)
        vkDestroyQueryPool(device, queries, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    FreeLibrary(module);
    return 0;
}

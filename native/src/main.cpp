#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <fstream>

#include "scripting/PythonHost.h"
#include "scripting/EngineModule.h"

static const char* kAppName = "BSP Engine Host";
static bool g_framebufferResized = false;
static PythonHost* g_py = nullptr; // used by WndProc to forward events

// --------------------- logging ---------------------
static void logi(const char* msg) { std::printf("[INFO] %s\n", msg); }
static void loge(const char* msg) { std::printf("[ERR ] %s\n", msg); }

static std::string get_exe_dir() {
  char path[MAX_PATH] = {};
  DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return std::string(".");
  std::string s(path, path + len);
  size_t pos = s.find_last_of("\\/");
  if (pos == std::string::npos) return std::string(".");
  return s.substr(0, pos);
}

// --------------------- Win32 window ---------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_SIZE:
      g_framebufferResized = true;
      if (g_py) {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        g_py->call_event("resize", w, h);
      }
      return 0;
    case WM_DESTROY:
      if (g_py) g_py->call_event("quit", 0, 0);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}

static HWND create_window(HINSTANCE hInstance, int width, int height) {
  WNDCLASS wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = "BspEngineWindowClass";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

  if (!RegisterClass(&wc)) {
    loge("RegisterClass failed.");
    return nullptr;
  }

  DWORD style = WS_OVERLAPPEDWINDOW;
  RECT rect{0, 0, width, height};
  AdjustWindowRect(&rect, style, FALSE);

  HWND hwnd = CreateWindowEx(
    0, wc.lpszClassName, kAppName, style,
    CW_USEDEFAULT, CW_USEDEFAULT,
    rect.right - rect.left, rect.bottom - rect.top,
    nullptr, nullptr, hInstance, nullptr
  );

  if (!hwnd) {
    loge("CreateWindowEx failed.");
    return nullptr;
  }

  ShowWindow(hwnd, SW_SHOW);
  return hwnd;
}

// --------------------- Vulkan debug ---------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
  VkDebugUtilsMessageSeverityFlagBitsEXT severity,
  VkDebugUtilsMessageTypeFlagsEXT,
  const VkDebugUtilsMessengerCallbackDataEXT* cb,
  void*
) {
  const char* prefix =
    (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   ? "[VULKAN][ERR]" :
    (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "[VULKAN][WRN]" :
    "[VULKAN][INF]";
  std::printf("%s %s\n", prefix, cb->pMessage);
  return VK_FALSE;
}

static VkResult create_debug_messenger(
  VkInstance instance,
  const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
  VkDebugUtilsMessengerEXT* messenger
) {
  auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
  if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
  return fn(instance, createInfo, nullptr, messenger);
}

static void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
  auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
  if (fn) fn(instance, messenger, nullptr);
}

// --------------------- Vulkan helpers ---------------------
static bool has_layer(const char* name) {
  uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> props(count);
  vkEnumerateInstanceLayerProperties(&count, props.data());
  for (auto& p : props) {
    if (std::strcmp(p.layerName, name) == 0) return true;
  }
  return false;
}

static void vkcheck(VkResult r, const char* where) {
  if (r != VK_SUCCESS) {
    std::printf("[VKERR] %s failed (%d)\n", where, (int)r);
    std::exit(1);
  }
}

struct Queues {
  uint32_t graphicsIndex = UINT32_MAX;
  uint32_t presentIndex  = UINT32_MAX;
};

static std::vector<uint32_t> read_spv(const char* path) {
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    std::printf("[ERR ] Failed to open %s\n", path);
    std::exit(1);
  }
  size_t size = (size_t)file.tellg();
  if (size % 4 != 0) {
    std::printf("[ERR ] %s size not multiple of 4\n", path);
    std::exit(1);
  }
  std::vector<uint32_t> data(size / 4);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(data.data()), size);
  file.close();
  return data;
}

static VkShaderModule create_shader_module(VkDevice device, const char* path) {
  auto code = read_spv(path);
  VkShaderModuleCreateInfo smci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
  smci.codeSize = code.size() * sizeof(uint32_t);
  smci.pCode = code.data();

  VkShaderModule module = VK_NULL_HANDLE;
  vkcheck(vkCreateShaderModule(device, &smci, nullptr, &module), "vkCreateShaderModule");
  return module;
}

int main() {
  HINSTANCE hInstance = GetModuleHandle(nullptr);
  logi("Starting host...");

  HWND hwnd = create_window(hInstance, 1280, 720);
  if (!hwnd) return 1;
  logi("Window created.");

// ---- Host context for Python bindings ----
EngineContext engCtx{};
engCtx.hwnd = hwnd;
EngineModule::SetContext(&engCtx);


  // ---- Python (gameplay scripts) ----
  // We deliberately keep this minimal: the renderer stays in C++/Vulkan,
  // while gameplay logic can immediately live in Python.
  PythonHost py;
  g_py = &py;
  {
    PythonHostConfig cfg;
    // project layout: native/../python
    cfg.scriptsDir = get_exe_dir() + "\\..\\python";
    cfg.moduleName = "game";

    if (!py.init(cfg)) {
      std::printf("[PYERR] Failed to init Python: %s\n", py.last_error().c_str());
    } else {
      py.call_init();
      py.call_event("start", 0, 0);
    }
  }

  // ---- Instance ----
  bool enableValidation = has_layer("VK_LAYER_KHRONOS_validation");
  if (enableValidation) logi("Validation layer available -> enabling.");
  else logi("Validation layer not found -> running without validation.");

  std::vector<const char*> layers;
  if (enableValidation) layers.push_back("VK_LAYER_KHRONOS_validation");

  std::vector<const char*> exts;
  exts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
  exts.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
  if (enableValidation) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
  appInfo.pApplicationName = kAppName;
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName = "BSP-Engine";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion = VK_API_VERSION_1_2;

  VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  ici.pApplicationInfo = &appInfo;
  ici.enabledLayerCount = (uint32_t)layers.size();
  ici.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
  ici.enabledExtensionCount = (uint32_t)exts.size();
  ici.ppEnabledExtensionNames = exts.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCI{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
  if (enableValidation) {
    debugCI.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCI.messageType =
      VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCI.pfnUserCallback = debug_callback;
    ici.pNext = &debugCI;
  }

  VkInstance instance = VK_NULL_HANDLE;
  vkcheck(vkCreateInstance(&ici, nullptr, &instance), "vkCreateInstance");
  logi("Vulkan instance created.");

  VkDebugUtilsMessengerEXT dbg = VK_NULL_HANDLE;
  if (enableValidation) {
    if (create_debug_messenger(instance, &debugCI, &dbg) == VK_SUCCESS) {
      logi("Debug messenger created.");
    } else {
      loge("Debug messenger creation failed (continuing).");
    }
  }

  // ---- Surface ----
  VkWin32SurfaceCreateInfoKHR win32SurfaceCI{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
  win32SurfaceCI.hinstance = hInstance;
  win32SurfaceCI.hwnd = hwnd;

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  vkcheck(vkCreateWin32SurfaceKHR(instance, &win32SurfaceCI, nullptr, &surface),
          "vkCreateWin32SurfaceKHR");
  logi("Win32 surface created.");

  // ---- Physical device + queues ----
  uint32_t physCount = 0;
  vkcheck(vkEnumeratePhysicalDevices(instance, &physCount, nullptr),
          "vkEnumeratePhysicalDevices(count)");
  if (physCount == 0) {
    loge("No Vulkan devices found.");
    return 4;
  }

  std::vector<VkPhysicalDevice> physDevices(physCount);
  vkcheck(vkEnumeratePhysicalDevices(instance, &physCount, physDevices.data()),
          "vkEnumeratePhysicalDevices(list)");

  VkPhysicalDevice physical = physDevices[0];

  VkPhysicalDeviceProperties gpuProps{};
  vkGetPhysicalDeviceProperties(physical, &gpuProps);
  std::printf("Using GPU: %s\n", gpuProps.deviceName);

  uint32_t qCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, nullptr);
  std::vector<VkQueueFamilyProperties> qProps(qCount);
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, qProps.data());

  Queues queues{};
  for (uint32_t i = 0; i < qCount; ++i) {
    if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queues.graphicsIndex = i;
    }
    VkBool32 present = VK_FALSE;
    vkcheck(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present),
            "vkGetPhysicalDeviceSurfaceSupportKHR");
    if (present) queues.presentIndex = i;
  }

  if (queues.graphicsIndex == UINT32_MAX || queues.presentIndex == UINT32_MAX) {
    loge("Required queue families not found.");
    return 5;
  }

  // ---- Device ----
  float priority = 1.0f;

  std::vector<uint32_t> uniqueQueues = { queues.graphicsIndex };
  if (queues.presentIndex != queues.graphicsIndex)
    uniqueQueues.push_back(queues.presentIndex);

  std::vector<VkDeviceQueueCreateInfo> queueInfos;
  for (uint32_t idx : uniqueQueues) {
    VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = idx;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    queueInfos.push_back(qci);
  }

  const char* deviceExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

  VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
  dci.queueCreateInfoCount = (uint32_t)queueInfos.size();
  dci.pQueueCreateInfos = queueInfos.data();
  dci.enabledExtensionCount = 1;
  dci.ppEnabledExtensionNames = deviceExts;

  VkDevice device = VK_NULL_HANDLE;
  vkcheck(vkCreateDevice(physical, &dci, nullptr, &device), "vkCreateDevice");
  logi("Logical device created.");

  VkQueue graphicsQueue = VK_NULL_HANDLE;
  VkQueue presentQueue  = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, queues.graphicsIndex, 0, &graphicsQueue);
  vkGetDeviceQueue(device, queues.presentIndex, 0, &presentQueue);

  // ---- RenderPass/Pipeline (created once we know swapchain format) ----
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;

  // ---- Swapchain dependent resources ----
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkExtent2D extent{};
  VkSurfaceFormatKHR surfaceFormat{};
  VkFormat currentFormat = VK_FORMAT_UNDEFINED;

  uint32_t scImgCount = 0;
  std::vector<VkImageView> swapViews;
  std::vector<VkFramebuffer> framebuffers;

  VkCommandPool cmdPool = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> cmdBufs;

  // ---- Sync ----
  const uint32_t MAX_FRAMES = 2;
  std::vector<VkSemaphore> imageAvailable(MAX_FRAMES);
  std::vector<VkFence> inFlight(MAX_FRAMES);
  std::vector<VkSemaphore> renderFinished; // per swapchain image

  {
    VkSemaphoreCreateInfo semCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES; ++i) {
      vkcheck(vkCreateSemaphore(device, &semCI, nullptr, &imageAvailable[i]),
              "vkCreateSemaphore(imageAvailable)");
      vkcheck(vkCreateFence(device, &fenceCI, nullptr, &inFlight[i]),
              "vkCreateFence(inFlight)");
    }
  }

  auto destroy_pipeline = [&]() {
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
      pipelineLayout = VK_NULL_HANDLE;
    }
  };

  auto destroy_renderpass = [&]() {
    if (renderPass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device, renderPass, nullptr);
      renderPass = VK_NULL_HANDLE;
    }
  };

  auto create_renderpass = [&](VkFormat fmt) {
    VkAttachmentDescription color{};
    color.format = fmt;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    vkcheck(vkCreateRenderPass(device, &rpci, nullptr, &renderPass), "vkCreateRenderPass");
  };

  auto create_pipeline = [&]() {
    VkShaderModule vert = create_shader_module(device, "shaders/triangle.vert.spv");
    VkShaderModule frag = create_shader_module(device, "shaders/triangle.frag.spv");

    VkPipelineShaderStageCreateInfo vs{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vs.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vs.module = vert;
    vs.pName = "main";

    VkPipelineShaderStageCreateInfo fs{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fs.module = frag;
    fs.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vs, fs };

    VkPipelineVertexInputStateCreateInfo vis{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo ias{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vps{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1;
    vps.pViewports = nullptr;
    vps.scissorCount = 1;
    vps.pScissors = nullptr;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cbs{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cbs.attachmentCount = 1;
    cbs.pAttachments = &cba;

    VkDynamicState dynStates[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dynStates;

    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    vkcheck(vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout),
            "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo gpci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vis;
    gpci.pInputAssemblyState = &ias;
    gpci.pViewportState = &vps;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cbs;
    gpci.pDynamicState = &ds;
    gpci.layout = pipelineLayout;
    gpci.renderPass = renderPass;
    gpci.subpass = 0;

    vkcheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline),
            "vkCreateGraphicsPipelines");

    vkDestroyShaderModule(device, frag, nullptr);
    vkDestroyShaderModule(device, vert, nullptr);
  };

  auto cleanup_swapchain_deps = [&]() {
    if (cmdPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, cmdPool, nullptr);
      cmdPool = VK_NULL_HANDLE;
    }
    cmdBufs.clear();

    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers.clear();

    for (auto v : swapViews) vkDestroyImageView(device, v, nullptr);
    swapViews.clear();

    if (!renderFinished.empty()) {
      for (auto s : renderFinished) vkDestroySemaphore(device, s, nullptr);
      renderFinished.clear();
    }

    if (swapchain != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(device, swapchain, nullptr);
      swapchain = VK_NULL_HANDLE;
    }

    scImgCount = 0;
  };

  auto create_swapchain_deps = [&]() -> bool {
    VkSurfaceCapabilitiesKHR caps{};
    vkcheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
      return false; // minimized
    }

    uint32_t fmtCount = 0;
    vkcheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtCount, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkcheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtCount, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(list)");

    uint32_t pmCount = 0;
    vkcheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &pmCount, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    std::vector<VkPresentModeKHR> presentModes(pmCount);
    vkcheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &pmCount, presentModes.data()),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(list)");

    surfaceFormat = formats[0];
    for (auto& f : formats) {
      if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
          f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        surfaceFormat = f;
        break;
      }
    }

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto pm : presentModes) {
      if (pm == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = pm; break; }
    }

    extent = caps.currentExtent;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swapchainCI{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swapchainCI.surface = surface;
    swapchainCI.minImageCount = imageCount;
    swapchainCI.imageFormat = surfaceFormat.format;
    swapchainCI.imageColorSpace = surfaceFormat.colorSpace;
    swapchainCI.imageExtent = extent;
    swapchainCI.imageArrayLayers = 1;
    swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t qIndices[] = { queues.graphicsIndex, queues.presentIndex };
    if (queues.graphicsIndex != queues.presentIndex) {
      swapchainCI.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      swapchainCI.queueFamilyIndexCount = 2;
      swapchainCI.pQueueFamilyIndices = qIndices;
    } else {
      swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    swapchainCI.preTransform = caps.currentTransform;
    swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCI.presentMode = presentMode;
    swapchainCI.clipped = VK_TRUE;

    vkcheck(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain),
            "vkCreateSwapchainKHR");

    uint32_t imgCount = 0;
    vkcheck(vkGetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr),
            "vkGetSwapchainImagesKHR(count)");
    scImgCount = imgCount;

    std::vector<VkImage> swapImages(scImgCount);
    vkcheck(vkGetSwapchainImagesKHR(device, swapchain, &imgCount, swapImages.data()),
            "vkGetSwapchainImagesKHR(list)");

    // semaphores per swapchain image
    renderFinished.resize(scImgCount);
    {
      VkSemaphoreCreateInfo semCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
      for (uint32_t i = 0; i < scImgCount; ++i) {
        vkcheck(vkCreateSemaphore(device, &semCI, nullptr, &renderFinished[i]),
                "vkCreateSemaphore(renderFinished)");
      }
    }

    if (currentFormat != surfaceFormat.format) {
      vkDeviceWaitIdle(device);

      destroy_pipeline();
      destroy_renderpass();

      create_renderpass(surfaceFormat.format);
      create_pipeline();

      currentFormat = surfaceFormat.format;
      logi("RenderPass + Pipeline created/recreated for new format.");
    }

    swapViews.resize(scImgCount);
    for (uint32_t i = 0; i < scImgCount; ++i) {
      VkImageViewCreateInfo ivci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
      ivci.image = swapImages[i];
      ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
      ivci.format = surfaceFormat.format;
      ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      ivci.subresourceRange.levelCount = 1;
      ivci.subresourceRange.layerCount = 1;
      vkcheck(vkCreateImageView(device, &ivci, nullptr, &swapViews[i]),
              "vkCreateImageView");
    }

    framebuffers.resize(scImgCount);
    for (uint32_t i = 0; i < scImgCount; ++i) {
      VkImageView attachments[] = { swapViews[i] };
      VkFramebufferCreateInfo fbci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
      fbci.renderPass = renderPass;
      fbci.attachmentCount = 1;
      fbci.pAttachments = attachments;
      fbci.width = extent.width;
      fbci.height = extent.height;
      fbci.layers = 1;
      vkcheck(vkCreateFramebuffer(device, &fbci, nullptr, &framebuffers[i]),
              "vkCreateFramebuffer");
    }

    VkCommandPoolCreateInfo cpci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.queueFamilyIndex = queues.graphicsIndex;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkcheck(vkCreateCommandPool(device, &cpci, nullptr, &cmdPool), "vkCreateCommandPool");

    cmdBufs.resize(scImgCount);
    VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = scImgCount;
    vkcheck(vkAllocateCommandBuffers(device, &cbai, cmdBufs.data()), "vkAllocateCommandBuffers");

    logi("Swapchain deps created.");
    return true;
  };

  while (!create_swapchain_deps()) Sleep(16);

  auto record = [&](uint32_t imageIndex) {
    VkCommandBuffer cmd = cmdBufs[imageIndex];

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkcheck(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");

    VkClearValue clear{};
    clear.color.float32[0] = 0.03f;
    clear.color.float32[1] = 0.02f;
    clear.color.float32[2] = 0.05f;
    clear.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo rpbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpbi.renderPass = renderPass;
    rpbi.framebuffer = framebuffers[imageIndex];
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = extent;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.x = 0.0f; vp.y = 0.0f;
    vp.width  = (float)extent.width;
    vp.height = (float)extent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = extent;

    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkcheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
  };

  uint32_t frameIndex = 0;
  MSG msg{};
  bool running = true;

  LARGE_INTEGER freq{};
  LARGE_INTEGER last{};
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&last);

  while (running) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float dt = (float)((double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart);
    last = now;
    if (py.ok()) py.call_update(dt);

    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) running = false;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    if (!running) break;

    vkcheck(vkWaitForFences(device, 1, &inFlight[frameIndex], VK_TRUE, UINT64_MAX),
            "vkWaitForFences");
    vkcheck(vkResetFences(device, 1, &inFlight[frameIndex]), "vkResetFences");

    uint32_t imageIndex = 0;
    VkResult ar = vkAcquireNextImageKHR(
      device, swapchain, UINT64_MAX,
      imageAvailable[frameIndex], VK_NULL_HANDLE, &imageIndex
    );

    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
      vkDeviceWaitIdle(device);
      cleanup_swapchain_deps();
      while (!create_swapchain_deps()) Sleep(16);
      g_framebufferResized = false;
      continue;
    }
    vkcheck(ar, "vkAcquireNextImageKHR");

    vkcheck(vkResetCommandBuffer(cmdBufs[imageIndex], 0), "vkResetCommandBuffer");
    record(imageIndex);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &imageAvailable[frameIndex];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmdBufs[imageIndex];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderFinished[imageIndex];

    vkcheck(vkQueueSubmit(graphicsQueue, 1, &si, inFlight[frameIndex]), "vkQueueSubmit");

    VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished[imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &imageIndex;

    VkResult pr = vkQueuePresentKHR(presentQueue, &pi);

    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR || g_framebufferResized) {
      vkDeviceWaitIdle(device);
      cleanup_swapchain_deps();
      while (!create_swapchain_deps()) Sleep(16);
      g_framebufferResized = false;
    } else if (pr != VK_SUCCESS) {
      vkcheck(pr, "vkQueuePresentKHR");
    }

    frameIndex = (frameIndex + 1) % MAX_FRAMES;
  }

  // ---- Cleanup ----
  vkDeviceWaitIdle(device);

  cleanup_swapchain_deps();
  destroy_pipeline();
  destroy_renderpass();

  for (auto f : inFlight) vkDestroyFence(device, f, nullptr);
  for (auto s : imageAvailable) vkDestroySemaphore(device, s, nullptr);

  vkDestroyDevice(device, nullptr);

  vkDestroySurfaceKHR(instance, surface, nullptr);
  if (dbg) destroy_debug_messenger(instance, dbg);
  vkDestroyInstance(instance, nullptr);

  if (py.ok()) {
    py.call_event("shutdown", 0, 0);
  }
  g_py = nullptr;

  logi("Shutdown clean.");
  return 0;
}

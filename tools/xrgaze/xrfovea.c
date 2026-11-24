// Foveated-rendering test client: XR_VARJO_quad_views + XR_VARJO_foveated_rendering.
// Every view is filled with a checkerboard of CELL pixels; the inset views (2, 3) are tinted green. Higher pixel
// density = smaller squares, so the gaze-steered inset shows up as a patch of fine green squares that follows the eyes.
// Usage: xrfovea [seconds=600] [nofovea]   ("nofovea": quad views without gaze steering, fixed centre inset)
#define XR_USE_TIMESPEC 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static XrInstance inst;
#define CK(x) do { XrResult r_ = (x); if (XR_FAILED(r_)) { char b[64]; xrResultToString(inst, r_, b); fprintf(stderr, "%s failed: %s (line %d)\n", #x, b, __LINE__); exit(1); } } while (0)
#define VK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s failed: %d (line %d)\n", #x, r_, __LINE__); exit(1); } } while (0)
#define PROC(name) PFN_##name name; CK(xrGetInstanceProcAddr(inst, #name, (PFN_xrVoidFunction *)&name))
#define NV 4
#define CELL 16
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
  double secs = argc > 1 ? atof(argv[1]) : 600; int fovea = !(argc > 2 && !strcmp(argv[2], "nofovea"));
  const char *exts[] = {"XR_KHR_vulkan_enable2", "XR_VARJO_quad_views", "XR_VARJO_foveated_rendering"};
  XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO}; strcpy(ici.applicationInfo.applicationName, "xrfovea");
  ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION; ici.enabledExtensionCount = 3; ici.enabledExtensionNames = exts;
  CK(xrCreateInstance(&ici, &inst));
  XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO, 0, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY}; XrSystemId sys; CK(xrGetSystem(inst, &sgi, &sys));
  XrSystemFoveatedRenderingPropertiesVARJO fp = {XR_TYPE_SYSTEM_FOVEATED_RENDERING_PROPERTIES_VARJO};
  XrSystemProperties sp = {XR_TYPE_SYSTEM_PROPERTIES, &fp}; CK(xrGetSystemProperties(inst, sys, &sp));
  printf("system: %s, foveated rendering supported: %d\n", sp.systemName, fp.supportsFoveatedRendering);
  if (!fp.supportsFoveatedRendering) fovea = 0;

  PROC(xrGetVulkanGraphicsRequirements2KHR); PROC(xrCreateVulkanInstanceKHR); PROC(xrGetVulkanGraphicsDevice2KHR); PROC(xrCreateVulkanDeviceKHR);
  XrGraphicsRequirementsVulkan2KHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR}; CK(xrGetVulkanGraphicsRequirements2KHR(inst, sys, &req));
  VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "xrfovea", 1, "none", 1, VK_API_VERSION_1_1};
  VkInstanceCreateInfo vici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &app};
  XrVulkanInstanceCreateInfoKHR xvi = {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR, 0, sys, 0, vkGetInstanceProcAddr, &vici};
  VkInstance vki; VkResult vr; CK(xrCreateVulkanInstanceKHR(inst, &xvi, &vki, &vr)); VK(vr);
  XrVulkanGraphicsDeviceGetInfoKHR gdi = {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR, 0, sys, vki}; VkPhysicalDevice pd; CK(xrGetVulkanGraphicsDevice2KHR(inst, &gdi, &pd));
  uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, NULL); VkQueueFamilyProperties qf[16]; if (nq > 16) nq = 16;
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qf); uint32_t qfi = 0; while (qfi < nq && !(qf[qfi].queueFlags & VK_QUEUE_GRAPHICS_BIT)) qfi++;
  float prio = 1; VkDeviceQueueCreateInfo dq = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 0, 0, qfi, 1, &prio};
  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, 0, 0, 1, &dq};
  XrVulkanDeviceCreateInfoKHR xvd = {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR, 0, sys, 0, vkGetInstanceProcAddr, pd, &dci};
  VkDevice dev; CK(xrCreateVulkanDeviceKHR(inst, &xvd, &dev, &vr)); VK(vr);
  VkQueue q; vkGetDeviceQueue(dev, qfi, 0, &q);
  VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 0, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, qfi};
  VkCommandPool pool; VK(vkCreateCommandPool(dev, &cpi, 0, &pool));
  VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 0, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
  VkCommandBuffer cb; VK(vkAllocateCommandBuffers(dev, &cai, &cb));
  VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; VK(vkCreateFence(dev, &fci, 0, &fence));

  XrGraphicsBindingVulkan2KHR gb = {XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR, 0, vki, pd, dev, qfi, 0};
  XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO, &gb, 0, sys}; XrSession ses; CK(xrCreateSession(inst, &sci, &ses));

  // View sizes (foveated sizes when gaze steering is on: the inset covers less angle at the same pixel count).
  const XrViewConfigurationType vct = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO;
  XrViewConfigurationView vcv[NV]; XrFoveatedViewConfigurationViewVARJO fvv[NV]; uint32_t nv = 0;
  for (int i = 0; i < NV; i++) {
    fvv[i] = (XrFoveatedViewConfigurationViewVARJO){XR_TYPE_FOVEATED_VIEW_CONFIGURATION_VIEW_VARJO, 0, fovea};
    vcv[i] = (XrViewConfigurationView){XR_TYPE_VIEW_CONFIGURATION_VIEW, fp.supportsFoveatedRendering ? &fvv[i] : NULL};
  }
  CK(xrEnumerateViewConfigurationViews(inst, sys, vct, NV, &nv, vcv));
  for (uint32_t i = 0; i < nv; i++) printf("view %u: %ux%u\n", i, vcv[i].recommendedImageRectWidth, vcv[i].recommendedImageRectHeight);

  uint32_t nf = 0; CK(xrEnumerateSwapchainFormats(ses, 0, &nf, NULL)); int64_t fmts[64]; if (nf > 64) nf = 64; CK(xrEnumerateSwapchainFormats(ses, nf, &nf, fmts));
  int64_t fmt = 0; for (uint32_t i = 0; i < nf && !fmt; i++) if (fmts[i] == VK_FORMAT_R8G8B8A8_SRGB || fmts[i] == VK_FORMAT_B8G8R8A8_SRGB) fmt = fmts[i];
  if (!fmt) { fprintf(stderr, "no 8-bit sRGB swapchain format\n"); return 1; }
  int bgr = fmt == VK_FORMAT_B8G8R8A8_SRGB;

  // Per view: swapchain + a host buffer holding its checkerboard, copied into the image every frame.
  XrSwapchain sc[NV]; XrSwapchainImageVulkan2KHR imgs[NV][8]; VkBuffer buf[NV]; VkDeviceMemory mem[NV];
  VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  for (uint32_t v = 0; v < nv; v++) {
    uint32_t w = vcv[v].recommendedImageRectWidth, h = vcv[v].recommendedImageRectHeight;
    XrSwapchainCreateInfo swci = {XR_TYPE_SWAPCHAIN_CREATE_INFO, 0, 0, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT, fmt, 1, w, h, 1, 1, 1};
    CK(xrCreateSwapchain(ses, &swci, &sc[v]));
    uint32_t ni = 0; CK(xrEnumerateSwapchainImages(sc[v], 0, &ni, NULL)); if (ni > 8) ni = 8;
    for (uint32_t i = 0; i < ni; i++) imgs[v][i] = (XrSwapchainImageVulkan2KHR){XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR};
    CK(xrEnumerateSwapchainImages(sc[v], ni, &ni, (XrSwapchainImageBaseHeader *)imgs[v]));
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0, (VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    VK(vkCreateBuffer(dev, &bci, 0, &buf[v])); VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, buf[v], &mr);
    uint32_t mt = 0; while (mt < mp.memoryTypeCount && !((mr.memoryTypeBits >> mt & 1) && (mp.memoryTypes[mt].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (mp.memoryTypes[mt].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) mt++;
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, mr.size, mt}; VK(vkAllocateMemory(dev, &mai, 0, &mem[v])); VK(vkBindBufferMemory(dev, buf[v], mem[v], 0));
    unsigned char *px; VK(vkMapMemory(dev, mem[v], 0, VK_WHOLE_SIZE, 0, (void **)&px));
    int inset = v >= 2;
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
      int on = ((x / CELL) + (y / CELL)) & 1; unsigned char *o = px + ((size_t)y * w + x) * 4;
      unsigned char r = on ? (inset ? 60 : 200) : 20, g = on ? 200 : 20, b = on ? (inset ? 60 : 200) : 20;
      if (x < 4 || y < 4 || x >= w - 4 || y >= h - 4) { r = 255; g = inset ? 255 : 60; b = 0; } // view border: orange (outer) / yellow (inset)
      o[0] = bgr ? b : r; o[1] = g; o[2] = bgr ? r : b; o[3] = 255;
    }
    vkUnmapMemory(dev, mem[v]);
  }
  XrReferenceSpaceCreateInfo rsi = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, 0, XR_REFERENCE_SPACE_TYPE_LOCAL, {{0,0,0,1},{0,0,0}}}; XrSpace local; CK(xrCreateReferenceSpace(ses, &rsi, &local));

  int running = 0; double t0 = 0; long frames = 0, foveated = 0;
  for (;;) {
    XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(inst, &ev) == XR_SUCCESS) {
      if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        XrSessionState s = ((XrEventDataSessionStateChanged *)&ev)->state;
        if (s == XR_SESSION_STATE_READY) { XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO, 0, vct}; CK(xrBeginSession(ses, &bi)); running = 1; t0 = now_s(); }
        if (s == XR_SESSION_STATE_STOPPING || s == XR_SESSION_STATE_EXITING || s == XR_SESSION_STATE_LOSS_PENDING) goto done;
      }
      ev.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
    if (!running) { usleep(20000); continue; }
    if (now_s() - t0 > secs) break;
    XrFrameState fs = {XR_TYPE_FRAME_STATE}; CK(xrWaitFrame(ses, 0, &fs)); CK(xrBeginFrame(ses, 0));
    XrViewLocateFoveatedRenderingVARJO vlf = {XR_TYPE_VIEW_LOCATE_FOVEATED_RENDERING_VARJO, 0, fovea};
    XrViewLocateInfo vli = {XR_TYPE_VIEW_LOCATE_INFO, fp.supportsFoveatedRendering ? &vlf : NULL, vct, fs.predictedDisplayTime, local};
    XrViewState vs = {XR_TYPE_VIEW_STATE}; XrView views[NV]; for (int i = 0; i < NV; i++) views[i] = (XrView){XR_TYPE_VIEW};
    uint32_t got = 0; CK(xrLocateViews(ses, &vli, &vs, NV, &got, views));
    XrCompositionLayerProjectionView pv[NV];
    for (uint32_t v = 0; v < nv; v++) {
      uint32_t w = vcv[v].recommendedImageRectWidth, h = vcv[v].recommendedImageRectHeight, ii;
      CK(xrAcquireSwapchainImage(sc[v], 0, &ii)); XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, 0, XR_INFINITE_DURATION}; CK(xrWaitSwapchainImage(sc[v], &wi));
      VkCommandBufferBeginInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
      VK(vkResetCommandBuffer(cb, 0)); VK(vkBeginCommandBuffer(cb, &cbi));
      VkImageSubresourceRange rng = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; VkImage im = imgs[v][ii].image;
      VkImageMemoryBarrier b1 = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, 0, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, im, rng};
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &b1);
      VkBufferImageCopy bic = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {w, h, 1}};
      vkCmdCopyBufferToImage(cb, buf[v], im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);
      VkImageMemoryBarrier b2 = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, im, rng};
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, 0, 0, 0, 1, &b2);
      VK(vkEndCommandBuffer(cb));
      VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, 0, 0, 0, 0, 1, &cb}; VK(vkQueueSubmit(q, 1, &si, fence));
      VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX)); VK(vkResetFences(dev, 1, &fence));
      XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO}; CK(xrReleaseSwapchainImage(sc[v], &ri));
      pv[v] = (XrCompositionLayerProjectionView){XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW, 0, views[v].pose, views[v].fov, {sc[v], {{0, 0}, {(int32_t)w, (int32_t)h}}, 0}};
    }
    XrCompositionLayerProjection proj = {XR_TYPE_COMPOSITION_LAYER_PROJECTION, 0, 0, local, nv, pv};
    const XrCompositionLayerBaseHeader *layers[] = {(XrCompositionLayerBaseHeader *)&proj};
    XrFrameEndInfo fei = {XR_TYPE_FRAME_END_INFO, 0, fs.predictedDisplayTime, XR_ENVIRONMENT_BLEND_MODE_OPAQUE, 1, layers};
    CK(xrEndFrame(ses, &fei)); frames++;
    if (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) foveated++;
    if (frames % 90 == 1) {
      XrFovf f = views[2].fov;
      printf("inset L fov: left %+.1f right %+.1f up %+.1f down %+.1f deg\n", f.angleLeft * 57.2958f, f.angleRight * 57.2958f, f.angleUp * 57.2958f, f.angleDown * 57.2958f);
      fflush(stdout);
    }
  }
done:
  printf("frames %ld (gaze steering %s)\n", frames, fovea ? "on" : "off");
  return 0;
}

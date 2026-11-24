// Eye-tracking calibration client: shows a head-locked white dot (quad layer in VIEW space, black background)
// at known angles in the headset and logs the measured gaze for each target.
// Usage: xrcalib <out.csv> [start_delay_s=8] [hold_s=2.5]
//        xrcalib --live [seconds=600]   gaze viewer: grey reference dots at the 15 target positions, white dot = gaze
// CSV: t,target,target_yaw_deg,target_pitch_deg,gaze_x,gaze_y,gaze_z   (VIEW space, -z forward, +x right, +y up;
// rows only after the first second of each target, so the eyes have settled)
#define XR_USE_TIMESPEC 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static XrInstance inst;
#define CK(x) do { XrResult r_ = (x); if (XR_FAILED(r_)) { char b[64]; xrResultToString(inst, r_, b); fprintf(stderr, "%s failed: %s (line %d)\n", #x, b, __LINE__); exit(1); } } while (0)
#define VK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s failed: %d (line %d)\n", #x, r_, __LINE__); exit(1); } } while (0)
#define PROC(name) PFN_##name name; CK(xrGetInstanceProcAddr(inst, #name, (PFN_xrVoidFunction *)&name))

static const float YAWS[] = {-25, -12, 0, 12, 25}, PITCHES[] = {15, 0, -15};
#define NT 15
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static XrQuaternionf qmul(XrQuaternionf a, XrQuaternionf b) {
  return (XrQuaternionf){a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y, a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
                         a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w, a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
}

static VkDevice g_dev; static VkCommandBuffer g_cb; static VkQueue g_q; static VkFence g_fence;
static int fill(XrSwapchain sc, XrSwapchainImageVulkan2KHR *imgs, float c) {
  uint32_t ii; CK(xrAcquireSwapchainImage(sc, 0, &ii)); XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, 0, XR_INFINITE_DURATION}; CK(xrWaitSwapchainImage(sc, &wi));
  VkCommandBufferBeginInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  VkCommandBuffer cb = g_cb; VK(vkResetCommandBuffer(cb, 0)); VK(vkBeginCommandBuffer(cb, &cbi));
  VkImageSubresourceRange rng = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageMemoryBarrier b1 = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, 0, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, imgs[ii].image, rng};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &b1);
  VkClearColorValue col = {{c, c, c, 1}}; vkCmdClearColorImage(cb, imgs[ii].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &rng);
  VkImageMemoryBarrier b2 = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, imgs[ii].image, rng};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, 0, 0, 0, 1, &b2);
  VK(vkEndCommandBuffer(cb));
  VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, 0, 0, 0, 0, 1, &cb}; VK(vkQueueSubmit(g_q, 1, &si, g_fence));
  VK(vkWaitForFences(g_dev, 1, &g_fence, VK_TRUE, UINT64_MAX)); VK(vkResetFences(g_dev, 1, &g_fence));
  XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO}; CK(xrReleaseSwapchainImage(sc, &ri));
  return 0;
}
// Head-locked quad at 1.5 m in direction (yaw, pitch) degrees, facing the eye, `deg` wide.
static XrCompositionLayerQuad quad_at(XrSpace space, XrSwapchain sc, float yaw, float pitch, float deg) {
  const float d = 1.5f, size = 2 * d * tanf(deg / 2 * (float)M_PI / 180);
  float yr = yaw * (float)M_PI / 180, pr = pitch * (float)M_PI / 180;
  XrVector3f pos = {d * sinf(yr) * cosf(pr), d * sinf(pr), -d * cosf(yr) * cosf(pr)};
  XrQuaternionf qy = {0, sinf(-yr / 2), 0, cosf(-yr / 2)}, qx = {sinf(pr / 2), 0, 0, cosf(pr / 2)};
  return (XrCompositionLayerQuad){XR_TYPE_COMPOSITION_LAYER_QUAD, 0, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT, space, XR_EYE_VISIBILITY_BOTH,
                                  {sc, {{0, 0}, {32, 32}}, 0}, {qmul(qy, qx), pos}, {size, size}};
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: xrcalib <out.csv> [start_delay_s] [hold_s] | xrcalib --live [seconds]\n"); return 2; }
  int live = !strcmp(argv[1], "--live"); double live_s = live && argc > 2 ? atof(argv[2]) : 600;
  int completed = 0; // all targets shown (calibrate mode)
  FILE *out = live ? NULL : fopen(argv[1], "w"); if (!live && !out) { perror(argv[1]); return 1; }
  double delay = !live && argc > 2 ? atof(argv[2]) : 8, hold = !live && argc > 3 ? atof(argv[3]) : 2.5, settle = 1.0;
  if (out) fprintf(out, "t,target,target_yaw_deg,target_pitch_deg,gaze_x,gaze_y,gaze_z\n");

  const char *exts[] = {"XR_KHR_vulkan_enable2", "XR_EXT_eye_gaze_interaction", "XR_KHR_convert_timespec_time"};
  XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO}; strcpy(ici.applicationInfo.applicationName, "xrcalib");
  ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION; ici.enabledExtensionCount = 3; ici.enabledExtensionNames = exts;
  CK(xrCreateInstance(&ici, &inst));
  XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO, 0, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY}; XrSystemId sys; CK(xrGetSystem(inst, &sgi, &sys));

  // Vulkan through the runtime (XR_KHR_vulkan_enable2).
  PROC(xrGetVulkanGraphicsRequirements2KHR); PROC(xrCreateVulkanInstanceKHR); PROC(xrGetVulkanGraphicsDevice2KHR); PROC(xrCreateVulkanDeviceKHR);
  XrGraphicsRequirementsVulkan2KHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR}; CK(xrGetVulkanGraphicsRequirements2KHR(inst, sys, &req));
  VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "xrcalib", 1, "none", 1, VK_API_VERSION_1_1};
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
  g_dev = dev; g_cb = cb; g_q = q; g_fence = fence;

  XrGraphicsBindingVulkan2KHR gb = {XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR, 0, vki, pd, dev, qfi, 0};
  XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO, &gb, 0, sys}; XrSession ses; CK(xrCreateSession(inst, &sci, &ses));

  // Dot swapchain: 32x32, cleared white every frame.
  uint32_t nf = 0; CK(xrEnumerateSwapchainFormats(ses, 0, &nf, NULL)); int64_t fmts[64]; if (nf > 64) nf = 64; CK(xrEnumerateSwapchainFormats(ses, nf, &nf, fmts));
  int64_t fmt = fmts[0]; for (uint32_t i = 0; i < nf; i++) if (fmts[i] == VK_FORMAT_R8G8B8A8_SRGB || fmts[i] == VK_FORMAT_B8G8R8A8_SRGB) { fmt = fmts[i]; break; }
  XrSwapchainCreateInfo swci = {XR_TYPE_SWAPCHAIN_CREATE_INFO, 0, 0, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT, fmt, 1, 32, 32, 1, 1, 1};
  XrSwapchain sc; CK(xrCreateSwapchain(ses, &swci, &sc));
  uint32_t ni = 0; CK(xrEnumerateSwapchainImages(sc, 0, &ni, NULL)); XrSwapchainImageVulkan2KHR imgs[8]; if (ni > 8) ni = 8;
  for (uint32_t i = 0; i < ni; i++) imgs[i] = (XrSwapchainImageVulkan2KHR){XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR};
  CK(xrEnumerateSwapchainImages(sc, ni, &ni, (XrSwapchainImageBaseHeader *)imgs));
  XrSwapchain sc2; CK(xrCreateSwapchain(ses, &swci, &sc2)); XrSwapchainImageVulkan2KHR imgs2[8]; uint32_t ni2 = 0;
  CK(xrEnumerateSwapchainImages(sc2, 0, &ni2, NULL)); if (ni2 > 8) ni2 = 8;
  for (uint32_t i = 0; i < ni2; i++) imgs2[i] = (XrSwapchainImageVulkan2KHR){XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR};
  CK(xrEnumerateSwapchainImages(sc2, ni2, &ni2, (XrSwapchainImageBaseHeader *)imgs2));

  // Gaze action.
  XrActionSetCreateInfo asi = {XR_TYPE_ACTION_SET_CREATE_INFO}; strcpy(asi.actionSetName, "calib"); strcpy(asi.localizedActionSetName, "calib");
  XrActionSet as; CK(xrCreateActionSet(inst, &asi, &as));
  XrActionCreateInfo aci = {XR_TYPE_ACTION_CREATE_INFO, 0, "gaze", XR_ACTION_TYPE_POSE_INPUT, 0, 0, "gaze"}; XrAction act; CK(xrCreateAction(as, &aci, &act));
  XrPath pg, pp; xrStringToPath(inst, "/user/eyes_ext/input/gaze_ext/pose", &pg); xrStringToPath(inst, "/interaction_profiles/ext/eye_gaze_interaction", &pp);
  XrActionSuggestedBinding b = {act, pg}; XrInteractionProfileSuggestedBinding sb = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING, 0, pp, 1, &b};
  CK(xrSuggestInteractionProfileBindings(inst, &sb));
  XrActionSpaceCreateInfo asci = {XR_TYPE_ACTION_SPACE_CREATE_INFO, 0, act, 0, {{0,0,0,1},{0,0,0}}}; XrSpace gs; CK(xrCreateActionSpace(ses, &asci, &gs));
  XrReferenceSpaceCreateInfo rsi = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, 0, XR_REFERENCE_SPACE_TYPE_VIEW, {{0,0,0,1},{0,0,0}}}; XrSpace view; CK(xrCreateReferenceSpace(ses, &rsi, &view));
  XrSessionActionSetsAttachInfo ai = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, 0, 1, &as}; CK(xrAttachSessionActionSets(ses, &ai));

  int running = 0; double t_start = 0; long rows[NT] = {0}, frames = 0;
  for (;;) {
    XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(inst, &ev) == XR_SUCCESS) {
      if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        XrSessionState s = ((XrEventDataSessionStateChanged *)&ev)->state;
        if (s == XR_SESSION_STATE_READY) { XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO, 0, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO}; CK(xrBeginSession(ses, &bi)); running = 1; t_start = now_s(); }
        if (s == XR_SESSION_STATE_STOPPING || s == XR_SESSION_STATE_EXITING || s == XR_SESSION_STATE_LOSS_PENDING) goto done;
      }
      ev.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
    if (!running) { usleep(20000); continue; }
    if (live) {
      if (now_s() - t_start > live_s) break;
      XrFrameState fs = {XR_TYPE_FRAME_STATE}; CK(xrWaitFrame(ses, 0, &fs)); CK(xrBeginFrame(ses, 0));
      fill(sc2, imgs2, 0.35f); fill(sc, imgs, 1.0f);
      XrActiveActionSet aas = {as, XR_NULL_PATH}; XrActionsSyncInfo syi = {XR_TYPE_ACTIONS_SYNC_INFO, 0, 1, &aas}; xrSyncActions(ses, &syi);
      XrCompositionLayerQuad quads[NT + 1]; const XrCompositionLayerBaseHeader *layers[NT + 1]; uint32_t n = 0;
      for (int i = 0; i < NT; i++) quads[n++] = quad_at(view, sc2, YAWS[i % 5], PITCHES[i / 5], 1.0f);
      XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION}; xrLocateSpace(gs, view, fs.predictedDisplayTime, &loc);
      if (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
        XrQuaternionf g = loc.pose.orientation;
        float x = -2 * (g.x * g.z + g.w * g.y), y = -2 * (g.y * g.z - g.w * g.x), z = -(1 - 2 * (g.x * g.x + g.y * g.y));
        float yaw = atan2f(x, -z) * 180 / (float)M_PI, pitch = atan2f(y, hypotf(x, z)) * 180 / (float)M_PI;
        quads[n++] = quad_at(view, sc, yaw, pitch, 0.6f); rows[0]++;
        if (frames % 90 == 0) { printf("gaze yaw %+6.1f pitch %+6.1f\n", yaw, pitch); fflush(stdout); }
      }
      for (uint32_t i = 0; i < n; i++) layers[i] = (XrCompositionLayerBaseHeader *)&quads[i];
      XrFrameEndInfo fei = {XR_TYPE_FRAME_END_INFO, 0, fs.predictedDisplayTime, XR_ENVIRONMENT_BLEND_MODE_OPAQUE, n, layers};
      CK(xrEndFrame(ses, &fei)); frames++;
      continue;
    }
    double t = now_s() - t_start; int idx; double in_target;
    if (t < delay) { idx = 7; in_target = 0; } // centre dot while getting ready (index 7 = yaw 0, pitch 0)
    else { int k = (int)((t - delay) / hold); if (k >= NT) { completed = 1; break; } idx = k; in_target = (t - delay) - k * hold; }
    float yaw = YAWS[idx % 5], pitch = PITCHES[idx / 5];

    XrFrameState fs = {XR_TYPE_FRAME_STATE}; CK(xrWaitFrame(ses, 0, &fs)); CK(xrBeginFrame(ses, 0));
    // white while recording, dim grey during the settle second (so the user sees the jump and follows it)
    fill(sc, imgs, in_target >= settle || t < delay ? 1.0f : 0.5f);
    XrCompositionLayerQuad quad = quad_at(view, sc, yaw, pitch, 1.15f);
    const XrCompositionLayerBaseHeader *layers[] = {(XrCompositionLayerBaseHeader *)&quad};
    XrFrameEndInfo fei = {XR_TYPE_FRAME_END_INFO, 0, fs.predictedDisplayTime, XR_ENVIRONMENT_BLEND_MODE_OPAQUE, 1, layers};
    CK(xrEndFrame(ses, &fei)); frames++;

    XrActiveActionSet aas = {as, XR_NULL_PATH}; XrActionsSyncInfo syi = {XR_TYPE_ACTIONS_SYNC_INFO, 0, 1, &aas}; xrSyncActions(ses, &syi);
    if (t >= delay && in_target >= settle) {
      XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION}; xrLocateSpace(gs, view, fs.predictedDisplayTime, &loc);
      if (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
        XrQuaternionf g = loc.pose.orientation; // gaze dir = g * (0,0,-1)
        float x = -2 * (g.x * g.z + g.w * g.y), y = -2 * (g.y * g.z - g.w * g.x), z = -(1 - 2 * (g.x * g.x + g.y * g.y));
        fprintf(out, "%.3f,%d,%.1f,%.1f,%.5f,%.5f,%.5f\n", now_s(), idx, yaw, pitch, x, y, z); rows[idx]++;
      }
    }
  }
done:
  if (out) fclose(out);
  if (live) { printf("frames %ld, gaze valid %ld\n", frames, rows[0]); return 0; }
  printf("frames %ld; valid gaze rows per target:", frames); for (int i = 0; i < NT; i++) printf(" %ld", rows[i]); printf("\n");
  // A run cut short (session ended) must not be fitted: the targets come row by row, so a partial run lacks pitch data.
  if (!completed) { fprintf(stderr, "calibration interrupted before all %d targets were shown\n", NT); return 3; }
  for (int i = 0; i < NT; i++) if (rows[i] == 0) { fprintf(stderr, "no valid gaze for target %d\n", i); return 4; }
  return 0;
}

#define XR_USE_TIMESPEC 1
#include <stdlib.h>
// Headless OpenXR client: prints gaze (XR_EXT_eye_gaze_interaction) relative to the local/view space.
#define XR_USE_PLATFORM_XLIB 0
#define _GNU_SOURCE
#include <time.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#define CK(x) do { XrResult r_ = (x); if (XR_FAILED(r_)) { char b[64]; xrResultToString(inst, r_, b); fprintf(stderr, "%s failed: %s (line %d)\n", #x, b, __LINE__); return 1; } } while (0)
int main(int argc, char **argv) {
  int secs = argc > 1 ? atoi(argv[1]) : 20; XrInstance inst = XR_NULL_HANDLE;
  const char *exts[] = {"XR_MND_headless", "XR_EXT_eye_gaze_interaction", "XR_KHR_convert_timespec_time"};
  XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO}; strcpy(ici.applicationInfo.applicationName, "xrgaze");
  ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION; ici.enabledExtensionCount = 3; ici.enabledExtensionNames = exts;
  CK(xrCreateInstance(&ici, &inst));
  XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO, 0, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY}; XrSystemId sys; CK(xrGetSystem(inst, &sgi, &sys));
  XrSystemEyeGazeInteractionPropertiesEXT eg = {XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
  XrSystemProperties sp = {XR_TYPE_SYSTEM_PROPERTIES, &eg}; CK(xrGetSystemProperties(inst, sys, &sp));
  printf("system '%s' supportsEyeGazeInteraction=%d\n", sp.systemName, eg.supportsEyeGazeInteraction);
  XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO, 0, 0, sys}; XrSession ses; CK(xrCreateSession(inst, &sci, &ses));
  XrActionSetCreateInfo asi = {XR_TYPE_ACTION_SET_CREATE_INFO}; strcpy(asi.actionSetName, "gaze"); strcpy(asi.localizedActionSetName, "gaze");
  XrActionSet as; CK(xrCreateActionSet(inst, &asi, &as));
  XrActionCreateInfo aci = {XR_TYPE_ACTION_CREATE_INFO}; strcpy(aci.actionName, "gaze"); strcpy(aci.localizedActionName, "gaze"); aci.actionType = XR_ACTION_TYPE_POSE_INPUT; XrAction act; CK(xrCreateAction(as, &aci, &act));
  XrPath p_gaze, p_prof; xrStringToPath(inst, "/user/eyes_ext/input/gaze_ext/pose", &p_gaze); xrStringToPath(inst, "/interaction_profiles/ext/eye_gaze_interaction", &p_prof);
  XrActionSuggestedBinding b = {act, p_gaze}; XrInteractionProfileSuggestedBinding sb = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING, 0, p_prof, 1, &b}; CK(xrSuggestInteractionProfileBindings(inst, &sb));
  XrActionSpaceCreateInfo asci = {XR_TYPE_ACTION_SPACE_CREATE_INFO, 0, act, 0, {{0,0,0,1},{0,0,0}}}; XrSpace gs; CK(xrCreateActionSpace(ses, &asci, &gs));
  XrReferenceSpaceCreateInfo rsi = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, 0, XR_REFERENCE_SPACE_TYPE_VIEW, {{0,0,0,1},{0,0,0}}}; XrSpace view; CK(xrCreateReferenceSpace(ses, &rsi, &view));
  XrSessionActionSetsAttachInfo ai = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, 0, 1, &as}; CK(xrAttachSessionActionSets(ses, &ai));
  int running = 0; time_t t0 = time(0); long n = 0, valid = 0;
  while (time(0) - t0 < secs) {
    XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(inst, &ev) == XR_SUCCESS) {
      if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) { XrEventDataSessionStateChanged *e = (void*)&ev; printf("session state %d\n", e->state);
        if (e->state == XR_SESSION_STATE_READY) { XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO, 0, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO}; CK(xrBeginSession(ses, &bi)); running = 1; } }
      ev.type = XR_TYPE_EVENT_DATA_BUFFER; }
    if (!running) { usleep(50000); continue; }
    PFN_xrConvertTimespecTimeToTimeKHR conv; xrGetInstanceProcAddr(inst, "xrConvertTimespecTimeToTimeKHR", (PFN_xrVoidFunction*)&conv); struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); XrTime now; conv(inst, &ts, &now); usleep(16000);
    XrFrameState fs = {XR_TYPE_FRAME_STATE}; fs.predictedDisplayTime = now;
    XrActiveActionSet aas = {as, XR_NULL_PATH}; XrActionsSyncInfo si = {XR_TYPE_ACTIONS_SYNC_INFO, 0, 1, &aas}; xrSyncActions(ses, &si);
    XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION}; xrLocateSpace(gs, view, fs.predictedDisplayTime, &loc);
    n++; int ok = (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0; valid += ok;
    if (ok && getenv("GAZE_CSV")) { struct timespec rt; clock_gettime(CLOCK_REALTIME, &rt); XrQuaternionf q = loc.pose.orientation; float x = -2*(q.x*q.z + q.w*q.y), y = -2*(q.y*q.z - q.w*q.x), z = -(1 - 2*(q.x*q.x + q.y*q.y)); printf("%.3f,%.4f,%.4f,%.4f\n", rt.tv_sec + rt.tv_nsec*1e-9, x, y, z); }
    else if (ok && n % 12 == 0) { XrQuaternionf q = loc.pose.orientation; // gaze dir = q * (0,0,-1)
      float x = -2*(q.x*q.z + q.w*q.y), y = -2*(q.y*q.z - q.w*q.x), z = -(1 - 2*(q.x*q.x + q.y*q.y));
      printf("gaze dir x=%+.2f y=%+.2f z=%+.2f   (yaw %+5.1f deg, pitch %+5.1f deg)\n", x, y, z, atan2f(x, -z)*57.2958f, asinf(y)*57.2958f); fflush(stdout); }
  }
  printf("done: %ld frames, gaze valid in %ld\n", n, valid); return 0; }

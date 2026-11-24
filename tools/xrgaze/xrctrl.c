#define XR_USE_TIMESPEC 1
// Headless OpenXR client: controller grip poses + trigger through /interaction_profiles/oculus/touch_controller
// (the profile apps such as WayVR suggest). Prints per hand once a second: bound profile, pose validity/tracking,
// position in LOCAL space, trigger value. Usage: xrctrl [seconds]
#define _GNU_SOURCE
#include <stdlib.h>
#include <time.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define CK(x) do { XrResult r_ = (x); if (XR_FAILED(r_)) { char b[64]; xrResultToString(inst, r_, b); fprintf(stderr, "%s failed: %s (line %d)\n", #x, b, __LINE__); return 1; } } while (0)
static XrInstance inst;
static XrPath P(const char *s) { XrPath p; xrStringToPath(inst, s, &p); return p; }
int main(int argc, char **argv) {
  int secs = argc > 1 ? atoi(argv[1]) : 20;
  const char *exts[] = {"XR_MND_headless", "XR_KHR_convert_timespec_time"};
  XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO}; strcpy(ici.applicationInfo.applicationName, "xrctrl");
  ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION; ici.enabledExtensionCount = 2; ici.enabledExtensionNames = exts;
  CK(xrCreateInstance(&ici, &inst));
  XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO, 0, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY}; XrSystemId sys; CK(xrGetSystem(inst, &sgi, &sys));
  XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO, 0, 0, sys}; XrSession ses; CK(xrCreateSession(inst, &sci, &ses));
  XrPath hands[2] = {P("/user/hand/left"), P("/user/hand/right")};
  XrActionSetCreateInfo asi = {XR_TYPE_ACTION_SET_CREATE_INFO}; strcpy(asi.actionSetName, "ctrl"); strcpy(asi.localizedActionSetName, "ctrl");
  XrActionSet as; CK(xrCreateActionSet(inst, &asi, &as));
  XrActionCreateInfo aci = {XR_TYPE_ACTION_CREATE_INFO, 0, "grip", XR_ACTION_TYPE_POSE_INPUT, 2, hands, "grip"}; XrAction grip; CK(xrCreateAction(as, &aci, &grip));
  XrActionCreateInfo tci = {XR_TYPE_ACTION_CREATE_INFO, 0, "trigger", XR_ACTION_TYPE_FLOAT_INPUT, 2, hands, "trigger"}; XrAction trig; CK(xrCreateAction(as, &tci, &trig));
  XrActionSuggestedBinding b[] = {{grip, P("/user/hand/left/input/grip/pose")}, {grip, P("/user/hand/right/input/grip/pose")},
                                  {trig, P("/user/hand/left/input/trigger/value")}, {trig, P("/user/hand/right/input/trigger/value")}};
  XrInteractionProfileSuggestedBinding sb = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING, 0, P("/interaction_profiles/oculus/touch_controller"), 4, b};
  CK(xrSuggestInteractionProfileBindings(inst, &sb));
  XrSpace gs[2];
  for (int i = 0; i < 2; i++) { XrActionSpaceCreateInfo c = {XR_TYPE_ACTION_SPACE_CREATE_INFO, 0, grip, hands[i], {{0,0,0,1},{0,0,0}}}; CK(xrCreateActionSpace(ses, &c, &gs[i])); }
  XrReferenceSpaceCreateInfo rsi = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, 0, XR_REFERENCE_SPACE_TYPE_LOCAL, {{0,0,0,1},{0,0,0}}}; XrSpace local; CK(xrCreateReferenceSpace(ses, &rsi, &local));
  XrReferenceSpaceCreateInfo vsi = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, 0, XR_REFERENCE_SPACE_TYPE_VIEW, {{0,0,0,1},{0,0,0}}}; XrSpace vspace; CK(xrCreateReferenceSpace(ses, &vsi, &vspace));
  float lo[3][3], hi[3][3]; long hv = 0; for (int k = 0; k < 3; k++) for (int j = 0; j < 3; j++) { lo[k][j] = 1e9f; hi[k][j] = -1e9f; }
  XrSessionActionSetsAttachInfo ai = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, 0, 1, &as}; CK(xrAttachSessionActionSets(ses, &ai));
  PFN_xrConvertTimespecTimeToTimeKHR conv; CK(xrGetInstanceProcAddr(inst, "xrConvertTimespecTimeToTimeKHR", (PFN_xrVoidFunction *)&conv));
  // XRCTRL_CSV=file: one line per frame: t, then head, L, R as valid,tracked,px,py,pz,qx,qy,qz,qw (LOCAL space).
  FILE *csv = getenv("XRCTRL_CSV") ? fopen(getenv("XRCTRL_CSV"), "w") : NULL;
  struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
  int running = 0; time_t t0 = time(0), last = 0; long n = 0, valid[2] = {0}, tracked[2] = {0};
  while (time(0) - t0 < secs) {
    XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(inst, &ev) == XR_SUCCESS) {
      if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED && ((XrEventDataSessionStateChanged *)&ev)->state == XR_SESSION_STATE_READY) {
        XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO, 0, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO}; CK(xrBeginSession(ses, &bi)); running = 1; }
      ev.type = XR_TYPE_EVENT_DATA_BUFFER; }
    if (!running) { usleep(50000); continue; }
    usleep(16000);
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); XrTime now; conv(inst, &ts, &now);
    XrActiveActionSet aas = {as, XR_NULL_PATH}; XrActionsSyncInfo si = {XR_TYPE_ACTIONS_SYNC_INFO, 0, 1, &aas}; xrSyncActions(ses, &si);
    n++; int print = time(0) != last; if (print) last = time(0);
    XrSpaceLocation hl = {XR_TYPE_SPACE_LOCATION}; xrLocateSpace(vspace, local, now, &hl);
#define CSVPOSE(l) fprintf(csv, ",%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f", ((l).locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0, \
      ((l).locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0, (l).pose.position.x, (l).pose.position.y, (l).pose.position.z, \
      (l).pose.orientation.x, (l).pose.orientation.y, (l).pose.orientation.z, (l).pose.orientation.w)
    if (csv) { fprintf(csv, "%.3f", ts.tv_sec - start.tv_sec + (ts.tv_nsec - start.tv_nsec) * 1e-9); CSVPOSE(hl);
      for (int i = 0; i < 2; i++) { XrSpaceLocation l2 = {XR_TYPE_SPACE_LOCATION}; xrLocateSpace(gs[i], local, now, &l2); CSVPOSE(l2); }
      fputc('\n', csv); }
    if (hl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) { hv++; float *q = &hl.pose.position.x; for (int j = 0; j < 3; j++) { if (q[j] < lo[2][j]) lo[2][j] = q[j]; if (q[j] > hi[2][j]) hi[2][j] = q[j]; } }
    if (print) {
      float r[3]; for (int k = 0; k < 3; k++) { r[k] = 0; for (int j = 0; j < 3; j++) { float d = hi[k][j] - lo[k][j]; if (d > r[k] && d < 1e8f) r[k] = d; } }
      printf("HEAD valid=%d pos (%+.3f %+.3f %+.3f) | 1s range cm: head %.1f  L %.1f  R %.1f\n", (hl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0,
             hl.pose.position.x, hl.pose.position.y, hl.pose.position.z, r[2] * 100, r[0] * 100, r[1] * 100);
      for (int k = 0; k < 3; k++) for (int j = 0; j < 3; j++) { lo[k][j] = 1e9f; hi[k][j] = -1e9f; }
    }
    for (int i = 0; i < 2; i++) {
      XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION}; xrLocateSpace(gs[i], local, now, &loc);
      int v = (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0, t = (loc.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
      valid[i] += v; tracked[i] += t;
      if (v) { float *q = &loc.pose.position.x; for (int j = 0; j < 3; j++) { if (q[j] < lo[i][j]) lo[i][j] = q[j]; if (q[j] > hi[i][j]) hi[i][j] = q[j]; } }
      if (!print) continue;
      XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO, 0, trig, hands[i]}; XrActionStateFloat f = {XR_TYPE_ACTION_STATE_FLOAT}; xrGetActionStateFloat(ses, &gi, &f);
      XrInteractionProfileState ps = {XR_TYPE_INTERACTION_PROFILE_STATE}; xrGetCurrentInteractionProfile(ses, hands[i], &ps);
      char prof[XR_MAX_PATH_LENGTH] = "(none)"; uint32_t len; if (ps.interactionProfile) xrPathToString(inst, ps.interactionProfile, sizeof prof, &len, prof);
      printf("%s profile=%s pos valid=%d tracked=%d (%+.2f %+.2f %+.2f) trigger active=%d %.2f\n", i ? "R" : "L", prof, v, t,
             loc.pose.position.x, loc.pose.position.y, loc.pose.position.z, f.isActive, f.currentState);
    }
    if (print) fflush(stdout);
  }
  printf("head valid %ld/%ld; ", hv, n); printf("done: %ld frames; L pos valid %ld tracked %ld; R pos valid %ld tracked %ld\n", n, valid[0], tracked[0], valid[1], tracked[1]);
  return 0;
}

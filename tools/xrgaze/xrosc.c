#define XR_USE_TIMESPEC 1
// Headless OpenXR eye-gaze -> VRChat OSC bridge.
// Usage: xrosc [host=127.0.0.1] [port=9000] [--rate 60] [--fake] [--seconds N]
//
// Sends VRChat's native eye tracking OSC (https://docs.vrchat.com/docs/osc-eye-tracking):
//   /tracking/eye/CenterPitchYaw   ",ff"  pitch, yaw in degrees
//   /tracking/eye/EyesClosedAmount ",f"   0..1 (0 = open); one value for both eyes
// Sign convention per the docs: POSITIVE PITCH = LOOKING DOWN, POSITIVE YAW = LOOKING RIGHT (docs example:
// a target 15 deg up / 20 deg right is sent as pitch -15.25, yaw 20.13). OpenXR VIEW space is -z forward, +x right,
// +y up, so pitch_osc = -asin(y) (negated vs. xrcalib's "up positive" pitch) and yaw_osc = atan2(x, -z).
// The docs define no "invalid" marker; the only fallback is a 10 s timeout after which VRChat reverts to
// auto-look/auto-blink. So when gaze is invalid we send nothing (EyesClosedAmount included) and let that happen.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <math.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

static XrInstance inst;
#define CK(x) do { XrResult r_ = (x); if (XR_FAILED(r_)) { char b[64]; xrResultToString(inst, r_, b); fprintf(stderr, "%s failed: %s (line %d)\n", #x, b, __LINE__); return 1; } } while (0)

// OSC 1.0 message: address, ",f..." type tags, each string null-terminated and padded to 4 bytes, big-endian float32.
static size_t osc_pad_str(uint8_t *p, const char *s) {
  size_t n = strlen(s) + 1, padded = (n + 3) & ~(size_t)3; memset(p, 0, padded); memcpy(p, s, n - 1); return padded;
}
static size_t osc_msg(uint8_t *buf, const char *addr, int nf, const float *f) {
  char tags[16] = ","; for (int i = 0; i < nf; i++) strcat(tags, "f");
  size_t o = osc_pad_str(buf, addr); o += osc_pad_str(buf + o, tags);
  for (int i = 0; i < nf; i++) { uint32_t u; memcpy(&u, &f[i], 4); u = htonl(u); memcpy(buf + o, &u, 4); o += 4; }
  return o;
}
static int sock; static struct sockaddr_storage dst; static socklen_t dstlen; static long sent;
static void osc_send(const char *addr, int nf, const float *f) {
  uint8_t buf[128]; size_t n = osc_msg(buf, addr, nf, f);
  if (sendto(sock, buf, n, 0, (struct sockaddr *)&dst, dstlen) >= 0) sent++;
}
static XrPath P(const char *s) { XrPath p; xrStringToPath(inst, s, &p); return p; }

int main(int argc, char **argv) {
  const char *host = "127.0.0.1", *port = "9000"; double rate = 60, secs = 0; int fake = 0, pos = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--fake")) fake = 1;
    else if (!strcmp(argv[i], "--rate") && i + 1 < argc) rate = atof(argv[++i]);
    else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) secs = atof(argv[++i]);
    else if (argv[i][0] != '-') { if (pos == 0) host = argv[i]; else if (pos == 1) port = argv[i]; pos++; }
    else { fprintf(stderr, "usage: xrosc [host] [port] [--rate 60] [--fake] [--seconds N]\n"); return 2; }
  }
  if (!isfinite(rate) || rate < 1) rate = 1;
  if (rate > 1000) rate = 1000;
  struct addrinfo hints = {.ai_socktype = SOCK_DGRAM}, *res;
  if (getaddrinfo(host, port, &hints, &res)) { fprintf(stderr, "cannot resolve %s:%s\n", host, port); return 1; }
  memcpy(&dst, res->ai_addr, res->ai_addrlen); dstlen = res->ai_addrlen;
  sock = socket(res->ai_family, SOCK_DGRAM, 0); freeaddrinfo(res); if (sock < 0) { perror("socket"); return 1; }

  XrSession ses = 0; XrSpace gs = 0, view = 0; XrActionSet as = 0; PFN_xrConvertTimespecTimeToTimeKHR conv = 0; int running = 0;
  if (!fake) {
    const char *exts[] = {"XR_MND_headless", "XR_EXT_eye_gaze_interaction", "XR_KHR_convert_timespec_time"};
    XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO}; strcpy(ici.applicationInfo.applicationName, "xrosc");
    ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION; ici.enabledExtensionCount = 3; ici.enabledExtensionNames = exts;
    CK(xrCreateInstance(&ici, &inst));
    XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO, 0, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY}; XrSystemId sys; CK(xrGetSystem(inst, &sgi, &sys));
    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO, 0, 0, sys}; CK(xrCreateSession(inst, &sci, &ses));
    XrActionSetCreateInfo asi = {XR_TYPE_ACTION_SET_CREATE_INFO}; strcpy(asi.actionSetName, "osc"); strcpy(asi.localizedActionSetName, "osc");
    CK(xrCreateActionSet(inst, &asi, &as));
    XrActionCreateInfo aci = {XR_TYPE_ACTION_CREATE_INFO, 0, "gaze", XR_ACTION_TYPE_POSE_INPUT, 0, 0, "gaze"}; XrAction act; CK(xrCreateAction(as, &aci, &act));
    XrActionSuggestedBinding b = {act, P("/user/eyes_ext/input/gaze_ext/pose")};
    XrInteractionProfileSuggestedBinding sb = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING, 0, P("/interaction_profiles/ext/eye_gaze_interaction"), 1, &b};
    CK(xrSuggestInteractionProfileBindings(inst, &sb));
    XrActionSpaceCreateInfo asci = {XR_TYPE_ACTION_SPACE_CREATE_INFO, 0, act, 0, {{0,0,0,1},{0,0,0}}}; CK(xrCreateActionSpace(ses, &asci, &gs));
    XrReferenceSpaceCreateInfo rsi = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, 0, XR_REFERENCE_SPACE_TYPE_VIEW, {{0,0,0,1},{0,0,0}}}; CK(xrCreateReferenceSpace(ses, &rsi, &view));
    XrSessionActionSetsAttachInfo ai = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, 0, 1, &as}; CK(xrAttachSessionActionSets(ses, &ai));
    CK(xrGetInstanceProcAddr(inst, "xrConvertTimespecTimeToTimeKHR", (PFN_xrVoidFunction *)&conv));
  }

  struct timespec t0, next, tl; clock_gettime(CLOCK_MONOTONIC, &t0); next = t0; tl = t0;
  long ticks = 0, valid = 0, tick_w = 0, valid_w = 0, sent_w0 = 0; float lp = 0, ly = 0; int have = 0;
  const long period_ns = (long)(1e9 / rate);
  for (;;) {
    clock_gettime(CLOCK_MONOTONIC, &tl);
    double el = (tl.tv_sec - t0.tv_sec) + (tl.tv_nsec - t0.tv_nsec) * 1e-9;
    if (secs > 0 && el >= secs) break;
    if (!fake) {
      XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
      while (xrPollEvent(inst, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
          XrSessionState s = ((XrEventDataSessionStateChanged *)&ev)->state;
          if (s == XR_SESSION_STATE_READY && !running) { XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO, 0, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO}; CK(xrBeginSession(ses, &bi)); running = 1; }
          if (s == XR_SESSION_STATE_STOPPING || s == XR_SESSION_STATE_EXITING || s == XR_SESSION_STATE_LOSS_PENDING) goto done;
        }
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
      }
    }
    int ok = 0; float pitch = 0, yaw = 0;
    if (fake) {
      double a = el * 2 * M_PI / 6.0; // 6 s per revolution, 15 deg radius
      pitch = (float)(15 * sin(a)); yaw = (float)(15 * cos(a)); ok = 1;
    } else if (running) {
      XrTime now; conv(inst, &tl, &now); // tl is CLOCK_MONOTONIC
      XrActiveActionSet aas = {as, XR_NULL_PATH}; XrActionsSyncInfo si = {XR_TYPE_ACTIONS_SYNC_INFO, 0, 1, &aas}; xrSyncActions(ses, &si);
      XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION}; xrLocateSpace(gs, view, now, &loc);
      if (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
        XrQuaternionf g = loc.pose.orientation; // gaze dir = g * (0,0,-1), as in xrcalib
        float x = -2 * (g.x * g.z + g.w * g.y), y = -2 * (g.y * g.z - g.w * g.x), z = -(1 - 2 * (g.x * g.x + g.y * g.y));
        yaw = atan2f(x, -z) * 180 / (float)M_PI;
        pitch = -atan2f(y, hypotf(x, z)) * 180 / (float)M_PI; // VRChat: positive = down
        ok = 1;
      }
    }
    ticks++; tick_w++;
    if (ok) {
      valid++; valid_w++; lp = pitch; ly = yaw; have = 1;
      float py[2] = {pitch, yaw}, closed = 0;
      osc_send("/tracking/eye/CenterPitchYaw", 2, py);
      osc_send("/tracking/eye/EyesClosedAmount", 1, &closed);
    }
    static long last_sec = 0;
    if ((long)el > last_sec) {
      last_sec = (long)el;
      printf("sent=%ld (+%ld/s) pitch=%+.1f yaw=%+.1f gaze valid %.0f%%%s\n", sent, sent - sent_w0, have ? lp : 0, have ? ly : 0,
             tick_w ? 100.0 * valid_w / tick_w : 0.0, fake ? " [fake]" : running ? "" : " [session not running]");
      fflush(stdout); tick_w = valid_w = 0; sent_w0 = sent;
    }
    next.tv_nsec += period_ns; while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
    if (next.tv_sec < tl.tv_sec || (next.tv_sec == tl.tv_sec && next.tv_nsec < tl.tv_nsec)) {
      next = tl;
      next.tv_nsec += period_ns; while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
  }
done:
  printf("done: %ld ticks, %ld valid, %ld datagrams sent\n", ticks, valid, sent);
  return 0;
}

// Prints the runtime's recommended view sizes (stereo and quad) and the quad view angles, then exits.
// Build: cc -O2 -I<openxr include> -o xrsizes xrsizes.c -lopenxr_loader
#include <time.h>
#define XR_USE_TIMESPEC
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void sizes(XrInstance inst, XrSystemId sys, XrViewConfigurationType type, const char *name)
{
	uint32_t n = 0;
	if (XR_FAILED(xrEnumerateViewConfigurationViews(inst, sys, type, 0, &n, NULL)) || n == 0) {
		printf("%-7s not offered\n", name);
		return;
	}
	XrViewConfigurationView v[4];
	for (uint32_t i = 0; i < 4; i++) v[i] = (XrViewConfigurationView){XR_TYPE_VIEW_CONFIGURATION_VIEW};
	if (n > 4) n = 4;
	if (XR_FAILED(xrEnumerateViewConfigurationViews(inst, sys, type, 4, &n, v)) || n > 4) {
		printf("%-7s enumerate failed\n", name);
		return;
	}
	long px = 0;
	printf("%-7s", name);
	for (uint32_t i = 0; i < n; i++) {
		printf("  [%u] %ux%u (max %ux%u)", i, v[i].recommendedImageRectWidth, v[i].recommendedImageRectHeight,
		       v[i].maxImageRectWidth, v[i].maxImageRectHeight);
		px += (long)v[i].recommendedImageRectWidth * v[i].recommendedImageRectHeight;
	}
	printf("  total %.1f MP\n", px / 1e6);
}

int main(void)
{
	XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO};
	strcpy(ici.applicationInfo.applicationName, "xrsizes");
	ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	// Enable only the extensions the runtime offers.
	const char *want[] = {"XR_MND_headless", "XR_VARJO_quad_views", "XR_KHR_convert_timespec_time"};
	const char *ext[3];
	uint32_t ext_count = 0, avail_n = 0;
	xrEnumerateInstanceExtensionProperties(NULL, 0, &avail_n, NULL);
	XrExtensionProperties avail[256];
	if (avail_n > 256) avail_n = 256;
	for (uint32_t i = 0; i < avail_n; i++) avail[i] = (XrExtensionProperties){XR_TYPE_EXTENSION_PROPERTIES};
	xrEnumerateInstanceExtensionProperties(NULL, avail_n, &avail_n, avail);
	for (uint32_t w = 0; w < 3; w++)
		for (uint32_t i = 0; i < avail_n; i++)
			if (strcmp(avail[i].extensionName, want[w]) == 0) {
				ext[ext_count++] = want[w];
				break;
			}
	ici.enabledExtensionCount = ext_count;
	ici.enabledExtensionNames = ext;
	XrInstance inst;
	if (XR_FAILED(xrCreateInstance(&ici, &inst))) return printf("no instance\n"), 1;
	XrSystemGetInfo gi = {XR_TYPE_SYSTEM_GET_INFO, NULL, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
	XrSystemId sys;
	if (XR_FAILED(xrGetSystem(inst, &gi, &sys))) return printf("no system\n"), 1;
	sizes(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, "stereo");
	sizes(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO, "quad");

	// Angles: a headless session can locate views without rendering.
	XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO, NULL, 0, sys};
	XrSession ses;
	if (XR_FAILED(xrCreateSession(inst, &sci, &ses))) return printf("(no headless session: angles skipped)\n"), 0;
	XrReferenceSpaceCreateInfo rs = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, NULL, XR_REFERENCE_SPACE_TYPE_VIEW, {{0, 0, 0, 1}, {0, 0, 0}}};
	XrSpace view;
	xrCreateReferenceSpace(ses, &rs, &view);
	for (int t = 0; t < 100; t++) {
		XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
		while (xrPollEvent(inst, &ev) == XR_SUCCESS) {
			if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED &&
			    ((XrEventDataSessionStateChanged *)&ev)->state == XR_SESSION_STATE_READY) {
				XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO, NULL, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO};
				xrBeginSession(ses, &bi);
			}
			ev.type = XR_TYPE_EVENT_DATA_BUFFER;
		}
		XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO, NULL, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO, 1, view};
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		XrViewState vs = {XR_TYPE_VIEW_STATE};
		XrView v[4];
		for (int i = 0; i < 4; i++) v[i] = (XrView){XR_TYPE_VIEW};
		uint32_t c = 0;
		PFN_xrConvertTimespecTimeToTimeKHR conv = NULL;
		xrGetInstanceProcAddr(inst, "xrConvertTimespecTimeToTimeKHR", (PFN_xrVoidFunction *)&conv);
		if (conv) conv(inst, &ts, &li.displayTime);
		if (XR_SUCCEEDED(xrLocateViews(ses, &li, &vs, 4, &c, v)) && c == 4) {
			for (int i = 0; i < 4; i++) {
				const XrFovf *f = &v[i].fov;
				printf("view %d fov L %.1f R %.1f U %.1f D %.1f deg\n", i, f->angleLeft * 57.2958, f->angleRight * 57.2958,
				       f->angleUp * 57.2958, f->angleDown * 57.2958);
			}
			return 0;
		}
		usleep(20000);
	}
	printf("(could not locate quad views)\n");
	return 0;
}

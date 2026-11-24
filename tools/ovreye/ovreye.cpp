// Minimal OpenVR client: reads SteamVR's eye-tracking action. build: see build.sh   usage: ovreye [type] [seconds]
#include <openvr.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
int main(int argc, char **argv) {
	const char *type = argc > 1 ? argv[1] : "pose";
	int secs = argc > 2 ? atoi(argv[2]) : 15;
	vr::EVRInitError err;
	vr::VR_Init(&err, vr::VRApplication_Overlay);
	if (err != vr::VRInitError_None) { printf("VR_Init: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(err)); return 1; }
	char dir[512]; getcwd(dir, sizeof dir);
	std::string man = std::string(dir) + "/actions.json";
	FILE *f = fopen(man.c_str(), "w");
	fprintf(f, "{\"actions\":[{\"name\":\"/actions/main/in/Eyes\",\"type\":\"%s\",\"requirement\":\"optional\"}],"
	           "\"action_sets\":[{\"name\":\"/actions/main\",\"usage\":\"leftright\"}],\"default_bindings\":[{\"controller_type\":\"monado_hmd\",\"binding_url\":\"%s\"}]}", type, (std::string(dir)+"/bindings.json").c_str());
	fclose(f);
	std::string bind = std::string(dir) + "/bindings.json";
	f = fopen(bind.c_str(), "w");
	const char *bpath = argc > 3 ? argv[3] : "/user/head/eyetracking";
	fprintf(f, "{\"controller_type\":\"monado_hmd\",\"description\":\"eyes\",\"name\":\"eyes\",\"bindings\":{\"/actions/main\":{\"eyetracking\":[{\"path\":\"%s\",\"output\":\"/actions/main/in/Eyes\"}]}}}", bpath);
	fclose(f);
	printf("HMD: %d devices; supports eye tracking prop=%d\n", vr::VRSystem()->IsTrackedDeviceConnected(0),
	       vr::VRSystem()->GetBoolTrackedDeviceProperty(0, (vr::ETrackedDeviceProperty)3069 /*unused*/));
	vr::EVRInputError e = vr::VRInput()->SetActionManifestPath(man.c_str());
	printf("SetActionManifestPath: %d\n", e);
	vr::VRActionHandle_t act = 0; vr::VRActionSetHandle_t set = 0;
	printf("GetActionHandle: %d\n", vr::VRInput()->GetActionHandle("/actions/main/in/Eyes", &act));
	vr::VRInput()->GetActionSetHandle("/actions/main", &set);
	// Overlay dot that follows the gaze (head-relative), plus a dim fixed dot at the centre of view.
	vr::VROverlayHandle_t dot = 0, ctr = 0;
	vr::VROverlay()->CreateOverlay("ovreye.dot", "gaze", &dot);
	vr::VROverlay()->CreateOverlay("ovreye.ctr", "centre", &ctr);
	static unsigned char px[64 * 64 * 4], pc[64 * 64 * 4];
	for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
		float dx = x - 31.5f, dy = y - 31.5f, r = dx * dx + dy * dy;
		if (r < 30 * 30) { px[(y*64+x)*4] = 255; px[(y*64+x)*4+1] = 30; px[(y*64+x)*4+2] = 30; px[(y*64+x)*4+3] = 255; }
		if (r < 6 * 6) { pc[(y*64+x)*4] = 255; pc[(y*64+x)*4+1] = 255; pc[(y*64+x)*4+2] = 255; pc[(y*64+x)*4+3] = 160; }
	}
	vr::VROverlay()->SetOverlayRaw(dot, px, 64, 64, 4);
	vr::VROverlay()->SetOverlayRaw(ctr, pc, 64, 64, 4);
	vr::VROverlay()->SetOverlayWidthInMeters(dot, 0.08f);
	vr::VROverlay()->SetOverlayWidthInMeters(ctr, 0.03f);
	vr::VROverlay()->ShowOverlay(dot);
	vr::VROverlay()->ShowOverlay(ctr);
	{ vr::HmdMatrix34_t m = {{{1,0,0,0},{0,1,0,0},{0,0,1,-2.0f}}};
	  vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(ctr, vr::k_unTrackedDeviceIndex_Hmd, &m); }
	for (int i = 0; i < secs * 10; i++) {
		vr::VRActiveActionSet_t as = {}; as.ulActionSet = set;
		vr::VRInput()->UpdateActionState(&as, sizeof as, 1);
		vr::VREyeTrackingData_t d = {};
		vr::EVRInputError r = vr::VRInput()->GetEyeTrackingDataForNextFrame(act, vr::TrackingUniverseStanding, &d, sizeof d);
		if (d.bValid) {
			float tx = d.vGazeTarget.v[0], ty = d.vGazeTarget.v[1], tz = d.vGazeTarget.v[2];
			vr::HmdMatrix34_t m = {{{1,0,0,tx},{0,1,0,ty},{0,0,1,tz}}};
			vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(dot, vr::k_unTrackedDeviceIndex_Hmd, &m);
			vr::VROverlay()->ShowOverlay(dot);
		} else vr::VROverlay()->HideOverlay(dot);
		if (i % 5 == 0)
			printf("r=%d active=%d valid=%d tracked=%d origin=(%.2f %.2f %.2f) target=(%.2f %.2f %.2f)\n", r, d.bActive, d.bValid,
			       d.bTracked, d.vGazeOrigin.v[0], d.vGazeOrigin.v[1], d.vGazeOrigin.v[2], d.vGazeTarget.v[0], d.vGazeTarget.v[1], d.vGazeTarget.v[2]);
		usleep(100000);
	}
	vr::VR_Shutdown();
}

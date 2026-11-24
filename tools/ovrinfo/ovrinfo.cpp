// Print what SteamVR reports for the HMD: per-eye raw projection (tan angles), eye-to-head transform, render size.
#include <openvr.h>
#include <cmath>
#include <cstdio>
int main() {
	vr::EVRInitError err;
	vr::IVRSystem *s = vr::VR_Init(&err, vr::VRApplication_Background);
	if (!s) { printf("VR_Init: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(err)); return 1; }
	uint32_t w, h; s->GetRecommendedRenderTargetSize(&w, &h);
	printf("recommended per-eye %ux%u\n", w, h);
	const double r2d = 180.0 / M_PI;
	for (int e = 0; e < 2; e++) {
		float l, r, t, b; s->GetProjectionRaw((vr::EVREye)e, &l, &r, &t, &b);
		vr::HmdMatrix34_t m = s->GetEyeToHeadTransform((vr::EVREye)e);
		double yaw = atan2(m.m[0][2], m.m[2][2]) * r2d;
		printf("%s: tan l=%.4f r=%.4f t=%.4f b=%.4f  -> deg l=%.2f r=%.2f t=%.2f b=%.2f (h %.1f v %.1f)\n", e ? "right" : "left ",
		       l, r, t, b, atan(l) * r2d, atan(r) * r2d, atan(t) * r2d, atan(b) * r2d, (atan(r) - atan(l)) * r2d, (atan(b) - atan(t)) * r2d);
		printf("       eye pos x=%.4f y=%.4f z=%.4f  yaw=%.2f deg\n", m.m[0][3], m.m[1][3], m.m[2][3], yaw);
	}
	// Distortion map: for panel point (u,v) of each eye viewport, which render-target uv (green) is sampled.
	// Also print the panel point that samples the render-target point of tan angle 0 (the eye's forward axis).
	for (int e = 0; e < 2; e++) {
		float l, r, t, b; s->GetProjectionRaw((vr::EVREye)e, &l, &r, &t, &b);
		float cu = -l / (r - l), cv = -t / (b - t);
		printf("%s grid (panel u,v -> green u,v); forward axis at render uv (%.4f, %.4f)\n", e ? "right" : "left ", cu, cv);
		float best = 1e9, bu = 0, bv = 0;
		for (int j = 0; j <= 200; j++) for (int i = 0; i <= 200; i++) {
			vr::DistortionCoordinates_t d;
			if (!s->ComputeDistortion((vr::EVREye)e, i / 200.f, j / 200.f, &d)) continue;
			float du = d.rfGreen[0] - cu, dv = d.rfGreen[1] - cv, q = du * du + dv * dv;
			if (q < best) { best = q; bu = i / 200.f; bv = j / 200.f; }
		}
		printf("   forward axis shown at panel (%.3f, %.3f)  [0.5,0.5 = viewport centre]\n", bu, bv);
		for (int j = 0; j <= 4; j++) {
			printf("  ");
			for (int i = 0; i <= 4; i++) {
				vr::DistortionCoordinates_t d;
				s->ComputeDistortion((vr::EVREye)e, i / 4.f, j / 4.f, &d);
				printf(" (%5.2f,%5.2f)", d.rfGreen[0], d.rfGreen[1]);
			}
			printf("\n");
		}
	}
	vr::VR_Shutdown();
}

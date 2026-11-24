#include <openxr/openxr.h>
#include <stdio.h>
#include <string.h>
int main(){ XrInstance inst; const char*exts[]={"XR_MND_headless","XR_VARJO_quad_views","XR_VARJO_foveated_rendering"};
 XrInstanceCreateInfo ici={XR_TYPE_INSTANCE_CREATE_INFO}; strcpy(ici.applicationInfo.applicationName,"xrfov"); ici.applicationInfo.apiVersion=XR_CURRENT_API_VERSION; ici.enabledExtensionCount=3; ici.enabledExtensionNames=exts;
 XrResult r=xrCreateInstance(&ici,&inst); printf("create instance with VARJO_foveated_rendering: %d\n",r); if(r) return 1;
 XrSystemGetInfo sgi={XR_TYPE_SYSTEM_GET_INFO,0,XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY}; XrSystemId sys; xrGetSystem(inst,&sgi,&sys);
 XrSystemFoveatedRenderingPropertiesVARJO fp={XR_TYPE_SYSTEM_FOVEATED_RENDERING_PROPERTIES_VARJO}; XrSystemProperties sp={XR_TYPE_SYSTEM_PROPERTIES,&fp}; xrGetSystemProperties(inst,sys,&sp);
 printf("supportsFoveatedRendering=%d\n",fp.supportsFoveatedRendering); return 0; }

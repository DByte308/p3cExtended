#define _GNU_SOURCE
#define XR_USE_TIMESPEC 1
#include <stdlib.h>
#include <time.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
static void dir(XrQuaternionf q, float *yaw, float *pitch){ float x=-2*(q.x*q.z+q.w*q.y), y=-2*(q.y*q.z-q.w*q.x), z=-(1-2*(q.x*q.x+q.y*q.y)); *yaw=atan2f(x,-z)*57.2958f; *pitch=asinf(y)*57.2958f; }
int main(int argc,char**argv){
  int secs=argc>1?atoi(argv[1]):10; XrInstance inst; const char*exts[]={"XR_MND_headless","XR_VARJO_quad_views","XR_KHR_convert_timespec_time"};
  XrInstanceCreateInfo ici={XR_TYPE_INSTANCE_CREATE_INFO}; strcpy(ici.applicationInfo.applicationName,"xrviews"); ici.applicationInfo.apiVersion=XR_CURRENT_API_VERSION; ici.enabledExtensionCount=3; ici.enabledExtensionNames=exts;
  XrResult r=xrCreateInstance(&ici,&inst); if(XR_FAILED(r)){printf("instance %d\n",r);return 1;}
  XrSystemGetInfo sgi={XR_TYPE_SYSTEM_GET_INFO,0,XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY}; XrSystemId sys; xrGetSystem(inst,&sgi,&sys);
  uint32_t n=0; xrEnumerateViewConfigurations(inst,sys,0,&n,NULL); XrViewConfigurationType t[8]; xrEnumerateViewConfigurations(inst,sys,8,&n,t); printf("view configs:"); for(uint32_t i=0;i<n;i++)printf(" %d",t[i]); printf("\n");
  XrSessionCreateInfo sci={XR_TYPE_SESSION_CREATE_INFO,0,0,sys}; XrSession ses; r=xrCreateSession(inst,&sci,&ses); if(XR_FAILED(r)){printf("session %d\n",r);return 1;}
  XrReferenceSpaceCreateInfo rsi={XR_TYPE_REFERENCE_SPACE_CREATE_INFO,0,XR_REFERENCE_SPACE_TYPE_LOCAL,{{0,0,0,1},{0,0,0}}}; XrSpace local; xrCreateReferenceSpace(ses,&rsi,&local);
  rsi.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW; XrSpace view; xrCreateReferenceSpace(ses,&rsi,&view);
  PFN_xrConvertTimespecTimeToTimeKHR conv; xrGetInstanceProcAddr(inst,"xrConvertTimespecTimeToTimeKHR",(PFN_xrVoidFunction*)&conv);
  int running=0; time_t t0=time(0); long k=0;
  while(time(0)-t0<secs){ XrEventDataBuffer ev={XR_TYPE_EVENT_DATA_BUFFER};
    while(xrPollEvent(inst,&ev)==XR_SUCCESS){ if(ev.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){XrEventDataSessionStateChanged*e=(void*)&ev; if(e->state==XR_SESSION_STATE_READY){XrSessionBeginInfo bi={XR_TYPE_SESSION_BEGIN_INFO,0,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO}; r=xrBeginSession(ses,&bi); printf("begin session %d\n",r); running=!XR_FAILED(r);}} ev.type=XR_TYPE_EVENT_DATA_BUFFER;}
    if(!running){usleep(50000);continue;}
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); XrTime now; conv(inst,&ts,&now);
    XrViewLocateInfo li={XR_TYPE_VIEW_LOCATE_INFO,0,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,now,view}; XrViewState vs={XR_TYPE_VIEW_STATE}; XrView v[4]; for(int i=0;i<4;i++){v[i].type=XR_TYPE_VIEW;v[i].next=0;} uint32_t c=0;
    r=xrLocateViews(ses,&li,&vs,4,&c,v); if(XR_FAILED(r)){printf("locate %d\n",r);usleep(500000);continue;}
    if(k++%30==0){ for(uint32_t i=0;i<c;i++){ float y,p; dir(v[i].pose.orientation,&y,&p);
      printf("view %u pos(%+.4f,%+.4f,%+.4f) yaw %+.1f pitch %+.1f  fov L%+.1f R%+.1f U%+.1f D%+.1f deg\n",i,v[i].pose.position.x,v[i].pose.position.y,v[i].pose.position.z,y,p,v[i].fov.angleLeft*57.3,v[i].fov.angleRight*57.3,v[i].fov.angleUp*57.3,v[i].fov.angleDown*57.3);} fflush(stdout); if(k>40) break;} usleep(16000);}
  return 0;}

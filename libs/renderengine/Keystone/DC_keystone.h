#ifndef _DC_DLOPEN_
#define _DC_DLOPEN_

#include <dlfcn.h>

#define PROP_KEYSTONE_TOP_LEFT "persist.sys.keystone.lt"
#define PROP_KEYSTONE_TOP_RIGHT "persist.sys.keystone.rt"
#define PROP_KEYSTONE_BOTTOM_LEFT "persist.sys.keystone.lb"
#define PROP_KEYSTONE_BOTTOM_RIGHT "persist.sys.keystone.rb"
#define PROP_KEYSTONE_UPDATE "persist.sys.keystone.update"
#define PROP_KEYSTONE_DISPLAY_ID "persist.sys.keystone.display.id"
#define PROP_KEYSTONE_DISPLAY_WIDTH "persist.sys.keystone.display.w"
#define PROP_KEYSTONE_DISPLAY_HEIGHT "persist.sys.keystone.display.h"
#define PROP_KEYSTONE_DISABLE "persist.sys.keystone.disable"
#define PROP_KEYSTONE_LOG "persist.sys.keystone.log"
#define PROP_KEYSTONE_RESET "persist.sys.keystone.reset"

#define PROP_SYS_BOOT_COMPLETED "sys.boot_completed"
#define PROP_KEYSTONE_BOOT_EFFECT "persist.sys.keystone.boot_effect"
#define PROP_KEYSTONE_MIRROR_X "persist.sys.keystone.mirror_x"
#define PROP_KEYSTONE_MIRROR_Y "persist.sys.keystone.mirror_y"
#define PROP_KEYSTONE_OFFSET_X "persist.sys.keystone.offset.x"
#define PROP_KEYSTONE_OFFSET_Y "persist.sys.keystone.offset.y"


namespace android {

typedef struct RKGFX_DC_keystone_offset_s {
    float lt_x;
    float lt_y;
    float lb_x;
    float lb_y;
    float rt_x;
    float rt_y;
    float rb_x;
    float rb_y;
} RKGFX_DC_keystone_offset_t;
RKGFX_DC_keystone_offset_t RKGFX_DC_keystone_defaultOffset = {0,0,0,0,0,0,0,0};
RKGFX_DC_keystone_offset_t * mKeystoneOffset = &RKGFX_DC_keystone_defaultOffset;

typedef struct RKGFX_DC_keystone_config_s {
    uint32_t mode;
    uint32_t mirror_x;
    uint32_t mirror_y;
    uint32_t bicubic_interpolation;
    uint32_t brightness_compensation;
    uint32_t mask_texture;
    uint32_t samples;
    uint32_t fd_in;
    uint32_t fd_out;
    float offset_x;
    float offset_y;
    float angle_x;
    float angle_y;
    char * buf_in;
    char * buf_out;
} RKGFX_DC_keystone_config_t;
RKGFX_DC_keystone_config_t RKGFX_DC_keystone_defaultConfig = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
RKGFX_DC_keystone_config_t * mKeystoneConfig = &RKGFX_DC_keystone_defaultConfig;


typedef struct RKGFX_DC_keystone_mesh_config_s {
    uint32_t mesh_enable;
    uint32_t mesh_w;
    uint32_t mesh_h;
    uint32_t need_mesh_file;
    char *   mesh_file_path;
} RKGFX_DC_keystone_mesh_config_t;
RKGFX_DC_keystone_mesh_config_t RKGFX_DC_keystone_defaultMeshConfig = {0,0,0,0,0};
RKGFX_DC_keystone_mesh_config_t * mKeystoneMeshConfig = &RKGFX_DC_keystone_defaultMeshConfig;


typedef struct RKGFX_DC_context_s {
    uint32_t versionCode;
    uint32_t disable;
    uint32_t displayTargetId;
    uint32_t displayId;
    uint32_t displayWidth;
    uint32_t displayHeight;
    uint32_t enableLog;
    uint32_t initPbuffer;
    RKGFX_DC_keystone_config_t * config;
    RKGFX_DC_keystone_mesh_config_t * mesh_config;
    RKGFX_DC_keystone_offset_t * offset_points;
}RKGFX_DC_context_t;
RKGFX_DC_context_t RKGFX_DC_keystone_defaultContext = {0,0,0,0,0,0,0,0,mKeystoneConfig,mKeystoneMeshConfig,mKeystoneOffset};
RKGFX_DC_context_t * mKeystoneContext = &RKGFX_DC_keystone_defaultContext;

typedef enum {
    RKGFX_DC_SUCCESS,
    RKGFX_DC_INVALID_PARAM,
    RKGFX_DC_FAILED,
} RKGFX_DC_STATUS;

RKGFX_DC_STATUS RKGFX_DC_getKeystoneConfig();


typedef RKGFX_DC_STATUS (* _RKGFX_DC_init) (void* context);
typedef RKGFX_DC_STATUS (* _RKGFX_DC_configure) (void* context);
typedef RKGFX_DC_STATUS (* _RKGFX_DC_prepare) (void* context);
typedef RKGFX_DC_STATUS (* _RKGFX_DC_process) (void* context);
typedef RKGFX_DC_STATUS (* _RKGFX_DC_updateProjectionMatrix) (void* context, mat4& projectionMatrix);
typedef RKGFX_DC_STATUS (* _RKGFX_DC_calculateMVP)(void* context, mat4 *mtx);
typedef RKGFX_DC_STATUS (* _RKGFX_DC_updatemesh)(void* context, RKGFX_DC_keystone_offset_t offset);
typedef RKGFX_DC_STATUS (* _RKGFX_DC_setMeshDimen) (void* context, uint32_t width, uint32_t height);
typedef RKGFX_DC_STATUS (* _RKGFX_DC_getMeshSize)(void* context, uint32_t& size, uint32_t& width, uint32_t& height);
typedef RKGFX_DC_STATUS (* _RKGFX_DC_getMeshBuffer)(void* context, float* meshbuffer);
typedef RKGFX_DC_STATUS (* _RKGFX_DC_deinit)(void* context);

#if defined(__LP64__)
#define LIBGRAPHIC_DC_PATH    "/system/lib64/librkgfx_dc.so"
#else
#define LIBGRAPHIC_DC_PATH    "/system/lib/librkgfx_dc.so"
#endif



static _RKGFX_DC_init RKGFX_DC_initp = NULL;
static _RKGFX_DC_configure RKGFX_DC_configure = NULL;
static _RKGFX_DC_prepare RKGFX_DC_prepare = NULL;
static _RKGFX_DC_process RKGFX_DC_process = NULL;
static _RKGFX_DC_updateProjectionMatrix RKGFX_DC_updateProjectionMatrix = NULL;
static _RKGFX_DC_calculateMVP RKGFX_DC_calculateMVPp = NULL;
static _RKGFX_DC_updatemesh RKGFX_DC_updatemesh = NULL;
static _RKGFX_DC_setMeshDimen RKGFX_DC_setMeshDimen = NULL;
static _RKGFX_DC_getMeshSize RKGFX_DC_getMeshSize = NULL;
static _RKGFX_DC_getMeshBuffer RKGFX_DC_getMeshBuffer = NULL;
static _RKGFX_DC_deinit RKGFX_DC_deinit = NULL;


RKGFX_DC_STATUS RKGFX_DC_dlopen()
{
    //dlopen
    void* dso = NULL;
    dso = dlopen(LIBGRAPHIC_DC_PATH, RTLD_NOW | RTLD_LOCAL);
    if (dso == 0) {
        ALOGE("RKGCC_DC can't not find %s ! error=%s \n",LIBGRAPHIC_DC_PATH,dlerror());
        return RKGFX_DC_FAILED;
    }

    if(RKGFX_DC_initp == NULL)
        RKGFX_DC_initp = (_RKGFX_DC_init)dlsym(dso, "_ZN7android13RKGFX_DC_initEPv");
    if(RKGFX_DC_initp == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }

    if(RKGFX_DC_configure == NULL)
        RKGFX_DC_configure = (_RKGFX_DC_configure)dlsym(dso, "_ZN7android18RKGFX_DC_configureEPv");
    if(RKGFX_DC_configure == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }

    if(RKGFX_DC_prepare == NULL)
        RKGFX_DC_prepare = (_RKGFX_DC_prepare)dlsym(dso, "_ZN7android16RKGFX_DC_prepareEPv");
    if(RKGFX_DC_prepare == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }

    if(RKGFX_DC_process == NULL)
        RKGFX_DC_process = (_RKGFX_DC_process)dlsym(dso, "_ZN7android16RKGFX_DC_processEPv");
    if(RKGFX_DC_process == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }


    if(RKGFX_DC_updateProjectionMatrix == NULL)
        RKGFX_DC_updateProjectionMatrix = (_RKGFX_DC_updateProjectionMatrix)dlsym(dso, "_ZN7android31RKGFX_DC_updateProjectionMatrixEPvRNS_6tmat44IfEE");
    if(RKGFX_DC_updateProjectionMatrix == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }

    if(RKGFX_DC_calculateMVPp == NULL)
        RKGFX_DC_calculateMVPp = (_RKGFX_DC_calculateMVP)dlsym(dso, "_ZN7android21RKGFX_DC_calculateMVPEPvPNS_6tmat44IfEE");
    if(RKGFX_DC_calculateMVPp == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }

    if(RKGFX_DC_updatemesh == NULL)
        RKGFX_DC_updatemesh = (_RKGFX_DC_updatemesh)dlsym(dso, "_ZN7android19RKGFX_DC_updatemeshEPvNS_26RKGFX_DC_keystone_offset_sE");
    if(RKGFX_DC_updatemesh == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }

    if(RKGFX_DC_setMeshDimen == NULL)
        RKGFX_DC_setMeshDimen = (_RKGFX_DC_setMeshDimen)dlsym(dso, "_ZN7android21RKGFX_DC_setMeshDimenEPvjj");
    if(RKGFX_DC_setMeshDimen == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }

    if(RKGFX_DC_getMeshSize == NULL)
        RKGFX_DC_getMeshSize = (_RKGFX_DC_getMeshSize)dlsym(dso, "_ZN7android20RKGFX_DC_getMeshSizeEPvRjS1_S1_");
    if(RKGFX_DC_getMeshSize == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }

    if(RKGFX_DC_getMeshBuffer == NULL)
        RKGFX_DC_getMeshBuffer = (_RKGFX_DC_getMeshBuffer)dlsym(dso, "_ZN7android22RKGFX_DC_getMeshBufferEPvPf");
    if(RKGFX_DC_getMeshBuffer == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }

    if(RKGFX_DC_deinit == NULL)
        RKGFX_DC_deinit = (_RKGFX_DC_deinit)dlsym(dso, "_ZN7android15RKGFX_DC_deinitEPv");
    if(RKGFX_DC_deinit == NULL)
    {
        ALOGE("RKGCC_DC[%s,%d] can't not find target function !\n",__FUNCTION__,__LINE__);
        dlclose(dso);
        return RKGFX_DC_FAILED;
    }

    return RKGFX_DC_SUCCESS;

}

EGLint const * RKGFX_DC_getMSAAEglConfig(void *context, EGLint const * attr)
{
    if(context == NULL)
        context = mKeystoneContext;

    int i = 0;
    while(attr[i] != EGL_NONE)
    {
        //ALOGE("RKGFX_DC_DEBUG[%s %d] i:%d [0x%x %d] \n",__FUNCTION__,__LINE__,i, attr[i],attr[i+1]);
        i+=2;
    }
    if(i > 0 && mKeystoneContext->config->samples > 0) {
        EGLint * newAttr = (EGLint *)malloc(sizeof(EGLint)*(i+4));
        memcpy(newAttr,attr,sizeof(EGLint)*(i));
        newAttr[i] = EGL_SAMPLES;
        newAttr[i+1] = 1;
        newAttr[i+2] = EGL_SAMPLE_BUFFERS;
        newAttr[i+3] = mKeystoneContext->config->samples;
        newAttr[i+4] = EGL_NONE;

        return newAttr;
    }else {
        return attr;
    }
}

RKGFX_DC_STATUS RKGFX_DC_init(void* context)
{
    if(RKGFX_DC_dlopen() != RKGFX_DC_SUCCESS)
    {
        return RKGFX_DC_FAILED;
    }

    if(context == NULL)
        context = mKeystoneContext;

    property_set(PROP_KEYSTONE_UPDATE,"1");
    RKGFX_DC_getKeystoneConfig();

    RKGFX_DC_STATUS DC_status = RKGFX_DC_initp(context);
    if(DC_status != RKGFX_DC_SUCCESS)
    {
        return DC_status;
    }

    ALOGD("RKGFX_DC_init success\n");
    return RKGFX_DC_SUCCESS;
}


int getPropValueInt(const char *key, const char *def) {
    char value[PROPERTY_VALUE_MAX];
    property_get(key, value, def);
    int val = atoi(value);
    return val;
}

float getPropValueFloat(const char *key, const char *def) {
    char value[PROPERTY_VALUE_MAX];
    property_get(key, value, def);
    double val = atof(value);
    return (float)val;
}
// get one KeyStone vertex position property value
bool getKeystonePropVal(const char *key, float *x, float *y, const char *def) {
    char value[PROPERTY_VALUE_MAX];
    const char *token = ",";
    char *p1;
    char *p2;
    // get property
    property_get(key, value, def);
    ALOGD("getKeyStonePropVal key: %s, value: %s", key, value);
    // split value
    p1 = strtok(value, token);
    p2 = strtok(NULL, token);
    if (p1 == NULL || p2 == NULL) {
        return false;
    }
    // get float value
    *x = (float)atoi(p1);
    *y = (float)atoi(p2);
    ALOGD("%s v(%f, %f)", key, *x, *y);
    return true;
}

char meshPath[PROPERTY_VALUE_MAX];
RKGFX_DC_STATUS RKGFX_DC_getKeystoneConfig()
{

    int update = getPropValueInt(PROP_KEYSTONE_UPDATE,"1");
    if(1 == update)
    {
        property_set(PROP_KEYSTONE_UPDATE,"0");

        mKeystoneContext->displayTargetId = getPropValueInt(PROP_KEYSTONE_DISPLAY_ID,"0");
        mKeystoneContext->disable = getPropValueInt(PROP_KEYSTONE_DISABLE,"0");
        mKeystoneContext->enableLog = getPropValueInt(PROP_KEYSTONE_LOG,"0");

        int keystoneReset = getPropValueInt(PROP_KEYSTONE_RESET,"0");
        if(keystoneReset == 1)
        {
            property_set(PROP_KEYSTONE_RESET,"0");
            property_set(PROP_KEYSTONE_TOP_LEFT,"0,0");
            property_set(PROP_KEYSTONE_TOP_RIGHT,"0,0");
            property_set(PROP_KEYSTONE_BOTTOM_LEFT,"0,0");
            property_set(PROP_KEYSTONE_BOTTOM_RIGHT,"0,0");
            if((int)mKeystoneContext->enableLog) {
                ALOGD("librkgfx_dc: (RKGFX_DC_getKeystoneConfig|%d) reset all point (0,0) \n", __LINE__);
            }
        }


        RKGFX_DC_keystone_defaultConfig.mirror_x = getPropValueInt(PROP_KEYSTONE_MIRROR_X,"0");
        RKGFX_DC_keystone_defaultConfig.mirror_y = getPropValueInt(PROP_KEYSTONE_MIRROR_Y,"0");

#if AFTER_ANDROID10
        getKeystonePropVal(PROP_KEYSTONE_BOTTOM_LEFT,&(RKGFX_DC_keystone_defaultOffset.lt_x),&(RKGFX_DC_keystone_defaultOffset.lt_y),"0,0");
        getKeystonePropVal(PROP_KEYSTONE_BOTTOM_RIGHT,&(RKGFX_DC_keystone_defaultOffset.rt_x),&(RKGFX_DC_keystone_defaultOffset.rt_y),"0,0");
        getKeystonePropVal(PROP_KEYSTONE_TOP_LEFT,&(RKGFX_DC_keystone_defaultOffset.lb_x),&(RKGFX_DC_keystone_defaultOffset.lb_y),"0,0");
        getKeystonePropVal(PROP_KEYSTONE_TOP_RIGHT,&(RKGFX_DC_keystone_defaultOffset.rb_x),&(RKGFX_DC_keystone_defaultOffset.rb_y),"0,0");

#else
        getKeystonePropVal(PROP_KEYSTONE_TOP_LEFT,&(RKGFX_DC_keystone_defaultOffset.lt_x),&(RKGFX_DC_keystone_defaultOffset.lt_y),"0,0");
        getKeystonePropVal(PROP_KEYSTONE_TOP_RIGHT,&(RKGFX_DC_keystone_defaultOffset.rt_x),&(RKGFX_DC_keystone_defaultOffset.rt_y),"0,0");
        getKeystonePropVal(PROP_KEYSTONE_BOTTOM_LEFT,&(RKGFX_DC_keystone_defaultOffset.lb_x),&(RKGFX_DC_keystone_defaultOffset.lb_y),"0,0");
        getKeystonePropVal(PROP_KEYSTONE_BOTTOM_RIGHT,&(RKGFX_DC_keystone_defaultOffset.rb_x),&(RKGFX_DC_keystone_defaultOffset.rb_y),"0,0");
#endif

    }


    return RKGFX_DC_SUCCESS;
}

void RKGFX_DC_calculateMVP(void* context, int display_id, mat4 *mtx)
{
    if(context == NULL)
        context = mKeystoneContext;

    RKGFX_DC_context_t * pcontext = (RKGFX_DC_context_t *)context;

    RKGFX_DC_getKeystoneConfig();
    if((int)pcontext->displayTargetId == display_id) {
        RKGFX_DC_calculateMVPp(context, mtx);
    }else {
        if((int)pcontext->enableLog) {
            ALOGD("librkgfx_dc: (RKGFX_DC_calculateMVP|%d) displayTargetId(%d) != display_id(%d) !!!\n", __LINE__, (int)pcontext->displayTargetId, display_id);
        }
    }

}



} /*namespace android */


#endif /* _DC_DLOPEN_ */

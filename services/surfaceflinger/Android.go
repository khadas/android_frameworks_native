
package SurfaceFlinger

import (
        "android/soong/android"
        "android/soong/cc"
        "fmt"
        "strings"
)

func init() {
    fmt.Println("SurfaceFlinger want to conditional Compile")
    android.RegisterModuleType("cc_SurfaceFlinger", DefaultsFactory)
}

func DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, Defaults)
    return module
}

func Defaults(ctx android.LoadHookContext) {
    type props struct {
        Cflags []string
    }
    p := &props{}
    p.Cflags = globalDefaults(ctx)
    ctx.AppendProperties(p)
}

func globalDefaults(ctx android.BaseContext) ([]string) {
    var cppflags []string

    fmt.Println("TARGET_PRODUCT:",ctx.AConfig().Getenv("TARGET_PRODUCT"))

    if (strings.Contains(ctx.AConfig().Getenv("TARGET_PRODUCT"),"rk3288")) {
        cppflags = append(cppflags,
           "-DRK_NV12_10_TO_NV12_BY_RGA=0",
           "-DRK_NV12_10_TO_NV12_BY_NENO=1")
    }else if (strings.Contains(ctx.AConfig().Getenv("TARGET_PRODUCT"),"rk3399")){
        cppflags = append(cppflags,
            "-DRK_HDR=1")
    }else{
        cppflags = append(cppflags,
            "-DRK_NV12_10_TO_NV12_BY_RGA=1",
            "-DRK_NV12_10_TO_NV12_BY_NENO=0")
    }
    return cppflags
}

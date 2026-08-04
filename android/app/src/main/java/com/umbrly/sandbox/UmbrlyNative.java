package com.umbrly.sandbox;

final class UmbrlyNative {
    static {
        System.loadLibrary("umbrly_android");
    }

    private UmbrlyNative() {
    }

    static native String runScript(String source, String input);
}

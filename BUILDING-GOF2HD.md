# Building the GOF2 HD (2012 Android) Vita port

Toolchain: VitaSDK **softfp** (vitasdk-softfp autobuilds), packages from
https://github.com/vitasdk-softfp/packages, plus:

* **vitaShaRK**: built from current source (`make`), needed by current vitaGL.
* **vitaGL**: built from current source with the boot splash screen disabled:

      make SOFTFP_ABI=1 NO_DEBUG=1 NO_SPLASHSCREEN=1

  vitaGL's animated splash ignores swaps until rendering starts and never
  handed over in this port (frozen "vitaGL" screen / black screen).

Then:

    cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-std=gnu17 -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration"
    cmake --build build -j

Data: the repacked 2012 APK (libgof2hdaa.so SHA1 F1F8904B...) as
`ux0:data/gof2hd/GalaxyOnFire.apk`.

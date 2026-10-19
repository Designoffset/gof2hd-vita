/*
 * falojni_impl.c
 *
 * Implementations for JNI functions and fields to be used by FalsoJNI.
 *
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <FalsoJNI/FalsoJNI_Impl.h>

// GOF2 HD 2012 build: methods the native code calls back into Java.
// DeviceInfo
jint DeviceInfo_getDisplayWidth(jmethodID id, va_list args) { return 960; }
jint DeviceInfo_getDisplayHeight(jmethodID id, va_list args) { return 544; }
jboolean DeviceInfo_isPad(jmethodID id, va_list args) { return JNI_FALSE; }

// NativeFunctionCalls: store / social / web links. None of these exist on the
// Vita, so they are accepted and ignored.
void NativeFunctionCalls_noop(jmethodID id, va_list args) { }

NameToMethodID nameToMethodId[] = {
    { 1, "getDisplayWidth", METHOD_TYPE_INT },
    { 2, "getDisplayHeight", METHOD_TYPE_INT },
    { 3, "isPad", METHOD_TYPE_BOOLEAN },
    { 10, "free_credits_followOnTwitter", METHOD_TYPE_VOID },
    { 11, "free_credits_likeFishlabsOnFacebook", METHOD_TYPE_VOID },
    { 12, "free_credits_likeGOF2OnFacebook", METHOD_TYPE_VOID },
    { 13, "free_credits_open_offerwall", METHOD_TYPE_VOID },
    { 14, "free_credits_rateGame", METHOD_TYPE_VOID },
    { 15, "free_credits_subscribeToYoutubeChannel", METHOD_TYPE_VOID },
    { 16, "free_credits_watchVideo", METHOD_TYPE_VOID },
    { 17, "iap_buy_dlc_full_package", METHOD_TYPE_VOID },
    { 18, "iap_buy_dlc_kaamo_club", METHOD_TYPE_VOID },
    { 19, "iap_buy_dlc_supernova", METHOD_TYPE_VOID },
    { 20, "iap_buy_dlc_valkyrie", METHOD_TYPE_VOID },
    { 21, "iap_buy_dlc_vip", METHOD_TYPE_VOID },
    { 22, "iap_restore_purchases", METHOD_TYPE_VOID },
    { 23, "openPrivacyPolicy", METHOD_TYPE_VOID },
    { 24, "openTermsOfService", METHOD_TYPE_VOID },
    { 25, "rateGame", METHOD_TYPE_VOID },
    { 26, "showMoreGames", METHOD_TYPE_VOID },
};

MethodsBoolean methodsBoolean[] = {
    { 3, DeviceInfo_isPad },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};
MethodsInt methodsInt[] = {
    { 1, DeviceInfo_getDisplayWidth },
    { 2, DeviceInfo_getDisplayHeight },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 10, NativeFunctionCalls_noop }, { 11, NativeFunctionCalls_noop },
    { 12, NativeFunctionCalls_noop }, { 13, NativeFunctionCalls_noop },
    { 14, NativeFunctionCalls_noop }, { 15, NativeFunctionCalls_noop },
    { 16, NativeFunctionCalls_noop }, { 17, NativeFunctionCalls_noop },
    { 18, NativeFunctionCalls_noop }, { 19, NativeFunctionCalls_noop },
    { 20, NativeFunctionCalls_noop }, { 21, NativeFunctionCalls_noop },
    { 22, NativeFunctionCalls_noop }, { 23, NativeFunctionCalls_noop },
    { 24, NativeFunctionCalls_noop }, { 25, NativeFunctionCalls_noop },
    { 26, NativeFunctionCalls_noop },
};

NameToFieldID nameToFieldId[] = {};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {};
FieldsObject fieldsObject[] = {};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES

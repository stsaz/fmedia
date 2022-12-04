/** JNI wrapper
2022, Simon Zolin */

/*
jni_local_unref
Meta:
	jni_class jni_class_obj
	jni_field
String:
	jni_js_sz
	jni_sz_js jni_sz_free
Arrays:
	jni_arr_len
	jni_bytes_jba jni_bytes_free
	jni_joa
	jni_joa_i jni_joa_i_set
	jni_jsa_sza
	jni_vec_jba
Object:
	jni_obj_jo_set
	jni_obj_sz_set
	jni_obj_long jni_obj_long_set

Notes:
	sz	char*
	js	jstring
	jo	jobject
	joa	jobjectArray
	jba	jobjectArray[jbyte]
	jsa	jobjectArray[String]
*/

#pragma once
#include <jni.h>

#define JNI_CSTR "java/lang/String"

#define JNI_TSTR "Ljava/lang/String;"
#define JNI_TOBJ "Ljava/lang/Object;"
#define JNI_TINT "I"
#define JNI_TLONG "J"
#define JNI_TARR "["

#define jni_local_unref(jobj) \
	(*env)->DeleteLocalRef(env, jobj)

/** jclass = class(JNI_C...) */
#define jni_class(C) \
	(*env)->FindClass(env, C)

/** jclass = typeof(obj) */
#define jni_class_obj(jobj) \
	(*env)->GetObjectClass(env, jobj)

/** jfieldID = jclass::field
T: JNI_T... */
#define jni_field(jclazz, name, T) \
	(*env)->GetFieldID(env, jclazz, name, T)


/** jstring = char* */
#define jni_js_sz(sz) \
	(*env)->NewStringUTF(env, sz)

/** char* = jstring */
#define jni_sz_js(js) \
	(*env)->GetStringUTFChars(env, js, NULL)

#define jni_sz_free(sz, js) \
	(*env)->ReleaseStringUTFChars(env, js, sz)


#define jni_arr_len(ja) \
	(*env)->GetArrayLength(env, ja)

/** ffbyte* = byte[] */
#define jni_bytes_jba(jab) \
	(*env)->GetByteArrayElements(env, jab, NULL)

#define jni_bytes_free(ptr, jab) \
	(*env)->ReleaseByteArrayElements(env, jab, ptr, 0)

/** jobjectArray = new */
#define jni_joa(cap, jclazz) \
	(*env)->NewObjectArray(env, cap, jclazz, NULL)

/** jobject = array[i] */
#define jni_joa_i(ja, i) \
	(*env)->GetObjectArrayElement(env, ja, i)

/** array[i] = jobject */
#define jni_joa_i_set(ja, i, val) \
	(*env)->SetObjectArrayElement(env, ja, i, val)

/** ffvec = byte[] */
static inline ffvec jni_vec_jba(JNIEnv *env, jbyteArray *jab)
{
	ffvec v = {};
	ffvec_alloc(&v, jni_arr_len(jab) + 1, 1);
	jbyte *b = jni_bytes_jba(jab);
	v.len = v.cap - 1;
	ffmem_copy(v.ptr, b, v.len);
	jni_bytes_free(b, jab);
	return v;
}

/** String[] = char*[] */
static inline jobjectArray jni_jsa_sza(JNIEnv *env, char **asz, ffsize n)
{
	jclass cstr = jni_class(JNI_CSTR);
	jobjectArray jas = jni_joa(n, cstr);
	for (ffsize i = 0;  i != n;  i++) {
		char *s = asz[i];
		jstring js = jni_js_sz(s);
		jni_joa_i_set(jas, i, js);
		jni_local_unref(js);
	}
	return jas;
}


/** obj.object = VAL */
#define	jni_obj_jo_set(jobj, jfield, val) \
	(*env)->SetObjectField(env, jobj, jfield, val)

/** obj.string = sz */
static inline void jni_obj_sz_set(JNIEnv *env, jobject jo, jfieldID jf, const char *sz)
{
	jstring js = jni_js_sz(sz);
	jni_obj_jo_set(jo, jf, js);
	jni_local_unref(js);
}

/** long = obj.long */
#define jni_obj_long(jobj, jfield) \
	(*env)->GetLongField(env, jobj, jfield)

/** obj.long = VAL */
#define	jni_obj_long_set(jobj, jfield, val) \
	(*env)->SetLongField(env, jobj, jfield, val)
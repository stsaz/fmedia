/** JNI wrapper
2022, Simon Zolin */

/*
jni_local_unref
jni_class
jni_class_obj
jni_field
jni_obj_long jni_obj_long_set
jni_str_sz
jni_sz_str jni_sz_free
jni_arr
jni_arr_len
jni_arr_obj jni_arr_obj_set
jni_astr_asz
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

/**
C: JNI_C... */
#define /*jclass*/ jni_class(C) \
	(*env)->FindClass(env, C)

#define /*jclass*/ jni_class_obj(jobj) \
	(*env)->GetObjectClass(env, jobj)

/**
T: JNI_T... */
#define /*jfieldID*/ jni_field(jclazz, name, T) \
	(*env)->GetFieldID(env, jclazz, name, T)

/** VAL = obj.long */
#define	/*long*/ jni_obj_long(jobj, jfield) \
	(*env)->GetLongField(env, jobj, jfield)

/** obj.long = VAL */
#define	jni_obj_long_set(jobj, jfield, val) \
	(*env)->SetLongField(env, jobj, jfield, val)


/** char* -> String */
#define /*jstring*/ jni_str_sz(sz) \
	(*env)->NewStringUTF(env, sz)

/** String -> char* */
#define /* char* */ jni_sz_str(js) \
	(*env)->GetStringUTFChars(env, js, NULL)

#define jni_sz_free(sz, js) \
	(*env)->ReleaseStringUTFChars(env, js, sz)


#define /*jobjectArray*/ jni_arr(cap, jclazz) \
	(*env)->NewObjectArray(env, cap, jclazz, NULL)

#define jni_arr_len(ja) \
	(*env)->GetArrayLength(env, ja)

/** OBJ = array[i] */
#define /*jobject*/ jni_arr_obj(ja, i) \
	(*env)->GetObjectArrayElement(env, ja, i)

/** array[i] = OBJ */
#define jni_arr_obj_set(ja, i, val) \
	(*env)->SetObjectArrayElement(env, ja, i, val)

/** char*[] -> String[] */
static inline jobjectArray jni_astr_asz(JNIEnv *env, char **asz, ffsize n)
{
	jclass cstr = jni_class(JNI_CSTR);
	jobjectArray jas = jni_arr(n, cstr);
	for (ffsize i = 0;  i != n;  i++) {
		char *s = asz[i];
		jstring js = jni_str_sz(s);
		jni_arr_obj_set(jas, i, js);
		jni_local_unref(js);
	}
	return jas;
}

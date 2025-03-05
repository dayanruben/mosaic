#include "cutils.h"
#include "jni.h"
#include "mosaic.h"
#include <stdlib.h>
#include <string.h>

static void throwIse(JNIEnv *env, uint32_t error) {
	jclass ise = (*env)->FindClass(env, "java/lang/IllegalStateException");

	// 11 == max unsigned digit length (10) + null termination byte (1)
	char *message = malloc(11 * sizeof(char));
	if (message) {
		sprintf(message, "%u", error);
	}
	(*env)->ThrowNew(env, ise, message);
}

typedef struct MosaicJniTtyCallback {
	JNIEnv *env;
	jobject instance;
	jmethodID onFocus;
	jmethodID onKey;
	jmethodID onMouse;
	jmethodID onResize;
} MosaicJniTtyCallback;

static void invokeOnFocusCallback(void *opaque, bool focused) {
	MosaicJniTtyCallback *callback = (MosaicJniTtyCallback *) opaque;
	(*callback->env)->CallVoidMethod(
		callback->env,
		callback->instance,
		callback->onFocus,
		focused
	);
}

static void invokeOnKeyCallback(void *opaque) {
	MosaicJniTtyCallback *callback = (MosaicJniTtyCallback *) opaque;
	(*callback->env)->CallVoidMethod(
		callback->env,
		callback->instance,
		callback->onKey
	);
}

static void invokeOnMouseCallback(void *opaque) {
	MosaicJniTtyCallback *callback = (MosaicJniTtyCallback *) opaque;
	(*callback->env)->CallVoidMethod(
		callback->env,
		callback->instance,
		callback->onMouse
	);
}

static void invokeOnResizeCallback(void *opaque, int columns, int rows, int width, int height) {
	MosaicJniTtyCallback *callback = (MosaicJniTtyCallback *) opaque;
	(*callback->env)->CallVoidMethod(
		callback->env,
		callback->instance,
		callback->onResize,
		columns,
		rows,
		width,
		height
	);
}

JNIEXPORT jlong JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyCallbackInit(
	JNIEnv *env,
	jclass type UNUSED,
	jobject instance
) {
	jobject globalInstance = (*env)->NewGlobalRef(env, instance);
	if (unlikely(globalInstance == NULL)) {
		return 0;
	}
	jclass clazz = (*env)->FindClass(env, "com/jakewharton/mosaic/tty/Tty$Callback");
	if (unlikely(clazz == NULL)) {
		return 0;
	}
	jmethodID onFocus = (*env)->GetMethodID(env, clazz, "onFocus", "(Z)V");
	if (unlikely(onFocus == NULL)) {
		return 0;
	}
	jmethodID onKey = (*env)->GetMethodID(env, clazz, "onKey", "()V");
	if (unlikely(onKey == NULL)) {
		return 0;
	}
	jmethodID onMouse = (*env)->GetMethodID(env, clazz, "onMouse", "()V");
	if (unlikely(onMouse == NULL)) {
		return 0;
	}
	jmethodID onResize = (*env)->GetMethodID(env, clazz, "onResize", "(IIII)V");
	if (unlikely(onResize == NULL)) {
		return 0;
	}

	MosaicJniTtyCallback *jniCallback = malloc(sizeof(MosaicJniTtyCallback));
	if (unlikely(!jniCallback)) {
		return 0;
	}
	jniCallback->env = env;
	jniCallback->instance = globalInstance;
	jniCallback->onFocus = onFocus;
	jniCallback->onKey = onKey;
	jniCallback->onMouse = onMouse;
	jniCallback->onResize = onResize;

	MosaicTtyCallback *callback = malloc(sizeof(MosaicTtyCallback));
	if (unlikely(!callback)) {
		return 0;
	}
	callback->opaque = jniCallback;
	callback->onFocus = invokeOnFocusCallback;
	callback->onKey = invokeOnKeyCallback;
	callback->onMouse = invokeOnMouseCallback;
	callback->onResize = invokeOnResizeCallback;

	return (jlong) callback;
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyCallbackFree(
	JNIEnv *env,
	jclass type UNUSED,
	jlong callbackOpaque
) {
	MosaicTtyCallback *callback = (MosaicTtyCallback *) callbackOpaque;
	MosaicJniTtyCallback *jniCallback = callback->opaque;
	jobject instance = jniCallback->instance;
	free(callback);
	free(jniCallback);
	(*env)->DeleteGlobalRef(env, instance);
}

JNIEXPORT jlong JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyInit(
	JNIEnv *env,
	jclass type UNUSED
) {
	MosaicTtyInitResult result = tty_init();
	if (likely(!result.error)) {
		return (jlong) result.tty;
	}

	// This throw can fail, but the only condition that should cause that is OOM which
	// will occur from returning 0 (which is otherwise ignored if the throw succeeds).
	throwIse(env, result.error);
	return 0;
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttySetCallback(
	JNIEnv *env UNUSED,
	jclass type UNUSED,
	jlong ttyOpaque,
	jlong callbackOpaque
) {
	MosaicTty *tty = (MosaicTty *) ttyOpaque;
	MosaicTtyCallback *callback = (MosaicTtyCallback *) callbackOpaque;
	tty_setCallback(tty, callback);
}

JNIEXPORT jint JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyReadInput(
	JNIEnv *env,
	jclass type UNUSED,
	jlong ttyOpaque,
	jbyteArray buffer,
	jint offset,
	jint count
) {
	jbyte *bufferElements = (*env)->GetByteArrayElements(env, buffer, NULL);
	jbyte *bufferElementsAtOffset = bufferElements + offset;
	// Reinterpret JVM signed bytes as unsigned.
	uint8_t *nativeBufferAtOffset = (uint8_t *) bufferElementsAtOffset;

	MosaicTty *tty = (MosaicTty *) ttyOpaque;
	MosaicTtyIoResult result = tty_readInput(tty, nativeBufferAtOffset, count);

	(*env)->ReleaseByteArrayElements(env, buffer, bufferElements, 0);

	if (likely(!result.error)) {
		return result.count;
	}

	// This throw can fail, but the only condition that should cause that is OOM. Return -1 (EOF)
	// and should cause the program to try and exit cleanly. 0 is a valid return value.
	throwIse(env, result.error);
	return -1;
}

JNIEXPORT jint JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyReadInputWithTimeout(
	JNIEnv *env,
	jclass type UNUSED,
	jlong ttyOpaque,
	jbyteArray buffer,
	jint offset,
	jint count,
	jint timeoutMillis
) {
	jbyte *bufferElements = (*env)->GetByteArrayElements(env, buffer, NULL);
	jbyte *bufferElementsAtOffset = bufferElements + offset;
	// Reinterpret JVM signed bytes as unsigned.
	uint8_t *nativeBufferAtOffset = (uint8_t *) bufferElementsAtOffset;

	MosaicTty *tty = (MosaicTty *) ttyOpaque;
	MosaicTtyIoResult result = tty_readInputWithTimeout(
		tty,
		nativeBufferAtOffset,
		count,
		timeoutMillis
	);

	(*env)->ReleaseByteArrayElements(env, buffer, bufferElements, 0);

	if (likely(!result.error)) {
		return result.count;
	}

	// This throw can fail, but the only condition that should cause that is OOM. Return -1 (EOF)
	// and should cause the program to try and exit cleanly. 0 is a valid return value.
	throwIse(env, result.error);
	return -1;
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyInterruptRead(
	JNIEnv *env,
	jclass type UNUSED,
	jlong ttyOpaque
) {
	MosaicTty *tty = (MosaicTty *) ttyOpaque;
	uint32_t error = tty_interruptRead(tty);
	if (unlikely(error)) {
		throwIse(env, error);
	}
}

JNIEXPORT jint JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyWriteOutput(
	JNIEnv *env,
	jclass type UNUSED,
	jlong ttyOpaque,
	jbyteArray buffer,
	jint offset,
	jint count
) {
	jbyte *bufferElements = (*env)->GetByteArrayElements(env, buffer, NULL);
	jbyte *bufferElementsAtOffset = bufferElements + offset;
	// Reinterpret JVM signed bytes as unsigned.
	uint8_t *nativeBufferAtOffset = (uint8_t *) bufferElementsAtOffset;

	MosaicTty *tty = (MosaicTty *) ttyOpaque;
	MosaicTtyIoResult result = tty_writeOutput(tty, nativeBufferAtOffset, count);

	(*env)->ReleaseByteArrayElements(env, buffer, bufferElements, 0);

	if (likely(!result.error)) {
		return result.count;
	}

	// This throw can fail, but the only condition that should cause that is OOM. Return -1 (EOF)
	// and should cause the program to try and exit cleanly. 0 is a valid return value.
	throwIse(env, result.error);
	return -1;
}

JNIEXPORT jint JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyWriteError(
	JNIEnv *env,
	jclass type UNUSED,
	jlong ttyOpaque,
	jbyteArray buffer,
	jint offset,
	jint count
) {
	jbyte *bufferElements = (*env)->GetByteArrayElements(env, buffer, NULL);
	jbyte *bufferElementsAtOffset = bufferElements + offset;
	// Reinterpret JVM signed bytes as unsigned.
	uint8_t *nativeBufferAtOffset = (uint8_t *) bufferElementsAtOffset;

	MosaicTty *tty = (MosaicTty *) ttyOpaque;
	MosaicTtyIoResult result = tty_writeError(tty, nativeBufferAtOffset, count);

	(*env)->ReleaseByteArrayElements(env, buffer, bufferElements, 0);

	if (likely(!result.error)) {
		return result.count;
	}

	// This throw can fail, but the only condition that should cause that is OOM. Return -1 (EOF)
	// and should cause the program to try and exit cleanly. 0 is a valid return value.
	throwIse(env, result.error);
	return -1;
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyEnableRawMode(
	JNIEnv *env,
	jclass type UNUSED,
	jlong ttyOpaque
) {
	MosaicTty *tty = (MosaicTty *) ttyOpaque;
	uint32_t error = tty_enableRawMode(tty);
	if (unlikely(error)) {
		throwIse(env, error);
	}
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyEnableWindowResizeEvents(
	JNIEnv *env,
	jclass type UNUSED,
	jlong ttyOpaque
) {
	MosaicTty *tty = (MosaicTty *) ttyOpaque;
	uint32_t error = tty_enableWindowResizeEvents(tty);
	if (unlikely(error)) {
		throwIse(env, error);
	}
}

JNIEXPORT jintArray JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyCurrentSize(
	JNIEnv *env,
	jclass type UNUSED,
	jlong ttyOpaque
) {
	MosaicTty *tty = (MosaicTty *) ttyOpaque;
	MosaicTtyTerminalSizeResult result = tty_currentTerminalSize(tty);
	if (likely(!result.error)) {
		jintArray ints = (*env)->NewIntArray(env, 4);
		jint *intsPtr = (*env)->GetIntArrayElements(env, ints, NULL);
		intsPtr[0] = result.columns;
		intsPtr[1] = result.rows;
		intsPtr[2] = result.width;
		intsPtr[3] = result.height;
		(*env)->ReleaseIntArrayElements(env, ints, intsPtr, 0);
		return ints;
	}

	throwIse(env, result.error);
	return NULL;
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_ttyFree(
	JNIEnv *env,
	jclass type UNUSED,
	jlong ttyOpaque
) {
	MosaicTty *tty = (MosaicTty *) ttyOpaque;
	uint32_t error = tty_free(tty);
	if (unlikely(error)) {
		throwIse(env, error);
	}
}

JNIEXPORT jlong JNICALL
Java_com_jakewharton_mosaic_tty_Jni_testTtyInit(
	JNIEnv *env,
	jclass type UNUSED
) {
	MosaicTestTtyInitResult result = testTty_init();
	if (likely(!result.error)) {
		return (jlong) result.testTty;
	}

	// This throw can fail, but the only condition that should cause that is OOM which
	// will occur from returning 0 (which is otherwise ignored if the throw succeeds).
	throwIse(env, result.error);
	return 0;
}

JNIEXPORT jint JNICALL
Java_com_jakewharton_mosaic_tty_Jni_testTtyWriteInput(
	JNIEnv *env,
	jclass type UNUSED,
	jlong testTtyOpaque,
	jbyteArray buffer,
	jint offset,
	jint count
) {
	jbyte *bufferElements = (*env)->GetByteArrayElements(env, buffer, NULL);
	jbyte *bufferElementsAtOffset = bufferElements + offset;
	// Reinterpret JVM signed bytes as unsigned.
	uint8_t *nativeBufferAtOffset = (uint8_t *) bufferElementsAtOffset;

	MosaicTestTty *testTty = (MosaicTestTty *) testTtyOpaque;
	MosaicTtyIoResult result = testTty_writeInput(testTty, nativeBufferAtOffset, count);

	(*env)->ReleaseByteArrayElements(env, buffer, bufferElements, 0);

	if (likely(!result.error)) {
		return result.count;
	}

	// This throw can fail, but the only condition that should cause that is OOM. Return -1 (EOF)
	// and should cause the program to try and exit cleanly.
	throwIse(env, result.error);
	return -1;
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_testTtyFocusEvent(
	JNIEnv *env UNUSED,
	jclass type UNUSED,
	jlong testTtyOpaque,
	bool focused
) {
	MosaicTestTty *testTty = (MosaicTestTty *) testTtyOpaque;
	testTty_focusEvent(testTty, focused);
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_testTtyKeyEvent(
	JNIEnv *env UNUSED,
	jclass type UNUSED,
	jlong testTtyOpaque
) {
	MosaicTestTty *testTty = (MosaicTestTty *) testTtyOpaque;
	testTty_keyEvent(testTty);
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_testTtyMouseEvent(
	JNIEnv *env UNUSED,
	jclass type UNUSED,
	jlong testTtyOpaque
) {
	MosaicTestTty *testTty = (MosaicTestTty *) testTtyOpaque;
	testTty_mouseEvent(testTty);
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_testTtyResizeEvent(
	JNIEnv *env UNUSED,
	jclass type UNUSED,
	jlong testTtyOpaque,
	jint columns,
	jint rows,
	jint width,
	jint height
) {
	MosaicTestTty *testTty = (MosaicTestTty *) testTtyOpaque;
	testTty_resizeEvent(testTty, columns, rows, width, height);
}

JNIEXPORT jlong JNICALL
Java_com_jakewharton_mosaic_tty_Jni_testTtyGetTty(
	JNIEnv *env UNUSED,
	jclass type UNUSED,
	jlong testTtyOpaque
) {
	MosaicTestTty *testTty = (MosaicTestTty *) testTtyOpaque;
	return (jlong) testTty_getTty(testTty);
}

JNIEXPORT void JNICALL
Java_com_jakewharton_mosaic_tty_Jni_testTtyFree(
	JNIEnv *env UNUSED,
	jclass type UNUSED,
	jlong testTtyOpaque
) {
	MosaicTestTty *testTty = (MosaicTestTty *) testTtyOpaque;
	uint32_t error = testTty_free(testTty);
	if (unlikely(error)) {
		throwIse(env, error);
	}
}

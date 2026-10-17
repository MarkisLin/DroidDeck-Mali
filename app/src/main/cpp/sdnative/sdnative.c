#include <jni.h>
#include <stdatomic.h>

JNIEXPORT void JNICALL
Java_dev_hayash1_steamoslite_input_FakeInputWriter_nativeStoreFence(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    atomic_thread_fence(memory_order_release);
}

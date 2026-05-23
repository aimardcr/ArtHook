// SPDX-License-Identifier: Apache-2.0
//
// Builds a minimal DEX at runtime with one `public static native` method,
// injects it via InMemoryDexClassLoader, and returns the method's
// ArtMethod* for Layout.cpp to read its quick entry. Class/method names
// are randomized per process (DEX layout requires fixed lengths — content
// is random). The class is dropped immediately after we read the pointer.

#include "probe/Probe.h"

#include <fcntl.h>
#include <jni.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include "art/ArtMethod.h"
#include "util/Log.h"

namespace arthook {

namespace {

// ---- Randomized class / method names ------------------------------------
// Lengths are fixed by the DEX layout below; content is random lowercase
// ASCII picked once per process.

struct ProbeNames {
    char pkg[8];          // 7 chars + nul
    char cls[6];          // 5 chars + nul
    char method[6];       // 5 chars + nul
    char descriptor[16];  // "L<pkg>/<cls>;"  — 15 chars + nul
    char dotted[14];      // "<pkg>.<cls>"    — 13 chars + nul
};

void FillRandom(uint8_t* dst, size_t n) {
    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t got = ::read(fd, dst, n);
        ::close(fd);
        if (got == static_cast<ssize_t>(n)) return;
    }
    // Fallback: time + pid hashed across the buffer. Plenty for our
    // weak-anti-detection use; we're not generating crypto keys.
    uint64_t seed = static_cast<uint64_t>(::time(nullptr)) ^
                    static_cast<uint64_t>(::getpid()) * 0x9E3779B97F4A7C15ULL;
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        dst[i] = static_cast<uint8_t>(seed >> 56);
    }
}

const ProbeNames& probe_names() {
    static const ProbeNames n = [] {
        ProbeNames v{};
        uint8_t r[17];
        FillRandom(r, sizeof(r));
        for (int i = 0; i < 7; ++i) v.pkg[i]    = 'a' + (r[i] % 26);
        for (int i = 0; i < 5; ++i) v.cls[i]    = 'a' + (r[7 + i] % 26);
        for (int i = 0; i < 5; ++i) v.method[i] = 'a' + (r[12 + i] % 26);
        std::snprintf(v.descriptor, sizeof(v.descriptor), "L%s/%s;", v.pkg, v.cls);
        std::snprintf(v.dotted,     sizeof(v.dotted),     "%s.%s",   v.pkg, v.cls);
        return v;
    }();
    return n;
}

// ---- SHA-1 (Steve Reid style, public domain) ----------------------------

#define SHA1_ROL(v, b) (((v) << (b)) | ((v) >> (32 - (b))))

void Sha1Transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = static_cast<uint32_t>(buffer[i * 4]) << 24 |
               static_cast<uint32_t>(buffer[i * 4 + 1]) << 16 |
               static_cast<uint32_t>(buffer[i * 4 + 2]) << 8 |
               static_cast<uint32_t>(buffer[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = SHA1_ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t t = SHA1_ROL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = SHA1_ROL(b, 30);
        b = a;
        a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void Sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint32_t state[5] = {
        0x67452301,
        0xEFCDAB89,
        0x98BADCFE,
        0x10325476,
        0xC3D2E1F0,
    };
    uint64_t bit_len = static_cast<uint64_t>(len) * 8;

    size_t pos = 0;
    while (pos + 64 <= len) {
        Sha1Transform(state, data + pos);
        pos += 64;
    }

    uint8_t tail[128] = {0};
    size_t rem = len - pos;
    std::memcpy(tail, data + pos, rem);
    tail[rem] = 0x80;
    size_t tail_size = (rem < 56) ? 64 : 128;
    for (int i = 0; i < 8; ++i) {
        tail[tail_size - 8 + i] = static_cast<uint8_t>(bit_len >> ((7 - i) * 8));
    }

    Sha1Transform(state, tail);
    if (tail_size == 128) Sha1Transform(state, tail + 64);

    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<uint8_t>(state[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(state[i]);
    }
}

uint32_t Adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

// ---- DEX builder --------------------------------------------------------
//
// Hand-laid 360-byte DEX. Layout (offsets in hex):
//   0x000 header (0x70 bytes)
//   0x070 string_ids (4 * 4 = 16 bytes)
//   0x080 type_ids (3 * 4 = 12 bytes)
//   0x08C proto_ids (1 * 12 = 12 bytes)
//   0x098 method_ids (1 * 8 = 8 bytes)
//   0x0A0 class_defs (1 * 32 = 32 bytes)
//   0x0C0 string_data (4 items, 47 bytes total)
//   0x0EF class_data (8 bytes)
//   0x0F7 padding (1 byte to 4-align map_list)
//   0x0F8 map_list (4 + 9*12 = 112 bytes)
//   0x168 end
//
// Class: public Larthook/Probe; extends Ljava/lang/Object;
// Method: public static native void probe()V

constexpr size_t kDexSize = 0x168;

void WriteU16(uint8_t* p, uint16_t v) {
    std::memcpy(p, &v, 2);
}
void WriteU32(uint8_t* p, uint32_t v) {
    std::memcpy(p, &v, 4);
}

std::vector<uint8_t> BuildProbeDex() {
    std::vector<uint8_t> d(kDexSize, 0);

    // Magic
    std::memcpy(d.data(), "dex\n035\0", 8);
    // d[8..11] = checksum (filled later)
    // d[12..31] = signature (filled later)

    // Header fields starting at 0x20
    WriteU32(&d[0x20], kDexSize);    // file_size
    WriteU32(&d[0x24], 0x70);        // header_size
    WriteU32(&d[0x28], 0x12345678);  // endian_tag (little-endian)
    WriteU32(&d[0x2C], 0);           // link_size
    WriteU32(&d[0x30], 0);           // link_off
    WriteU32(&d[0x34], 0xF8);        // map_off
    WriteU32(&d[0x38], 4);           // string_ids_size
    WriteU32(&d[0x3C], 0x70);        // string_ids_off
    WriteU32(&d[0x40], 3);           // type_ids_size
    WriteU32(&d[0x44], 0x80);        // type_ids_off
    WriteU32(&d[0x48], 1);           // proto_ids_size
    WriteU32(&d[0x4C], 0x8C);        // proto_ids_off
    WriteU32(&d[0x50], 0);           // field_ids_size
    WriteU32(&d[0x54], 0);           // field_ids_off
    WriteU32(&d[0x58], 1);           // method_ids_size
    WriteU32(&d[0x5C], 0x98);        // method_ids_off
    WriteU32(&d[0x60], 1);           // class_defs_size
    WriteU32(&d[0x64], 0xA0);        // class_defs_off
    WriteU32(&d[0x68], 0xA8);        // data_size (0x168 - 0xC0)
    WriteU32(&d[0x6C], 0xC0);        // data_off

    // String IDs (4 entries, each is an offset into string_data)
    WriteU32(&d[0x70], 0xC0);  // "Larthook/Probe;"
    WriteU32(&d[0x74], 0xD1);  // "Ljava/lang/Object;"
    WriteU32(&d[0x78], 0xE5);  // "V"
    WriteU32(&d[0x7C], 0xE8);  // "probe"

    // Type IDs (3 entries, each is a string idx)
    WriteU32(&d[0x80], 0);  // Larthook/Probe;
    WriteU32(&d[0x84], 1);  // Ljava/lang/Object;
    WriteU32(&d[0x88], 2);  // V

    // Proto IDs (1 entry: shorty, return, params_off)
    WriteU32(&d[0x8C], 2);  // shorty_idx → "V"
    WriteU32(&d[0x90], 2);  // return_type_idx → V
    WriteU32(&d[0x94], 0);  // parameters_off → none

    // Method IDs (1 entry: class_idx u16, proto_idx u16, name_idx u32)
    WriteU16(&d[0x98], 0);  // class_idx → Larthook/Probe;
    WriteU16(&d[0x9A], 0);  // proto_idx → ()V
    WriteU32(&d[0x9C], 3);  // name_idx → "probe"

    // Class Defs (1 entry, 32 bytes)
    WriteU32(&d[0xA0], 0);           // class_idx → Larthook/Probe;
    WriteU32(&d[0xA4], 0x0001);      // access_flags = ACC_PUBLIC
    WriteU32(&d[0xA8], 1);           // superclass_idx → Ljava/lang/Object;
    WriteU32(&d[0xAC], 0);           // interfaces_off
    WriteU32(&d[0xB0], 0xFFFFFFFF);  // source_file_idx → NO_INDEX
    WriteU32(&d[0xB4], 0);           // annotations_off
    WriteU32(&d[0xB8], 0xEF);        // class_data_off
    WriteU32(&d[0xBC], 0);           // static_values_off

    // String data items (uleb128 utf16_len + MUTF-8 + NUL). Class
    // descriptor and method name are the randomized versions; lengths
    // (15 / 5) match the DEX layout this hand-laid table was built for.
    d[0xC0] = 15;
    std::memcpy(&d[0xC1], probe_names().descriptor, 15);
    // d[0xC0 + 16] = 0 already
    d[0xD1] = 18;
    std::memcpy(&d[0xD2], "Ljava/lang/Object;", 18);
    d[0xE5] = 1;
    d[0xE6] = 'V';
    d[0xE8] = 5;
    std::memcpy(&d[0xE9], probe_names().method, 5);

    // Class data at 0xEF (uleb128 fields)
    d[0xEF] = 0;  // static_fields_size
    d[0xF0] = 0;  // instance_fields_size
    d[0xF1] = 1;  // direct_methods_size
    d[0xF2] = 0;  // virtual_methods_size
    // direct_methods[0]:
    d[0xF3] = 0;     // method_idx_diff = 0
    d[0xF4] = 0x89;  // access_flags = 0x109 (uleb128 byte 1: 0x09 | 0x80)
    d[0xF5] = 0x02;  // access_flags = 0x109 (uleb128 byte 2: 0x02)
    d[0xF6] = 0;     // code_off = 0 (native, no code)
    // d[0xF7] = 0 (padding)

    // Map list at 0xF8 (size + 9 entries of 12 bytes each)
    WriteU32(&d[0xF8], 9);
    auto map = [&](size_t entry_idx, uint16_t type, uint32_t size, uint32_t off) {
        size_t p = 0xFC + entry_idx * 12;
        WriteU16(&d[p], type);
        WriteU16(&d[p + 2], 0);
        WriteU32(&d[p + 4], size);
        WriteU32(&d[p + 8], off);
    };
    map(0, 0x0000, 1, 0x00);  // HEADER_ITEM
    map(1, 0x0001, 4, 0x70);  // STRING_ID_ITEM
    map(2, 0x0002, 3, 0x80);  // TYPE_ID_ITEM
    map(3, 0x0003, 1, 0x8C);  // PROTO_ID_ITEM
    map(4, 0x0005, 1, 0x98);  // METHOD_ID_ITEM
    map(5, 0x0006, 1, 0xA0);  // CLASS_DEF_ITEM
    map(6, 0x2002, 4, 0xC0);  // STRING_DATA_ITEM
    map(7, 0x2000, 1, 0xEF);  // CLASS_DATA_ITEM
    map(8, 0x1000, 1, 0xF8);  // MAP_LIST

    // Signature = SHA-1 of bytes [32, end)
    Sha1(d.data() + 32, kDexSize - 32, &d[12]);

    // Checksum = Adler-32 of bytes [12, end) (signature + everything after)
    uint32_t cs = Adler32(d.data() + 12, kDexSize - 12);
    WriteU32(&d[8], cs);

    return d;
}

// ---- Class loading helpers ----------------------------------------------

jclass FindClassChecked(JNIEnv* env, const char* name) {
    jclass c = env->FindClass(name);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return c;
}

jobject GetSystemClassLoader(JNIEnv* env) {
    jclass cl_cls = FindClassChecked(env, "java/lang/ClassLoader");
    if (!cl_cls) return nullptr;
    jmethodID m =
        env->GetStaticMethodID(cl_cls, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
    if (!m) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cl_cls);
        return nullptr;
    }
    jobject loader = env->CallStaticObjectMethod(cl_cls, m);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        loader = nullptr;
    }
    env->DeleteLocalRef(cl_cls);
    return loader;
}

// Load the probe class from a freshly-built dex. Returns the probe class
// as a local ref on success (caller must DeleteLocalRef), nullptr on
// failure. The dex bytes must stay alive for the lifetime of the
// classloader — we keep `dex_storage` as a static so the ByteBuffer it
// backs doesn't get freed (only relevant on the direct-buffer path).
jclass LoadProbeClass(JNIEnv* env) {
    static std::vector<uint8_t> dex_storage = BuildProbeDex();

    auto dumpException = [&](const char* where) {
        if (env->ExceptionCheck()) {
            LOGE("Probe: %s threw — describing:", where);
            env->ExceptionDescribe();  // prints stack trace to logcat
            env->ExceptionClear();
        } else {
            LOGE("Probe: %s returned null without exception", where);
        }
    };

    // Strip any MTE / pointer-auth tag from the address — some Android 14+
    // builds reject tagged native pointers in NewDirectByteBuffer.
    uintptr_t addr = reinterpret_cast<uintptr_t>(dex_storage.data());
    addr &= 0x00ffffffffffffffULL;

    // Preferred: direct ByteBuffer wrapping native memory — zero-copy.
    jobject buf = env->NewDirectByteBuffer(reinterpret_cast<void*>(addr),
                                           static_cast<jlong>(dex_storage.size()));
    if (env->ExceptionCheck()) {
        LOGE("Probe: NewDirectByteBuffer threw —");
        env->ExceptionDescribe();
        env->ExceptionClear();
        buf = nullptr;
    }

    // Fallback: copy into a Java byte[] and wrap via ByteBuffer.wrap.
    if (!buf) {
        LOGI("Probe: trying ByteBuffer.wrap fallback");
        jbyteArray ba = env->NewByteArray(static_cast<jsize>(dex_storage.size()));
        if (!ba || env->ExceptionCheck()) {
            dumpException("NewByteArray");
            return nullptr;
        }

        env->SetByteArrayRegion(ba,
                                0,
                                static_cast<jsize>(dex_storage.size()),
                                reinterpret_cast<const jbyte*>(dex_storage.data()));
        if (env->ExceptionCheck()) {
            dumpException("SetByteArrayRegion");
            env->DeleteLocalRef(ba);
            return nullptr;
        }

        jclass bbCls = env->FindClass("java/nio/ByteBuffer");
        if (!bbCls) {
            dumpException("FindClass(ByteBuffer)");
            env->DeleteLocalRef(ba);
            return nullptr;
        }

        jmethodID wrap = env->GetStaticMethodID(bbCls, "wrap", "([B)Ljava/nio/ByteBuffer;");
        if (!wrap) {
            dumpException("GetStaticMethodID(wrap)");
            env->DeleteLocalRef(bbCls);
            env->DeleteLocalRef(ba);
            return nullptr;
        }

        buf = env->CallStaticObjectMethod(bbCls, wrap, ba);
        if (!buf || env->ExceptionCheck()) {
            dumpException("CallStaticObjectMethod(wrap)");
            env->DeleteLocalRef(bbCls);
            env->DeleteLocalRef(ba);
            return nullptr;
        }
        env->DeleteLocalRef(bbCls);
        env->DeleteLocalRef(ba);
        LOGI("Probe: ByteBuffer.wrap fallback succeeded");
    }

    jclass loader_cls = FindClassChecked(env, "dalvik/system/InMemoryDexClassLoader");
    if (!loader_cls) {
        env->DeleteLocalRef(buf);
        return nullptr;
    }
    jmethodID ctor =
        env->GetMethodID(loader_cls, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    if (!ctor) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(loader_cls);
        env->DeleteLocalRef(buf);
        return nullptr;
    }

    jobject parent = GetSystemClassLoader(env);
    jobject loader = env->NewObject(loader_cls, ctor, buf, parent);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        loader = nullptr;
    }
    env->DeleteLocalRef(loader_cls);
    env->DeleteLocalRef(buf);
    if (parent) env->DeleteLocalRef(parent);
    if (!loader) {
        LOGE("Probe: InMemoryDexClassLoader ctor failed (probe dex rejected?)");
        return nullptr;
    }

    jclass    cl_cls = FindClassChecked(env, "java/lang/ClassLoader");
    jmethodID loadClass =
        env->GetMethodID(cl_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = env->NewStringUTF(probe_names().dotted);
    jobject clazz_obj = env->CallObjectMethod(loader, loadClass, name);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        clazz_obj = nullptr;
    }
    env->DeleteLocalRef(cl_cls);
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(loader);
    if (!clazz_obj) {
        LOGE("Probe: loadClass failed");
        return nullptr;
    }
    return static_cast<jclass>(clazz_obj);
}

}  // namespace

// Never actually invoked — only RegisterNatives is called. The quick
// entry of an unresolved native stays at art_quick_resolution_trampoline
// (in libart), which Layout.cpp adjusts by +0x140 to reach the generic
// JNI bridge. Invoking would resolve the entry, and on Android 15 ART
// updates the stored quick entry to point straight at this C function —
// outside libart, breaking the bridge derivation.
extern "C" void Probe_dummy(JNIEnv*, jclass) {}

void* Probe::SampleProbeArtMethod(JNIEnv* env) {
    if (!env) return nullptr;
    jclass probe = LoadProbeClass(env);
    if (!probe) return nullptr;

    JNINativeMethod nm{};
    nm.name      = const_cast<char*>(probe_names().method);
    nm.signature = const_cast<char*>("()V");
    nm.fnPtr     = reinterpret_cast<void*>(&Probe_dummy);
    if (env->RegisterNatives(probe, &nm, 1) != JNI_OK) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("Probe: RegisterNatives failed");
        env->DeleteLocalRef(probe);
        return nullptr;
    }

    ArtMethodPtr am = ArtMethodFromJniBinding(env, probe, probe_names().method, "()V");

    // Drop our last reference to the probe class. Combined with the
    // already-released local refs to the classloader and ByteBuffer, the
    // class becomes eligible for ART to unload at its leisure — the
    // ArtMethodPtr we return is only read once by Layout.cpp before the
    // caller discards it.
    env->DeleteLocalRef(probe);
    if (!am) {
        LOGE("Probe: failed to resolve probe ArtMethod");
        return nullptr;
    }
    LOGI("Probe: probe ArtMethod = %p", am);
    return am;
}

}  // namespace arthook

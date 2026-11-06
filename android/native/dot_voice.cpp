#include <jni.h>
#include <whisper.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int rate = 16000;
constexpr int block = rate / 50;
std::mutex modelGuard;
std::unique_ptr<whisper_context, decltype(&whisper_free)> cached(nullptr,
                                                                 whisper_free);
std::string loadedPath;
void load(const std::string &path) {
  if (cached && loadedPath == path)
    return;
  cached.reset();
  loadedPath.clear();
  auto params = whisper_context_default_params();
  params.use_gpu = false;
  cached.reset(whisper_init_from_file_with_params(path.c_str(), params));
  if (!cached)
    throw std::runtime_error("Não consegui abrir o modelo no celular.");
  loadedPath = path;
}

float decodeMuLaw(uint8_t value) {
  const unsigned code = static_cast<uint8_t>(~value);
  const int magnitude = (((code & 15) * 8 + 132) << ((code >> 4) & 7)) - 132;
  return (code & 128 ? -magnitude : magnitude) / 32768.0f;
}

std::string recognize(const std::string &model,
                      const std::vector<jbyte> &audio) {
  std::vector<float> pcm(audio.size());
  std::transform(audio.begin(), audio.end(), pcm.begin(), decodeMuLaw);
  // Reject silence and constant DC before passing audio to a generative
  // decoder.
  int voiced = 0;
  for (size_t i = 0; i + block <= pcm.size(); i += block) {
    double sum = 0, squares = 0;
    for (int j = 0; j < block; ++j) {
      double v = pcm[i + j];
      sum += v;
      squares += v * v;
    }
    const double variance = squares / block - (sum / block) * (sum / block);
    if (variance > 0.000025)
      ++voiced;
  }
  if (voiced < 5)
    return {};
  std::lock_guard<std::mutex> lock(modelGuard);
  load(model);
  auto options = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  options.n_threads = 4;
  options.language = "pt";
  options.translate = false;
  options.no_context = true;
  options.print_progress = false;
  options.print_timestamps = false;
  options.print_realtime = false;
  options.print_special = false;
  options.suppress_nst = true;
  options.temperature_inc = 0;
  if (whisper_full(cached.get(), options, pcm.data(),
                   static_cast<int>(pcm.size())) != 0) {
    throw std::runtime_error("Não consegui transcrever a fala no celular.");
  }
  std::string result;
  for (int i = 0; i < whisper_full_n_segments(cached.get()); ++i) {
    if (const char *segment = whisper_full_get_segment_text(cached.get(), i))
      result += segment;
  }
  return result;
}
} // namespace

extern "C" JNIEXPORT jbyteArray JNICALL
Java_br_bolso_dot_NativeVoice_transcribe(JNIEnv *env, jclass, jstring file,
                                         jbyteArray data) {
  try {
    if (!file || !data)
      throw std::runtime_error("Áudio indisponível.");
    const auto count = env->GetArrayLength(data);
    if (count < rate / 4 || count > 30 * rate)
      throw std::runtime_error("Duração de voz inválida.");
    const char *utf = env->GetStringUTFChars(file, nullptr);
    if (!utf)
      return nullptr;
    std::string model(utf);
    env->ReleaseStringUTFChars(file, utf);
    std::vector<jbyte> audio(count);
    env->GetByteArrayRegion(data, 0, count, audio.data());
    if (env->ExceptionCheck())
      return nullptr;
    auto text = recognize(model, audio);
    auto result = env->NewByteArray(static_cast<jsize>(text.size()));
    if (result && !text.empty())
      env->SetByteArrayRegion(result, 0, text.size(),
                              reinterpret_cast<const jbyte *>(text.data()));
    return result;
  } catch (const std::exception &e) {
    jclass type = env->FindClass("java/io/IOException");
    if (type)
      env->ThrowNew(type, e.what());
    return nullptr;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_br_bolso_dot_NativeVoice_warm(JNIEnv *env, jclass, jstring file) {
  const char *utf = env->GetStringUTFChars(file, nullptr);
  if (!utf)
    return;
  std::string path(utf);
  env->ReleaseStringUTFChars(file, utf);
  try {
    std::lock_guard<std::mutex> lock(modelGuard);
    load(path);
  } catch (const std::exception &e) {
    jclass type = env->FindClass("java/io/IOException");
    if (type)
      env->ThrowNew(type, e.what());
  }
}
extern "C" JNIEXPORT void JNICALL
Java_br_bolso_dot_NativeVoice_release(JNIEnv *, jclass) {
  std::lock_guard<std::mutex> lock(modelGuard);
  cached.reset();
  loadedPath.clear();
}

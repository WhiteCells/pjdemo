#include <iostream>
#include <vector>
#include <sndfile.h>
#include <portaudio.h>

// 回调函数用于将音频数据输出到音频设备
static int audioCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo *timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData
) {
    SF_INFO *sfinfo = (SF_INFO *)userData;
    float *out = (float *)outputBuffer;
    static std::vector<float> buffer(framesPerBuffer * sfinfo->channels);
    
    // 读取文件的音频数据
    sf_count_t framesRead = sf_readf_float((SNDFILE *)userData, buffer.data(), framesPerBuffer);
    if (framesRead < framesPerBuffer) {
        std::fill(out, out + framesPerBuffer * sfinfo->channels, 0); // 用零填充，表示音频流结束
    }
    
    // 将数据传递给输出缓冲区
    std::copy(buffer.begin(), buffer.begin() + framesRead * sfinfo->channels, out);
    return paContinue;
}

// 流式音频 -> 音频设备
void streamAudioToDevice(const char *filename) {
    SNDFILE *sndfile;
    SF_INFO sfinfo;
    if (!(sndfile = sf_open(filename, SFM_READ, &sfinfo))) {
        std::cerr << "Failed to open audio file: " << sf_strerror(sndfile) << std::endl;
        return;
    }

    // 初始化PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    PaStream *stream;
    err = Pa_OpenDefaultStream(&stream, 0, sfinfo.channels, paFloat32, sfinfo.samplerate,
                               256, audioCallback, (void *)sndfile);
    if (err != paNoError) {
        std::cerr << "Failed to open PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "Failed to start PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    // 运行直到音频文件播放完
    std::cout << "Playing audio..." << std::endl;
    while (Pa_IsStreamActive(stream)) {
        Pa_Sleep(100);
    }

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    sf_close(sndfile);
}

int main() {
    const char *audioFile = "/home/cells/dev/c-project/pj-demo/pa/16k16bit.wav";
    streamAudioToDevice(audioFile);
    return 0;
}

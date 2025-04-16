#include <iostream>
#include <portaudio.h>
#include <sndfile.h>

#define SAMPLE_RATE 44100
#define NUM_CHANNELS 2
#define FRAMES_PER_BUFFER 256

// 音频回调函数
static int audioCallback(const void *inputBuffer, void *outputBuffer, unsigned long frameCount,
    const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
    void *userData
) {
    SNDFILE *sndfile = (SNDFILE *)userData;
    sf_count_t framesRead = sf_readf_float(sndfile, (float *)outputBuffer, frameCount);
    if (framesRead < frameCount) {
        return paComplete;  // 如果文件读完了，则结束
    }
    return paContinue;
}

int main() {
    PaError err;

    // 打开 WAV 文件
    SF_INFO sfinfo;
    sfinfo.format = 0;  // 设置文件格式为未知
    SNDFILE *sndfile = sf_open("your_audio_file.wav", SFM_READ, &sfinfo);
    if (!sndfile) {
        std::cerr << "Error opening audio file!" << std::endl;
        return -1;
    }

    // 初始化 PortAudio
    err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization error: " << Pa_GetErrorText(err) << std::endl;
        return 1;
    }

    // 查找 Loopback 设备，假设它是设备 2
    PaDeviceIndex outputDeviceIndex = 2;  // Loopback 设备的索引
    PaStream *stream;

    PaStreamParameters outputParameters;
    outputParameters.device = outputDeviceIndex;
    outputParameters.channelCount = 2;  // 例如立体声
    outputParameters.sampleFormat = paFloat32;  // 32位浮动点数
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputDeviceIndex)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    // 打开流，使用 Loopback 设备作为输出
    err = Pa_OpenStream(&stream,
                        NULL, // 输入设备为空
                        &outputParameters, // 输出设备为 2 通道
                        SAMPLE_RATE, // 音频格式为浮动点
                        FRAMES_PER_BUFFER,
                        paClipOff,  // 禁用自动剪辑
                        audioCallback,
                        sndfile);
    if (err != paNoError) {
        std::cerr << "PortAudio stream open error: " << Pa_GetErrorText(err) << std::endl;
        return 1;
    }

    // 启动音频流
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "PortAudio stream start error: " << Pa_GetErrorText(err) << std::endl;
        return 1;
    }

    // 播放音频
    std::cout << "Playing audio..." << std::endl;

    // 使用 Pa_IsStreamActive 来判断流是否仍然活跃
    while (Pa_IsStreamActive(stream)) {
        Pa_Sleep(100); // 等待，检查流是否仍然活跃
    }

    // 停止流
    err = Pa_StopStream(stream);
    if (err != paNoError) {
        std::cerr << "PortAudio stream stop error: " << Pa_GetErrorText(err) << std::endl;
        return 1;
    }

    // 清理和关闭
    Pa_Terminate();
    sf_close(sndfile);

    return 0;
}

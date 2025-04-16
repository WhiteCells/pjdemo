#include <iostream>
#include <vector>
#include <sndfile.h>
#include <portaudio.h>

// 回调函数用于从音频设备捕获数据
static int captureCallback(
    const void *inputBuffer, // 
    void *outputBuffer, // 
    unsigned long framesPerBuffer, // 
    const PaStreamCallbackTimeInfo *timeInfo, //
    PaStreamCallbackFlags statusFlags, //
    void *userData // 
) {
    SF_INFO *sfinfo = (SF_INFO *)userData;
    const float *in = (const float *)inputBuffer;
    std::vector<float> buffer(framesPerBuffer * sfinfo->channels);
    
    // 将输入缓冲区数据写入文件
    std::copy(in, in + framesPerBuffer * sfinfo->channels, buffer.begin());
    sf_writef_float((SNDFILE *)userData, buffer.data(), framesPerBuffer);
    
    return paContinue;
}

// 捕获 dev
void captureFromDeviceToFile(const char *filename) {
    SF_INFO sfinfo;
    sfinfo.samplerate = 44100;  // 设定采样率
    sfinfo.channels = 2;        // 立体声
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;  // 文件格式
    
    SNDFILE *sndfile = sf_open(filename, SFM_WRITE, &sfinfo);
    if (!sndfile) {
        std::cerr << "Failed to open file: " << sf_strerror(sndfile) << std::endl;
        return;
    }

    // 初始化PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    PaStream *stream;
    err = Pa_OpenDefaultStream(&stream, sfinfo.channels, 0, paFloat32, sfinfo.samplerate,
                               256, captureCallback, (void *)sndfile);
    if (err != paNoError) {
        std::cerr << "Failed to open PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "Failed to start PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    // 捕获音频数据并保存到文件
    std::cout << "Capturing audio..." << std::endl;
    while (Pa_IsStreamActive(stream)) {
        Pa_Sleep(100);
    }

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    sf_close(sndfile);
}

int main() {
    const char *outputFile = "output_audio.wav";
    captureFromDeviceToFile(outputFile);
    return 0;
}

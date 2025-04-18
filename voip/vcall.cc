#include "vcall.h"
#include "vaccount.h"
#include "vaudiomediaport.h"

#include <pjsua2/call.hpp>
#include <iostream>
#include <fstream>

voip::VCall::VCall(voip::VAccount &acc, int call_id) :
    Call(acc, call_id),
    acc_(acc)
{
}

voip::VCall::~VCall()
{
    if (acc_.cur_call == this) {
        acc_.cur_call = nullptr;
        std::cout << "--- Call object destroyed, account call pointer cleared." << std::endl;
    }
    else {
        std::cout << "--- Call object destroyed (was not the account's active call)." << std::endl;
    }
}

void voip::VCall::onCallState(pj::OnCallStateParam &prm)
{
    PJ_UNUSED_ARG(prm);
    try {
        pj::CallInfo ci = getInfo();
        std::cout << "*** Call " << ci.id << " State: " << ci.stateText;
        if (!ci.lastReason.empty()) {
            std::cout << " (Reason: " << ci.lastReason << ")";
        }
        std::cout << std::endl;

        if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
            std::cout << "--- Call " << ci.id << " Disconnected." << std::endl;
            if (acc_.cur_call == this) {
                acc_.cur_call = nullptr;
                std::cout << "--- Account's active call pointer cleared due to DISCONNECTED state." << std::endl;
            }
        }
        else if (ci.state == PJSIP_INV_STATE_CONFIRMED) {
            std::cout << "--- Call " << ci.id << " Connected/Confirmed." << std::endl;
        }
    }
    catch (const pj::Error &err) {
        std::cerr << "!!! Error getting call info in onCallState: " << err.info() << std::endl;
    }
}

void voip::VCall::onCallMediaState(pj::OnCallMediaStateParam &prm)
{
    PJ_UNUSED_ARG(prm);
    try {
        pj::CallInfo ci = getInfo();
        std::cout << "*** Call " << ci.id << " Media State Changed" << std::endl;

        voip::VRecvAudioMediaPort *recv_aud_med = new voip::VRecvAudioMediaPort {};
        voip::VSendAudioMediaPort *send_aud_med = new voip::VSendAudioMediaPort {};
        pj::MediaFormatAudio med_for_aud;

        // med_for_aud.init(PJMEDIA_FORMAT_PCM, 8000, 1, 20, 16);
        // recv_aud_med->createPort("recv", med_for_aud);
        // send_aud_med->createPort("send", med_for_aud);

        pj::AudDevManager &mgr = pj::Endpoint::instance().audDevManager();
        auto cap_dev_med = mgr.getCaptureDevMedia();
        auto play_dev_med = mgr.getPlaybackDevMedia();

        for (unsigned i = 0; i < ci.media.size(); ++i) {
            if (ci.media[i].type == PJMEDIA_TYPE_AUDIO && getMedia(i)) {
                pj::AudioMedia aud_med = getAudioMedia(i);

                if (ci.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
                    std::cout << "--- Audio media is ACTIVE for call " << ci.id << ". Connecting to sound device." << std::endl;
                    try {
                        cap_dev_med.startTransmit(aud_med);
                        aud_med.startTransmit(play_dev_med);

                        // recv_aud_med->startTransmit(*send_aud_med);
                        // send_aud_med->startTransmit(play_dev_med);

                        // play_dev_med.startTransmit(*recv_aud_med);
                        // aud_med.startTransmit(*recv_aud_med);
                        // recv_aud_med->startTransmit(aud_med);

                        std::cout << "--- Audio connected for call " << ci.id << std::endl;
                    }
                    catch (pj::Error &err) {
                        std::cerr << "!!! Failed to connect audio for call " << ci.id << ": " << err.info() << std::endl;
                    }
                }
            }
            else if (ci.media[i].type != PJMEDIA_TYPE_AUDIO) {
                std::cout << "--- Non-audio media stream detected (type: " << ci.media[i].type << ")" << std::endl;
            }
        }
    }
    catch (const pj::Error &err) {
        std::cerr << "!!! Error in onCallMediaState: " << err.info() << std::endl;
    }
}

// void voip::VCall::onStreamCreated(pj::OnStreamCreatedParam &prm)
// {
//     this->onStreamCreated(prm);
//     std::ofstream out_file("stream", std::ios::binary | std::ios::app);
//     if (out_file.is_open()) {
//         out_file.write(reinterpret_cast<const char *>(prm.stream), sizeof(prm.stream));
//         out_file.close();
//     }
// }
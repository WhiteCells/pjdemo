#include "vcall.h"
#include "vaccount.h"
#include <iostream>

voip::VCall(voip::VAccount &acc, int call_id)
{
}

voip::VCall::~VCall()
{
    if (myAcc.currentCall == this) {
        myAcc.currentCall = nullptr;
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
            if (myAcc.currentCall == this) {
                myAcc.currentCall = nullptr; // Signal that the call is over
                std::cout << "--- Account's active call pointer cleared due to DISCONNECTED state." << std::endl;
                // Deletion is handled by main loop or unique_ptr management now
            }
        }
        else if (ci.state == PJSIP_INV_STATE_CONFIRMED) {
            std::cout << "--- Call " << ci.id << " Connected/Confirmed." << std::endl;
        }
    }
    catch (const Error &err) {
        std::cerr << "!!! Error getting call info in onCallState: " << err.info() << std::endl;
    }
}

void voip::MyCall::onCallMediaState(OnCallMediaStateParam &prm)
{
    PJ_UNUSED_ARG(prm);
    try {
        CallInfo ci = getInfo();
        std::cout << "*** Call " << ci.id << " Media State Changed" << std::endl;

        for (unsigned i = 0; i < ci.media.size(); ++i) {
            if (ci.media[i].type == PJMEDIA_TYPE_AUDIO && getMedia(i)) {
                AudioMedia *aud_med = static_cast<AudioMedia *>(getMedia(i));
                AudDevManager &mgr = Endpoint::instance().audDevManager();

                if (ci.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
                    std::cout << "--- Audio media is ACTIVE for call " << ci.id << ". Connecting to sound device." << std::endl;
                    try {
                        mgr.getCaptureDevMedia().startTransmit(*aud_med);
                        aud_med->startTransmit(mgr.getPlaybackDevMedia());
                        std::cout << "--- Audio connected for call " << ci.id << std::endl;
                    }
                    catch (Error &err) {
                        std::cerr << "!!! Failed to connect audio for call " << ci.id << ": " << err.info() << std::endl;
                    }
                }
                else {
                    std::cout << "--- Audio media is NOT ACTIVE (status: " << ci.media[i].status << ") for call " << ci.id << "." << std::endl;
                    // Optional: Stop transmission explicitly if needed, though PJSIP often handles it.
                    // try { mgr.getCaptureDevMedia().stopTransmit(*aud_med); } catch(...) {}
                    // try { aud_med->stopTransmit(mgr.getPlaybackDevMedia()); } catch(...) {}
                }
            }
            else if (ci.media[i].type != PJMEDIA_TYPE_AUDIO) {
                std::cout << "--- Non-audio media stream detected (type: " << ci.media[i].type << ")" << std::endl;
            }
        }
    }
    catch (const Error &err) {
        std::cerr << "!!! Error in onCallMediaState: " << err.info() << std::endl;
    }
}

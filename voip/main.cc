#include <pjsua2.hpp>
#include <iostream>
#include <string>
#include <memory>
#include <limits>

#define SIP_USER      "1003"
#define SIP_DOMAIN    "192.168.10.51:5060"
#define SIP_PASSWORD  "1003"
#define SIP_REGISTRAR "sip:" SIP_DOMAIN

using namespace pj;

class MyAccount;
class MyCall;

class MyCall : public Call
{
private:
    MyAccount &myAcc;

public:
    MyCall(Account& acc, int call_id = PJSUA_INVALID_ID);

    ~MyCall();

    virtual void onCallState(OnCallStateParam &prm);
    virtual void onCallMediaState(OnCallMediaStateParam &prm);
};

class MyAccount : public Account
{
public:
    MyCall *currentCall = nullptr;

    MyAccount() :
        Account() {}

    virtual void onRegState(OnRegStateParam &prm)
    {
        AccountInfo ai = getInfo();
        std::cout << (ai.regIsActive ? "*** Registered:" : "*** Unregistered:")
                  << " code=" << prm.code << " reason=" << prm.reason
                  << " (" << ai.uri << ")" << std::endl;
    }

    virtual void onIncomingCall(OnIncomingCallParam &iprm)
    {
        CallOpParam prm;

        if (currentCall) {
            std::cout << "Another call is active"
                      << iprm.callId << " from " << iprm.rdata.srcAddress << std::endl;

            MyCall *rejectCall = nullptr;
            try {
                rejectCall = new MyCall(*this, iprm.callId);
                prm.statusCode = PJSIP_SC_BUSY_HERE;
                rejectCall->hangup(prm);
                delete rejectCall;
                rejectCall = nullptr;
            }
            catch (Error &err) {
                std::cerr << "!!! Error rejecting call ID " << iprm.callId << ": " << err.info() << std::endl;
                if (rejectCall) {
                    std::cerr << "!!! Attempting cleanup of rejectCall object after rejection error." << std::endl;
                    delete rejectCall;
                }
            }
            return;
        }

        std::cout << "*** Incoming Call: " << iprm.callId << " from " << iprm.rdata.srcAddress << std::endl;

        MyCall *call = nullptr;
        try {
            call = new MyCall(*this, iprm.callId);

            std::cout << ">>> Auto-answering incoming call..." << std::endl;
            prm.statusCode = PJSIP_SC_OK;
            call->answer(prm);
            currentCall = call;
        }
        catch (Error &err) {
            std::cerr << "!!! Failed to create or answer call ID " << iprm.callId << ": " << err.info() << std::endl;
            if (call) {
                delete call;
            }
            currentCall = nullptr;
        }
    }
};

MyCall::MyCall(Account &acc, int call_id) :
    Call(acc, call_id), myAcc(dynamic_cast<MyAccount &>(acc)) {}

MyCall::~MyCall()
{
    if (myAcc.currentCall == this) {
        myAcc.currentCall = nullptr;
        std::cout << "--- Call object destroyed, account call pointer cleared." << std::endl;
    }
    else {
        std::cout << "--- Call object destroyed (was not the account's active call)." << std::endl;
    }
}

void MyCall::onCallState(OnCallStateParam &prm)
{
    PJ_UNUSED_ARG(prm);
    try {
        CallInfo ci = getInfo();
        std::cout << "*** Call " << ci.id << " State: " << ci.stateText;
        if (!ci.lastReason.empty()) {
            std::cout << " (Reason: " << ci.lastReason << ")";
        }
        std::cout << std::endl;

        if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
            std::cout << "--- Call " << ci.id << " Disconnected." << std::endl;
            if (myAcc.currentCall == this) {
                myAcc.currentCall = nullptr;
                std::cout << "--- Account's active call pointer cleared due to DISCONNECTED state." << std::endl;
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

void MyCall::onCallMediaState(OnCallMediaStateParam &prm)
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

int main()
{
    Endpoint ep;
    std::unique_ptr<MyAccount> acc;

    try {
        // 初始化端点
        std::cout << "Init Endpoint..." << std::endl;
        ep.libCreate();
        EpConfig ep_cfg;
#ifdef STUN_SERVER
        ep_cfg.uaConfig.stunServer.push_back(STUN_SERVER);
#endif
        ep.libInit(ep_cfg);

        // 创建传输配置
        TransportConfig tcfg;
        tcfg.port = 5060;
        ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);

        // 初始化
        ep.libStart();
        std::cout << "*** Pjsua2 library started." << std::endl;

        // 设置音频设备
        try {
            AudDevManager &mgr = ep.audDevManager();
            if (mgr.getDevCount() > 0) {
                std::cout << "--- Default capture device: " << mgr.getCaptureDev() << std::endl;
                std::cout << "--- Default playback device: " << mgr.getPlaybackDev() << std::endl;
            }
            else {
                std::cout << "!!! No audio devices found. Using NULL audio device." << std::endl;
                ep.audDevManager().setNullDev();
            }
        }
        catch (Error &err) {
            std::cerr << "!!! Error setting audio devices: " << err.info() << std::endl;
            try {
                ep.audDevManager().setNullDev();
            }
            catch (...) {
            }
        }

        // 账户配置
        AccountConfig acc_cfg;
        acc_cfg.idUri = "sip:" SIP_USER "@" SIP_DOMAIN;
        acc_cfg.regConfig.registrarUri = SIP_REGISTRAR;
        AuthCredInfo cred("digest", "*", SIP_USER, 0, SIP_PASSWORD);
        acc_cfg.sipConfig.authCreds.push_back(cred);

        // 注册账户
        acc = std::make_unique<MyAccount>();
        acc->create(acc_cfg);
        std::cout << "*** Account created for " << acc_cfg.idUri << ". Registering..." << std::endl;

        //
        std::cout << "\nCommands:\n";
        std::cout << "  m <sip:user@domain>  : Make call\n";
        std::cout << "  h                    : Hangup call\n";
        std::cout << "  q                    : Quit\n\n";

        char cmd[100];
        while (true) {
            std::cout << "> ";
            if (!std::cin.getline(cmd, sizeof(cmd))) {
                if (std::cin.eof())
                    break;
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cerr << "!!! Input error." << std::endl;
                continue;
            }

            std::string command_line = cmd;
            if (command_line.empty()) {
                continue;
            }

            char action = command_line[0];

            if (action == 'q') {
                break;
            }
            else if (action == 'm') {
                if (acc->currentCall) {
                    std::cerr << "A call is already active." << std::endl;
                    continue;
                }
                if (command_line.length() < 3 || command_line[1] != ' ') {
                    std::cerr << "Use: m <sip:user@domain>" << std::endl;
                    continue;
                }
                std::string target_uri = command_line.substr(2);
                std::cout << ">>> Placing call to: " << target_uri << std::endl;

                MyCall *call = new MyCall(*acc);
                CallOpParam prm(true);

                try {
                    call->makeCall(target_uri, prm);
                    acc->currentCall = call;
                }
                catch (Error &err) {
                    std::cerr << "!!! Failed to make call: " << err.info() << std::endl;
                    delete call;
                }
            }
            else if (action == 'h') {
                if (!acc->currentCall) {
                    std::cerr << "!!! No active call to hang up." << std::endl;
                    continue;
                }
                std::cout << ">>> Hanging up call..." << std::endl;
                CallOpParam prm;
                try {
                    acc->currentCall->hangup(prm);
                }
                catch (Error &err) {
                    std::cerr << "!!! Failed to hang up call: " << err.info() << std::endl;
                }
            }
            else {
                std::cerr << "!!! Unknown command: " << action << std::endl;
            }

            if (acc->currentCall) {
                try {
                    CallInfo ci = acc->currentCall->getInfo();
                    if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
                        std::cout << "--- Detected disconnected call in main loop, attempting cleanup." << std::endl;
                        delete acc->currentCall;
                        acc->currentCall = nullptr;
                    }
                }
                catch (Error &err) {
                    std::cerr << "--- Error checking call state in main loop (might be already deleted): " << err.info() << std::endl;
                    acc->currentCall = nullptr;
                }
            }
        }

        // 6. Cleanup
        std::cout << "Shutting down..." << std::endl;
        if (acc->currentCall) {
            std::cout << "--- Hanging up active call before exit..." << std::endl;
            CallOpParam prm;
            try {
                acc->currentCall->hangup(prm);
            }
            catch (Error &err) {
            }
            pj_thread_sleep(500);
            delete acc->currentCall;
            acc->currentCall = nullptr;
        }

        acc.reset();

        ep.libDestroy();
        std::cout << "*** Pjsua2 library destroyed." << std::endl;
    }
    catch (Error &err) {
        std::cerr << "!!! Exception: " << err.info() << std::endl;
        try {
            if (ep.libGetState() != PJSUA_STATE_NULL)
                ep.libDestroy();
        }
        catch (...) {
        }
        return 1;
    }

    return 0;
}
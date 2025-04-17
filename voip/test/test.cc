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
    MyAccount &myAcc; // Reference to the account that owns this call

public:
    // Constructor for outgoing calls
    MyCall(Account &acc); // No changes here

    // Constructor for incoming calls (uses existing callId)
    // FIX: Removed default value for call_id to resolve ambiguity
    MyCall(Account &acc, int call_id);

    ~MyCall();

    virtual void onCallState(OnCallStateParam &prm);
    virtual void onCallMediaState(OnCallMediaStateParam &prm);
    // virtual void onDtmfDigit(OnDtmfDigitParam &prm); // Optional DTMF
};

// -----------------------------------------------------------------------------
// Custom Account class to handle account events
// -----------------------------------------------------------------------------
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
        CallOpParam prm; // Declare param here for reuse

        if (currentCall) {
            std::cout << "*** Another call is active. Rejecting incoming call ID "
                      << iprm.callId << " from " << iprm.rdata.srcAddress << std::endl;

            // FIX: Create a temporary Call object specifically to reject the incoming call
            MyCall *rejectCall = nullptr;
            try {
                rejectCall = new MyCall(*this, iprm.callId); // Create using incoming ID
                prm.statusCode = PJSIP_SC_BUSY_HERE;         // Set status code for hangup (rejection)
                rejectCall->hangup(prm);                     // Reject by hanging up immediately
                // Since we called hangup, the stack will manage the call termination.
                // We need to delete the C++ wrapper object we created.
                delete rejectCall;
                rejectCall = nullptr; // Avoid dangling pointer if delete throws somehow (unlikely)
            }
            catch (Error &err) {
                std::cerr << "!!! Error rejecting call ID " << iprm.callId << ": " << err.info() << std::endl;
                // Clean up if hangup failed but object was created
                if (rejectCall) {
                    // If hangup failed, deleting might be problematic, but we must avoid leaking.
                    // Consider logging this specific scenario.
                    std::cerr << "!!! Attempting cleanup of rejectCall object after rejection error." << std::endl;
                    delete rejectCall;
                }
            }
            return; // Don't process further (don't answer)
        }

        // --- If not busy, proceed with answering ---
        std::cout << "*** Incoming Call: " << iprm.callId << " from " << iprm.rdata.srcAddress << std::endl;

        MyCall *call = nullptr;
        try {
            // Create the real Call object for the incoming call we intend to answer
            call = new MyCall(*this, iprm.callId);

            std::cout << ">>> Auto-answering incoming call..." << std::endl;
            prm.statusCode = PJSIP_SC_OK; // Set status code for answer
            call->answer(prm);
            currentCall = call; // Track the now active call
        }
        catch (Error &err) {
            std::cerr << "!!! Failed to create or answer call ID " << iprm.callId << ": " << err.info() << std::endl;
            // Clean up if answer failed but object was created
            if (call) {
                // If answer failed, the call object might be in an unusable state. Delete it.
                delete call;
            }
            // Ensure currentCall isn't pointing to a failed call object
            currentCall = nullptr;
        }
    }
};

// -----------------------------------------------------------------------------
// MyCall Method Implementations (can be here or inline in class definition)
// -----------------------------------------------------------------------------
MyCall::MyCall(Account &acc) :
    Call(acc), myAcc(dynamic_cast<MyAccount &>(acc)) {}

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

// -----------------------------------------------------------------------------
// Main Application Logic
// -----------------------------------------------------------------------------
int main()
{
    Endpoint ep;
    std::unique_ptr<MyAccount> acc;

    try {
        // 1. Initialize Endpoint
        std::cout << "Initializing Endpoint..." << std::endl;
        ep.libCreate();
        EpConfig ep_cfg;
#ifdef STUN_SERVER
        ep_cfg.uaConfig.stunServer.push_back(STUN_SERVER);
#endif
        ep.libInit(ep_cfg);

        // 2. Create SIP Transport
        TransportConfig tcfg;
        tcfg.port = 5060;
        ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);

        // 3. Start the library
        ep.libStart();
        std::cout << "*** Pjsua2 library started." << std::endl;

        // Set audio devices
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

        // 4. Configure Account
        AccountConfig acc_cfg;
        acc_cfg.idUri = "sip:" SIP_USER "@" SIP_DOMAIN;
        acc_cfg.regConfig.registrarUri = SIP_REGISTRAR;
        AuthCredInfo cred("digest", "*", SIP_USER, 0, SIP_PASSWORD);
        acc_cfg.sipConfig.authCreds.push_back(cred);

        // 5. Create and Register Account
        acc = std::make_unique<MyAccount>();
        acc->create(acc_cfg);
        std::cout << "*** Account created for " << acc_cfg.idUri << ". Registering..." << std::endl;

        // --- Main Interaction Loop ---
        std::cout << "\nCommands:\n";
        std::cout << "  m <sip:user@domain>  : Make call\n";
        std::cout << "  h                    : Hangup call\n";
        std::cout << "  q                    : Quit\n"
                  << std::endl;

        char cmd[100]; // Increased buffer size slightly
        while (true) {
            std::cout << "> ";
            if (!std::cin.getline(cmd, sizeof(cmd))) {
                if (std::cin.eof())
                    break;
                std::cin.clear();
                // FIX: Added #include <limits> and corrected usage
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cerr << "!!! Input error. Please try again." << std::endl;
                continue;
            }

            std::string command_line = cmd;
            if (command_line.empty())
                continue;

            char action = command_line[0];

            if (action == 'q') {
                break;
            }
            else if (action == 'm') {
                if (acc->currentCall) {
                    std::cerr << "!!! Cannot make a new call. A call is already active." << std::endl;
                    continue;
                }
                if (command_line.length() < 3 || command_line[1] != ' ') {
                    std::cerr << "!!! Invalid format. Use: m <sip:user@domain>" << std::endl;
                    continue;
                }
                std::string target_uri = command_line.substr(2);
                std::cout << ">>> Placing call to: " << target_uri << std::endl;

                // FIX: Constructor call is now unambiguous
                MyCall *call = new MyCall(*acc);
                CallOpParam prm(true);

                try {
                    call->makeCall(target_uri, prm);
                    acc->currentCall = call;
                }
                catch (Error &err) {
                    std::cerr << "!!! Failed to make call: " << err.info() << std::endl;
                    delete call; // Clean up if makeCall failed
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
                    // Call object deletion is implicitly handled by onCallState
                    // setting currentCall = nullptr, and subsequent check/delete
                }
                catch (Error &err) {
                    std::cerr << "!!! Failed to hang up call: " << err.info() << std::endl;
                }
            }
            else {
                std::cerr << "!!! Unknown command: " << action << std::endl;
            }

            // Cleanup check for disconnected calls (basic)
            if (acc->currentCall) {
                try {
                    CallInfo ci = acc->currentCall->getInfo();
                    if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
                        std::cout << "--- Detected disconnected call in main loop, attempting cleanup." << std::endl;
                        // Check if the pointer wasn't already cleared by onCallState
                        // This delete might be redundant if onCallState already cleared the pointer
                        // but better safe than leaking if the callback didn't trigger deletion logic.
                        // A more robust approach uses smart pointers or dedicated cleanup.
                        delete acc->currentCall;
                        acc->currentCall = nullptr; // Ensure it's null after delete
                    }
                }
                catch (Error &err) {
                    // Error likely means call object is already invalid/deleted
                    std::cerr << "--- Error checking call state in main loop (might be already deleted): " << err.info() << std::endl;
                    acc->currentCall = nullptr;
                }
            }
            else {
                // If pointer is already null (likely from onCallState), ensure no dangling pointer issues.
                // No action needed here if onCallState correctly nulls the pointer.
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
            catch (Error &err) { /* Ignore */
            }
            pj_thread_sleep(500);    // Give time for hangup
            delete acc->currentCall; // Final explicit cleanup
            acc->currentCall = nullptr;
        }

        acc.reset(); // Destroy account object (unique_ptr)

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

    std::cout << "Exiting application." << std::endl;
    return 0;
}
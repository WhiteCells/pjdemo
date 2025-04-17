#include <pjsua2.hpp>
#include <iostream>
#include <pjsua2/types.hpp>
#include <string>
#include <memory>
#include <limits>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono> // For timing/buffering

// --- Networking Placeholder ---
// Replace this with your actual networking code (e.g., using libcurl)
// This is highly simplified and likely needs async handling
void sendAudioToAI(const std::vector<int16_t> &audio_buffer)
{
    std::cout << "--- [AI] Sending buffer of size " << audio_buffer.size() << " samples to AI..." << std::endl;
    // TODO: Implement actual network request to your AI service
    // This should likely be asynchronous or in a separate thread pool
    // Example: curl_easy_perform(...) or similar
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate network delay
    std::cout << "--- [AI] Sending simulated complete." << std::endl;

    // --- This is where you'd handle the AI RESPONSE ---
    // For demonstration, let's pretend we received an echo after a delay
    // In a real scenario, a network callback or response handler would populate
    // the AI response buffer used by AiOutputPort.
    // For now, we won't simulate receiving audio back in this simplified example.
    // You'd need a mechanism here to push the received audio bytes into a
    // thread-safe queue accessible by AiOutputPort.
}

// Forward Declarations
class MyAccount;
class MyCall;
class AiInputPort;  // NEW
class AiOutputPort; // NEW

using namespace pj;

// =============================================================================
// Custom AudioMediaPort to capture audio going TO the AI
// =============================================================================
class AiInputPort : public AudioMediaPort
{
private:
    MyCall &call_owner;
    std::vector<int16_t> buffer;
    std::mutex buffer_mutex;
    // Example: Buffer 1 second of audio at 8kHz mono (typical VoIP)
    // Adjust sample rate based on your actual codec settings
    const size_t SAMPLE_RATE = 8000;       // Adjust if needed (e.g., 16000 for wideband)
    const size_t CHUNK_DURATION_MS = 1000; // Send every 1 second
    const size_t chunk_size_samples = (SAMPLE_RATE * CHUNK_DURATION_MS) / 1000;
    std::thread network_thread; // Simple thread for sending
    std::atomic<bool> running {true};

    // Basic non-blocking sender function
    void senderLoop()
    {
        std::vector<int16_t> local_buffer;
        while (running) {
            { // Lock scope
                std::unique_lock<std::mutex> lock(buffer_mutex);
                // Wait until buffer is full enough or stopping
                if (buffer.size() < chunk_size_samples && running) {
                    // Release lock and wait briefly if not enough data
                    lock.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Don't busy-wait
                    continue;
                }
                if (!running && buffer.empty())
                    break; // Exit if stopped and buffer empty

                // Copy data to send
                size_t elements_to_copy = std::min(buffer.size(), chunk_size_samples);
                if (elements_to_copy > 0 || !running) // Send remaining if stopping
                {
                    local_buffer.assign(buffer.begin(), buffer.begin() + elements_to_copy);
                    buffer.erase(buffer.begin(), buffer.begin() + elements_to_copy);
                }
                else {
                    // Should not happen with the condition above, but defensive coding
                    continue;
                }

            } // Lock released

            if (!local_buffer.empty()) {
                // Perform the actual network send (blocking in this thread for simplicity)
                sendAudioToAI(local_buffer);
                local_buffer.clear();
            }
        }
        std::cout << "--- [AI Input] Sender thread finished." << std::endl;
    }

public:
    AiInputPort(MyCall &owner) :
        call_owner(owner)
    {
        std::cout << "--- AiInputPort created." << std::endl;
        // Start the simple sender thread
        network_thread = std::thread(&AiInputPort::senderLoop, this);
    }

    ~AiInputPort()
    {
        std::cout << "--- Destroying AiInputPort..." << std::endl;
        running = false;
        if (network_thread.joinable()) {
            network_thread.join();
        }
        std::cout << "--- AiInputPort destroyed." << std::endl;
    }

    // Called by PJSIP's media framework when audio arrives FROM the remote party
    virtual void onFrameReceived(MediaFrame &frame) override
    {
        if (!running)
            return;

        // Lock and append the received audio data to our buffer
        std::lock_guard<std::mutex> lock(buffer_mutex);
        ByteVector pcm_data = frame.buf;
        size_t sample_count = frame.size / sizeof(int16_t); // Assuming 16-bit samples

        // Append data to the buffer
        buffer.insert(buffer.end(), pcm_data, pcm_data + sample_count);

        // Logging (optional, can be verbose)
        // std::cout << "--- [AI Input] Received frame: " << sample_count << " samples. Buffer size: " << buffer.size() << std::endl;
    }

    // We don't request frames FROM this port, PJSIP puts frames INTO it
    virtual void onFrameRequested(AudioFrame & /*frame*/) override
    {
        // This port *receives* data, it doesn't *provide* it on request.
        // std::cerr << "!!! AiInputPort::onFrameRequested called unexpectedly" << std::endl;
    }
};

// =============================================================================
// Custom AudioMediaPort to provide audio TO the remote party (from AI)
// =============================================================================
class AiOutputPort : public AudioMediaPort
{
private:
    MyCall &call_owner;
    std::vector<int16_t> ai_response_buffer;
    std::mutex buffer_mutex;
    std::atomic<bool> has_new_data {false}; // Simple flag/condition alternative

    // --- Mechanism to receive AI audio ---
    // This needs to be called by your network handling code when AI audio arrives
public:
    void receiveAiAudio(const int16_t *data, size_t sample_count)
    {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        ai_response_buffer.insert(ai_response_buffer.end(), data, data + sample_count);
        has_new_data = true; // Signal that new data is available
        std::cout << "--- [AI Output] Received " << sample_count << " samples from AI. Buffer size: " << ai_response_buffer.size() << std::endl;
    }
    void receiveAiAudio(const std::vector<int16_t> &data)
    {
        if (!data.empty()) {
            receiveAiAudio(data.data(), data.size());
        }
    }

public:
    AiOutputPort(MyCall &owner) :
        call_owner(owner)
    {
        std::cout << "--- AiOutputPort created." << std::endl;
    }

    ~AiOutputPort()
    {
        std::cout << "--- AiOutputPort destroyed." << std::endl;
    }

    // We don't receive frames INTO this port, PJSIP requests frames FROM it
    virtual void onFrameReceived(MediaFrame & /*frame*/) override
    {
        // This port *provides* data, it doesn't *receive* it like this.
        // std::cerr << "!!! AiOutputPort::onFrameReceived called unexpectedly" << std::endl;
    }

    // Called by PJSIP's media framework when it needs audio TO SEND to the remote party
    virtual void onFrameRequested(MediaFrame &frame) override
    {
        std::lock_guard<std::mutex> lock(buffer_mutex);

        size_t requested_bytes = frame.size; // PJSIP tells us how much it needs
        size_t requested_samples = requested_bytes / sizeof(int16_t);
        size_t available_samples = ai_response_buffer.size();
        int16_t *output_buf = static_cast<int16_t *>(frame.buf);

        if (available_samples >= requested_samples) {
            // Copy data from our buffer to PJSIP's frame
            std::copy(ai_response_buffer.begin(),
                      ai_response_buffer.begin() + requested_samples,
                      output_buf);
            // Remove the copied data from our buffer
            ai_response_buffer.erase(ai_response_buffer.begin(),
                                     ai_response_buffer.begin() + requested_samples);

            // std::cout << "--- [AI Output] Provided " << requested_samples << " samples. Remaining: " << ai_response_buffer.size() << std::endl;
        }
        else {
            // Not enough data from AI - provide silence
            // std::cout << "--- [AI Output] Buffer (" << available_samples <<") < requested(" << requested_samples <<"). Providing silence." << std::endl;
            std::fill(output_buf, output_buf + requested_samples, 0); // Fill with 0 (silence for PCM16)
            // Optionally copy the little data we have and fill the rest with silence
            if (available_samples > 0) {
                std::copy(ai_response_buffer.begin(), ai_response_buffer.end(), output_buf);
                ai_response_buffer.clear();
            }
        }

        // Reset flag if buffer is empty
        if (ai_response_buffer.empty()) {
            has_new_data = false;
        }

        // PJSIP expects frame.size to indicate how much data we *actually* put in the buffer.
        // It's generally assumed we fill it completely (with data or silence).
        // frame.size = requested_bytes; // Should already be set correctly by PJSIP
    }
};

// =============================================================================
// Modified MyCall
// =============================================================================
class MyCall : public Call
{
private:
    MyAccount &myAcc;
    std::unique_ptr<AiInputPort> ai_input_port;   // Use unique_ptr for RAII
    std::unique_ptr<AiOutputPort> ai_output_port; // Use unique_ptr for RAII
    AudioMedia *call_audio_media = nullptr;       // Keep track of the call's audio media

public:
    MyCall(Account &acc);
    MyCall(Account &acc, int call_id);
    ~MyCall();

    virtual void onCallState(OnCallStateParam &prm);
    virtual void onCallMediaState(OnCallMediaStateParam &prm);

    // Public method to allow external code (like network receiver) to feed AI audio
    void feedAiAudioResponse(const std::vector<int16_t> &audio_data)
    {
        if (ai_output_port) {
            ai_output_port->receiveAiAudio(audio_data);
        }
        else {
            std::cerr << "!!! Warning: Tried to feed AI audio but AiOutputPort is not active." << std::endl;
        }
    }
};

// -----------------------------------------------------------------------------
// MyAccount (Small change needed in onIncomingCall for potential errors)
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
        CallOpParam prm;

        if (currentCall) {
            std::cout << "*** Another call is active. Rejecting incoming call ID "
                      << iprm.callId << " from " << iprm.rdata.srcAddress << std::endl;
            // Use a temporary Call object to reject
            Call *rejectCall = nullptr; // Use base Call pointer
            try {
                rejectCall = new Call(*this, iprm.callId); // Create using incoming ID
                prm.statusCode = PJSIP_SC_BUSY_HERE;
                rejectCall->hangup(prm);
                delete rejectCall; // Delete the temporary wrapper
                rejectCall = nullptr;
            }
            catch (Error &err) {
                std::cerr << "!!! Error rejecting call ID " << iprm.callId << ": " << err.info() << std::endl;
                delete rejectCall; // Attempt cleanup even on error
            }
            return;
        }

        // --- If not busy, proceed with answering ---
        std::cout << "*** Incoming Call: " << iprm.callId << " from " << iprm.rdata.srcAddress << std::endl;

        MyCall *call = nullptr; // Use MyCall pointer now
        try {
            call = new MyCall(*this, iprm.callId);
            std::cout << ">>> Auto-answering incoming call..." << std::endl;
            prm.statusCode = PJSIP_SC_OK;
            call->answer(prm);
            currentCall = call; // Track the active call
        }
        catch (Error &err) {
            std::cerr << "!!! Failed to create or answer call ID " << iprm.callId << ": " << err.info() << std::endl;
            delete call;           // Clean up failed call object
            currentCall = nullptr; // Ensure currentCall is null
        }
    }
};

// -----------------------------------------------------------------------------
// MyCall Method Implementations
// -----------------------------------------------------------------------------
MyCall::MyCall(Account &acc) :
    Call(acc), myAcc(dynamic_cast<MyAccount &>(acc)) {}

MyCall::MyCall(Account &acc, int call_id) :
    Call(acc, call_id), myAcc(dynamic_cast<MyAccount &>(acc)) {}

// Destructor needs to clean up ports if they exist
MyCall::~MyCall()
{
    std::cout << "--- MyCall Destructor for Call ID " << getId() << "---" << std::endl;

    // Stop media streams explicitly before deleting ports
    if (call_audio_media) {
        AudDevManager &mgr = Endpoint::instance().audDevManager();
        try {
            if (ai_input_port)
                call_audio_media->stopTransmit(*ai_input_port);
        }
        catch (const Error &e) {
            std::cerr << "Error stopping tx to ai_input_port: " << e.info() << std::endl;
        }
        try {
            if (ai_output_port)
                ai_output_port->stopTransmit(*call_audio_media);
        }
        catch (const Error &e) {
            std::cerr << "Error stopping tx from ai_output_port: " << e.info() << std::endl;
        }
    }

    // unique_ptr will handle deletion of the ports automatically here
    // ai_input_port.reset();
    // ai_output_port.reset();

    if (myAcc.currentCall == this) {
        myAcc.currentCall = nullptr;
        std::cout << "--- Call object destroyed, account call pointer cleared." << std::endl;
    }
    else {
        std::cout << "--- Call object destroyed (was not the account's active call)." << std::endl;
    }
    call_audio_media = nullptr; // Clear pointer
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

            // Stop media and destroy ports explicitly on disconnect
            if (call_audio_media) {
                try {
                    if (ai_input_port)
                        call_audio_media->stopTransmit(*ai_input_port);
                }
                catch (const Error &e) {
                    std::cerr << "Error stopping tx to ai_input_port: " << e.info() << std::endl;
                }
                try {
                    if (ai_output_port)
                        ai_output_port->stopTransmit(*call_audio_media);
                }
                catch (const Error &e) {
                    std::cerr << "Error stopping tx from ai_output_port: " << e.info() << std::endl;
                }
            }
            ai_input_port.reset(); // Destroy ports now
            ai_output_port.reset();
            call_audio_media = nullptr;

            if (myAcc.currentCall == this) {
                // Important: Signal app that call is over.
                // Let the main loop or owner handle deletion of the MyCall object itself.
                // Avoid `delete this;` in callbacks.
                myAcc.currentCall = nullptr;
                std::cout << "--- Account's active call pointer cleared due to DISCONNECTED state." << std::endl;
            }
        }
        else if (ci.state == PJSIP_INV_STATE_CONFIRMED) {
            std::cout << "--- Call " << ci.id << " Connected/Confirmed." << std::endl;
            // Media setup happens in onCallMediaState
        }
    }
    catch (const Error &err) {
        std::cerr << "!!! Error getting call info in onCallState: " << err.info() << std::endl;
        // If getInfo fails, the call might already be invalid
        if (myAcc.currentCall == this) {
            myAcc.currentCall = nullptr; // Assume invalid, prevent dangling pointer use
        }
    }
}

// *** CRITICAL CHANGE: Reroute media here ***
void MyCall::onCallMediaState(OnCallMediaStateParam &prm)
{
    PJ_UNUSED_ARG(prm);
    try {
        CallInfo ci = getInfo();
        std::cout << "*** Call " << ci.id << " Media State Changed" << std::endl;

        for (unsigned i = 0; i < ci.media.size(); ++i) {
            if (ci.media[i].type == PJMEDIA_TYPE_AUDIO) {
                call_audio_media = static_cast<AudioMedia *>(getMedia(i)); // Store pointer

                if (ci.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
                    if (!call_audio_media) {
                        std::cerr << "!!! Error: Audio media is active but getMedia(" << i << ") returned null!" << std::endl;
                        continue;
                    }
                    std::cout << "--- Audio media is ACTIVE for call " << ci.id << ". Setting up AI ports." << std::endl;

                    // ** STOP Default Connection **
                    // We are NOT connecting to default sound devices anymore.

                    // ** Create and Connect Custom Ports **
                    try {
                        // Ensure ports are created only once
                        if (!ai_input_port) {
                            ai_input_port = std::make_unique<AiInputPort>(*this);
                        }
                        if (!ai_output_port) {
                            ai_output_port = std::make_unique<AiOutputPort>(*this);
                        }

                        // Connect call media to our custom ports:
                        // 1. Audio FROM Remote Party --> Goes INTO AiInputPort
                        call_audio_media->startTransmit(*ai_input_port);
                        std::cout << "--- Connected CallMedia Tx -> AiInputPort" << std::endl;

                        // 2. Audio FROM AiOutputPort --> Goes TO Remote Party
                        ai_output_port->startTransmit(*call_audio_media);
                        std::cout << "--- Connected AiOutputPort Tx -> CallMedia" << std::endl;

                        std::cout << "--- AI Audio Bridge connected for call " << ci.id << std::endl;
                    }
                    catch (Error &err) {
                        std::cerr << "!!! Failed to connect AI audio ports for call " << ci.id << ": " << err.info() << std::endl;
                        // Cleanup partially created ports if error occurs
                        ai_input_port.reset();
                        ai_output_port.reset();
                        call_audio_media = nullptr;
                    }
                }
                else if (ci.media[i].status == PJSUA_CALL_MEDIA_NONE || ci.media[i].status == PJSUA_CALL_MEDIA_ERROR) {
                    std::cout << "--- Audio media is NOT ACTIVE (status: " << ci.media[i].status << ") for call " << ci.id << "." << std::endl;
                    // Stop transmissions and destroy ports if they exist
                    if (call_audio_media) {
                        try {
                            if (ai_input_port)
                                call_audio_media->stopTransmit(*ai_input_port);
                        }
                        catch (const Error &e) {
                            std::cerr << "Error stopping tx to ai_input_port: " << e.info() << std::endl;
                        }
                        try {
                            if (ai_output_port)
                                ai_output_port->stopTransmit(*call_audio_media);
                        }
                        catch (const Error &e) {
                            std::cerr << "Error stopping tx from ai_output_port: " << e.info() << std::endl;
                        }
                    }
                    ai_input_port.reset(); // Destroy ports
                    ai_output_port.reset();
                    call_audio_media = nullptr; // Clear media pointer
                }
                else {
                    std::cout << "--- Audio media state is intermediate (status: " << ci.media[i].status << ") for call " << ci.id << "." << std::endl;
                    // Usually no action needed here, wait for ACTIVE or NONE/ERROR
                }
            }
            else if (ci.media[i].type != PJMEDIA_TYPE_AUDIO) {
                std::cout << "--- Non-audio media stream detected (type: " << ci.media[i].type << ")" << std::endl;
            }
        } // end for loop over media
    }
    catch (const Error &err) {
        std::cerr << "!!! Error in onCallMediaState: " << err.info() << std::endl;
        // If getInfo fails, the call might be invalid, attempt cleanup
        ai_input_port.reset();
        ai_output_port.reset();
        call_audio_media = nullptr;
    }
}

// -----------------------------------------------------------------------------
// Main Application Logic (Mostly unchanged, but add includes)
// -----------------------------------------------------------------------------
// ADD INCLUDES AT TOP: <vector>, <thread>, <mutex>, <condition_variable>, <atomic>, <chrono>

int main()
{
    Endpoint ep;
    std::unique_ptr<MyAccount> acc; // Keep using unique_ptr for account

    try {
        // 1. Initialize Endpoint (Same as before)
        std::cout << "Initializing Endpoint..." << std::endl;
        ep.libCreate();
        EpConfig ep_cfg;
#ifdef STUN_SERVER
        ep_cfg.uaConfig.stunServer.push_back(STUN_SERVER);
#endif
        // Increase media threads if AI processing + networking is heavy
        // ep_cfg.medConfig.threadCnt = 2; // Example
        ep.libInit(ep_cfg);

        // 2. Create SIP Transport (Same as before)
        TransportConfig tcfg;
        tcfg.port = 5060; // Or let PJSIP choose: tcfg.port = 0;
        TransportId tid = ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);
        std::cout << "--- SIP UDP Transport created on port " << ep.transportGetInfo(tid).localName << std::endl;

        // 3. Start the library (Same as before)
        ep.libStart();
        std::cout << "*** Pjsua2 library started." << std::endl;

        // Set audio devices - **IMPORTANT**: Even though we intercept,
        // PJSIP might still need *some* valid (or null) device selected initially.
        // Using the null device is often safest when doing full interception.
        try {
            std::cout << "--- Setting NULL audio device for background operation." << std::endl;
            ep.audDevManager().setNullDev();
            // AudDevManager &mgr = ep.audDevManager();
            // std::cout << "--- Default capture device: " << mgr.getCaptureDev() << std::endl;
            // std::cout << "--- Default playback device: " << mgr.getPlaybackDev() << std::endl;
        }
        catch (Error &err) {
            std::cerr << "!!! Error setting audio devices: " << err.info() << std::endl;
            // Attempting to continue without audio device might fail later
        }

        // 4. Configure Account (Same as before)
        AccountConfig acc_cfg;
        acc_cfg.idUri = "sip:" SIP_USER "@" SIP_DOMAIN;
        acc_cfg.regConfig.registrarUri = SIP_REGISTRAR;
        AuthCredInfo cred("digest", "*", SIP_USER, 0, SIP_PASSWORD);
        acc_cfg.sipConfig.authCreds.push_back(cred);
        // ** Optional: Specify codecs if needed **
        // acc_cfg.medConfig.codecSettings // ... configure codecs if necessary

        // 5. Create and Register Account (Same as before)
        acc = std::make_unique<MyAccount>();
        acc->create(acc_cfg);
        std::cout << "*** Account created for " << acc_cfg.idUri << ". Registering..." << std::endl;

        // --- Main Interaction Loop (Same as before) ---
        std::cout << "\nCommands:\n";
        std::cout << "  m <sip:user@domain>  : Make call\n";
        std::cout << "  h                    : Hangup call\n";
        // Add a command to simulate AI response for testing
        std::cout << "  t                    : Simulate Test AI response (echo)\n";
        std::cout << "  q                    : Quit\n"
                  << std::endl;

        char cmd[100];
        while (true) {
            std::cout << "> ";
            if (!std::cin.getline(cmd, sizeof(cmd))) {
                if (std::cin.eof())
                    break;
                std::cin.clear();
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
                if (command_line.length() < 3 || command_line[1] != ' ') { /*... format ...*/
                    continue;
                }
                std::string target_uri = command_line.substr(2);
                std::cout << ">>> Placing call to: " << target_uri << std::endl;

                // Use the appropriate MyCall constructor
                MyCall *call = new MyCall(*acc); // Outgoing call constructor
                CallOpParam prm(true);

                try {
                    call->makeCall(target_uri, prm);
                    acc->currentCall = call; // Track it
                }
                catch (Error &err) {
                    std::cerr << "!!! Failed to make call: " << err.info() << std::endl;
                    delete call; // IMPORTANT: Delete if makeCall fails
                }
            }
            else if (action == 'h') {
                if (!acc->currentCall) { /* ... no call ... */
                    continue;
                }
                std::cout << ">>> Hanging up call..." << std::endl;
                CallOpParam prm;
                try {
                    acc->currentCall->hangup(prm);
                    // Deletion is handled when DISCONNECTED state occurs
                    // or during main loop cleanup check
                }
                catch (Error &err) {
                    std::cerr << "!!! Failed to hang up call: " << err.info() << std::endl;
                    // Consider forced cleanup if hangup fails badly
                    // delete acc->currentCall;
                    // acc->currentCall = nullptr;
                }
            }
            else { /* ... unknown command ... */
            }

            // --- Cleanup check for disconnected calls in main loop ---
            // This ensures the MyCall object (and its processor/ports) is deleted
            // even if the onCallState callback somehow didn't lead to deletion.
            if (acc->currentCall) {
                try {
                    CallInfo ci = acc->currentCall->getInfo();
                    if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
                        std::cout << "--- Main loop cleanup: Deleting disconnected call object." << std::endl;
                        delete acc->currentCall; // This triggers ~MyCall()
                        acc->currentCall = nullptr;
                    }
                }
                catch (Error &err) {
                    // Error likely means call object is already invalid/deleted
                    if (err.status != PJ_SUCCESS) { // Check if it's a real error vs just invalid ID
                        std::cerr << "--- Error checking call state in main loop: " << err.info() << std::endl;
                    }
                    // Ensure pointer is null if check fails or object gone
                    acc->currentCall = nullptr;
                }
            }
        } // End while loop

        // 6. Cleanup
        std::cout << "Shutting down..." << std::endl;
        // Hang up active call before destroying account/library
        if (acc->currentCall) {
            std::cout << "--- Hanging up active call before exit..." << std::endl;
            CallOpParam prm;
            try {
                acc->currentCall->hangup(prm);
            }
            catch (Error &err) { /* Ignore */
            }
            pj_thread_sleep(500); // Give hangup time
            std::cout << "--- Deleting final call object..." << std::endl;
            delete acc->currentCall; // Triggers ~MyCall cleanup
            acc->currentCall = nullptr;
        }

        // Account goes out of scope via unique_ptr
        acc.reset();
        std::cout << "--- Account destroyed." << std::endl;

        // Destroy library
        ep.libDestroy();
        std::cout << "*** Pjsua2 library destroyed." << std::endl;
    }
    catch (Error &err) {
        std::cerr << "!!! Exception: " << err.info() << std::endl;
        // Ensure library is destroyed on error
        try {
            if (ep.libGetState() != PJSUA_STATE_NULL)
                ep.libDestroy();
        }
        catch (...) {
        }
        return 1;
    }

    std::cout << "Exiting application." << std::endl;

    auto a = AudioMediaPort {};
    return 0;
}
#include <pjsua2.hpp>
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <limits> // Already included
#include <functional>

using namespace pj;

// --- AI Interaction Placeholders ---
// Replace these with your actual AI client implementation
namespace AiServiceClient {
// Structure to hold audio data (e.g., raw PCM)
struct AudioChunk
{
    std::vector<int16_t> samples;
    // Add metadata if needed (sample rate, channels etc.)
};

// Function to send audio data to the AI (runs in a separate thread)
// This function will receive audio chunks and should make network calls.
// It needs a way to signal back when AI response audio is ready.
void sendAudioToAi(const AudioChunk &chunk, std::function<void(AudioChunk)> on_response_received);

// Example: A queue to hold responses from the AI service
std::queue<AudioChunk> ai_response_queue;
std::mutex ai_response_mutex;
std::condition_variable ai_response_cv;

// This callback would be invoked by your network client when AI audio arrives
void internal_ai_response_handler(AudioChunk response_chunk)
{
    std::lock_guard<std::mutex> lock(ai_response_mutex);
    ai_response_queue.push(std::move(response_chunk));
    ai_response_cv.notify_one(); // Signal the player thread
    std::cout << "--- AI response received and queued." << std::endl;
}

// Example implementation of the send function
void sendAudioToAi(const AudioChunk &chunk, std::function<void(AudioChunk)> on_response_received)
{
    std::cout << "--- Sending chunk with " << chunk.samples.size() << " samples to AI (Placeholder)..." << std::endl;
    // --- TODO: Implement actual network call to your AI service here ---
    // Send chunk.samples.data(), chunk.samples.size() * sizeof(int16_t)
    // This call should be asynchronous or run in its own thread managed
    // by your AI client library.

    // --- TODO: When the AI responds with audio, call the callback: ---
    // Example: Simulating a delayed response
    std::thread response_simulator([chunk, on_response_received]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Simulate network/AI delay
        // Create a dummy response (e.g., echo back or silent chunk)
        AudioChunk response = chunk; // Simple echo for demo
        // Or create silence: AudioChunk response; response.samples.assign(8000, 0); // 1 sec silence at 8k
        std::cout << "--- AI processing finished (Placeholder). Calling response handler." << std::endl;
        on_response_received(std::move(response));
    });
    response_simulator.detach(); // Let the thread run independently
}

} // namespace AiServiceClient

// --- Custom Audio Media Port for Playing AI Responses ---
class AudioAiPlayer : public AudioMediaPort
{
private:
    std::mutex queue_mutex;
    std::condition_variable data_cv;
    std::queue<AiServiceClient::AudioChunk> audio_queue;
    std::vector<char> current_buffer;
    size_t current_pos = 0;
    unsigned _clockRate;
    unsigned _channelCount;
    unsigned _samplesPerFrame;
    unsigned _bitsPerSample;
    pjmedia_format_id _formatId; // Store the format ID

public:
    AudioAiPlayer(unsigned clockRate = 8000, unsigned channelCount = 1, unsigned samplesPerFrame = 160, unsigned bitsPerSample = 16) :
        _clockRate(clockRate), _channelCount(channelCount), _samplesPerFrame(samplesPerFrame), _bitsPerSample(bitsPerSample)
    {
        // Determine format ID based on parameters (simplified)
        if (clockRate == 8000 && bitsPerSample == 16) {
            _formatId = PJMEDIA_FORMAT_L16;
        }
        else {
            _formatId = PJMEDIA_FORMAT_L16; // Default assumption
            std::cerr << "Warning: AudioAiPlayer using default L16 format ID, clock rate/bit depth matching not fully implemented." << std::endl;
        }
        std::cout << "AudioAiPlayer created (Rate: " << _clockRate << ", Ch: " << _channelCount << ", SpF: " << _samplesPerFrame << ")" << std::endl;
    }

    ~AudioAiPlayer()
    {
        std::cout << "AudioAiPlayer destroyed." << std::endl;
        // Ensure any waiting threads are woken up
        data_cv.notify_all();
    }

    // Function called by external thread (AI response handler) to add audio
    void addAudioChunk(AiServiceClient::AudioChunk chunk)
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        audio_queue.push(std::move(chunk));
        data_cv.notify_one(); // Notify the PJSIP thread that data is available
    }

    // PJSIP calls this when it needs audio data *from* this port to send
    virtual void onFrameRequested(MediaFrame &frame) override
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // Cast frame data pointer correctly
        ByteVector frame_buffer = frame.buf;
        size_t frame_capacity_samples = frame.size / sizeof(int16_t); // Samples the frame can hold

        size_t samples_filled = 0;
        while (samples_filled < frame_capacity_samples) {
            if (current_pos >= current_buffer.size()) {
                // Current buffer is exhausted, try to get a new one from the queue
                current_buffer.clear();
                current_pos = 0;

                if (!audio_queue.empty()) {
                    current_buffer = std::move(audio_queue.front());
                    audio_queue.pop();
                    // std::cout << "AI Player: Popped chunk with " << current_buffer.size() << " samples." << std::endl;
                }
                else {
                    // No data available, wait briefly or fill with silence
                    // Filling with silence is generally better to keep media flowing
                    // std::cout << "AI Player: Queue empty. Waiting..." << std::endl;
                    // lock.unlock(); // Unlock before waiting (optional, depends on desired blocking behavior)
                    // std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Small sleep
                    // lock.lock();   // Re-lock
                    // continue; // Retry getting data

                    // Alternative: Fill remaining frame with silence
                    size_t silence_samples = frame_capacity_samples - samples_filled;
                    // std::cout << "AI Player: Queue empty. Filling " << silence_samples << " samples with silence." << std::endl;
                    std::fill_n(frame_buffer + samples_filled, silence_samples, 0);
                    samples_filled += silence_samples;
                    break; // Frame is full (with silence)
                }
            }

            // Copy data from current_buffer to frame
            size_t samples_to_copy = std::min(frame_capacity_samples - samples_filled, current_buffer.size() - current_pos);
            if (samples_to_copy > 0) {
                memcpy(frame_buffer + samples_filled, current_buffer.data() + current_pos, samples_to_copy * sizeof(int16_t));
                samples_filled += samples_to_copy;
                current_pos += samples_to_copy;
            }
        }

        // Set frame properties
        frame.type = PJMEDIA_FRAME_TYPE_AUDIO;
        frame.size = samples_filled * sizeof(int16_t); // Actual size filled
        // frame.timestamp is usually handled by the PJSIP core when transmitting
    }
};

// --- Custom Audio Media Port for Intercepting Customer Audio ---
class AudioAiProcessor : public AudioMediaPort
{
private:
    std::vector<int16_t> buffer;
    size_t target_chunk_size_samples; // e.g., 1 second worth of samples
    unsigned _clockRate;
    unsigned _channelCount;
    unsigned _samplesPerFrame;
    unsigned _bitsPerSample;
    std::shared_ptr<AudioAiPlayer> ai_player; // Reference to the player to send responses to

public:
    // Takes a pointer to the AI player to send responses back
    AudioAiProcessor(std::shared_ptr<AudioAiPlayer> player, unsigned clockRate = 8000, unsigned channelCount = 1, unsigned samplesPerFrame = 160, unsigned bitsPerSample = 16) :
        ai_player(player), _clockRate(clockRate), _channelCount(channelCount), _samplesPerFrame(samplesPerFrame), _bitsPerSample(bitsPerSample)
    {
        // Example: Send chunks of ~0.5 seconds
        target_chunk_size_samples = clockRate * channelCount / 2;
        buffer.reserve(target_chunk_size_samples * 2); // Pre-allocate some space
        std::cout << "AudioAiProcessor created (Target chunk: " << target_chunk_size_samples << " samples)" << std::endl;
    }
    ~AudioAiProcessor()
    {
        std::cout << "AudioAiProcessor destroyed." << std::endl;
        // Send any remaining buffered data? Maybe not desirable on destruction.
    }

    // PJSIP calls this when it receives audio data *for* this port
    virtual void onFrameReceived(MediaFrame &frame) override
    {
        if (frame.type != PJMEDIA_FRAME_TYPE_AUDIO || frame.size == 0) {
            return;
        }

        // Append data to internal buffer
        size_t incoming_samples = frame.size / sizeof(int16_t);
        const int16_t *frame_data = static_cast<const int16_t *>(frame.buf);
        buffer.insert(buffer.end(), frame_data, frame_data + incoming_samples);

        // Check if buffer has enough data for a chunk
        while (buffer.size() >= target_chunk_size_samples) {
            AiServiceClient::AudioChunk chunk;
            // Copy the chunk data
            chunk.samples.assign(buffer.begin(), buffer.begin() + target_chunk_size_samples);
            // Remove the chunk data from the buffer
            buffer.erase(buffer.begin(), buffer.begin() + target_chunk_size_samples);

            // std::cout << "Processor: Extracted chunk (" << chunk.samples.size() << "), buffer remaining: " << buffer.size() << std::endl;

            // Send chunk to AI asynchronously using the placeholder function
            // Pass the response handler lambda
            std::thread ai_thread([this, chunk]() {
                AiServiceClient::sendAudioToAi(chunk, [this](AiServiceClient::AudioChunk response_chunk) {
                    // This lambda is the callback executed when AI response is ready
                    if (this->ai_player) { // Check if player still exists
                                           // Add the AI's response audio to the player's queue
                        this->ai_player->addAudioChunk(std::move(response_chunk));
                    }
                });
            });
            ai_thread.detach(); // Detach the thread to let it run independently
        }
    }
};

// --- Forward Declarations ---
class MyAccount;
class MyCall; // Already forward declared

// --- MyCall Definition ---
class MyCall : public Call
{
private:
    MyAccount &myAcc;
    std::shared_ptr<AudioAiPlayer> ai_player;       // Smart pointer for the player
    std::shared_ptr<AudioAiProcessor> ai_processor; // Smart pointer for the processor
    pjmedia_port *call_audio_port = nullptr;        // Store underlying port if needed

public:
    MyCall(Account &acc);
    MyCall(Account &acc, int call_id);
    ~MyCall();

    virtual void onCallState(OnCallStateParam &prm);
    virtual void onCallMediaState(OnCallMediaStateParam &prm);
};

// --- MyAccount Definition (mostly unchanged) ---
class MyAccount : public Account
{
public:
    // Use unique_ptr for automatic memory management of the active call object
    // This helps prevent leaks if call destruction logic fails.
    std::unique_ptr<MyCall> currentCall = nullptr;

    MyAccount() :
        Account() {}
    virtual ~MyAccount()
    {
        // unique_ptr will automatically delete the call if it exists
        if (currentCall) {
            std::cout << "MyAccount destructor: Cleaning up active call." << std::endl;
        }
    }

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

            // Use a temporary raw pointer for rejection, manage carefully
            MyCall *rejectCall = nullptr;
            try {
                rejectCall = new MyCall(*this, iprm.callId);
                prm.statusCode = PJSIP_SC_BUSY_HERE;
                rejectCall->hangup(prm);
                // PJSIP manages the underlying call resource after hangup.
                // Delete the C++ wrapper object.
                delete rejectCall; // Delete immediately after hangup initiation
                rejectCall = nullptr;
            }
            catch (Error &err) {
                std::cerr << "!!! Error rejecting call ID " << iprm.callId << ": " << err.info() << std::endl;
                if (rejectCall) { // Clean up if object created but hangup failed
                    std::cerr << "!!! Cleaning up rejectCall object after rejection error." << std::endl;
                    delete rejectCall;
                }
            }
            return;
        }

        std::cout << "*** Incoming Call: " << iprm.callId << " from " << iprm.rdata.srcAddress << std::endl;

        MyCall *new_call_ptr = nullptr; // Temporary raw pointer
        try {
            // Create the call object
            new_call_ptr = new MyCall(*this, iprm.callId);

            std::cout << ">>> Auto-answering incoming call..." << std::endl;
            prm.statusCode = PJSIP_SC_OK;
            new_call_ptr->answer(prm);

            // Transfer ownership to the unique_ptr if answer succeeded
            currentCall.reset(new_call_ptr);
            std::cout << "--- Incoming call answered and tracked by unique_ptr." << std::endl;
        }
        catch (Error &err) {
            std::cerr << "!!! Failed to create or answer call ID " << iprm.callId << ": " << err.info() << std::endl;
            // Clean up if answer failed but object was created
            if (new_call_ptr) {
                delete new_call_ptr; // Delete the failed call object
            }
            currentCall.reset(); // Ensure unique_ptr is null
        }
    }
};

// --- MyCall Method Implementations ---
MyCall::MyCall(Account &acc) :
    Call(acc), myAcc(dynamic_cast<MyAccount &>(acc)) {}

MyCall::MyCall(Account &acc, int call_id) :
    Call(acc, call_id), myAcc(dynamic_cast<MyAccount &>(acc)) {}

MyCall::~MyCall()
{
    std::cout << "--- MyCall object destructor called for call ID " << getId() << "." << std::endl;
    // Stop media connections if they are active
    try {
        if (ai_processor && call_audio_port) {
            AudioMediaPort call_aud_med(call_audio_port); // Wrap the port temporarily
            std::cout << "--- Stopping transmission from call media to AI processor." << std::endl;
            call_aud_med.stopTransmit(*ai_processor);
        }
        if (ai_player && call_audio_port) {
            AudioMediaPort call_aud_med(call_audio_port);
            std::cout << "--- Stopping transmission from AI player to call media." << std::endl;
            ai_player->stopTransmit(call_aud_med);
        }
    }
    catch (const Error &e) {
        std::cerr << "!!! Error stopping media transmission in MyCall destructor: " << e.info() << std::endl;
    }
    catch (...) {
        std::cerr << "!!! Unknown error stopping media transmission in MyCall destructor." << std::endl;
    }

    // Smart pointers (ai_player, ai_processor) will automatically delete
    // the objects they own when the MyCall object is destroyed.
    std::cout << "--- AI media ports will be released by smart pointers." << std::endl;

    // We don't manage myAcc.currentCall here anymore.
    // The unique_ptr in MyAccount handles the lifetime of the active call object.
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

            // The unique_ptr in MyAccount is responsible for deletion.
            // We just need to signal that the call is over by resetting the pointer.
            // Check if this call instance is the one currently held by the account.
            MyAccount *acc_ptr = dynamic_cast<MyAccount *>(&myAcc); // Get raw ptr for comparison
            if (acc_ptr && acc_ptr->currentCall.get() == this) {
                std::cout << "--- Resetting account's unique_ptr due to DISCONNECTED state." << std::endl;
                acc_ptr->currentCall.reset(); // Release ownership, triggers ~MyCall
            }
            else {
                std::cout << "--- Disconnected call was not the account's active call or account ptr invalid." << std::endl;
                // If it's not the main call (e.g., the temporary reject call),
                // it should have already been deleted in onIncomingCall.
                // If we reach here for a non-currentCall, it might indicate a logic issue.
            }
        }
        else if (ci.state == PJSIP_INV_STATE_CONFIRMED) {
            std::cout << "--- Call " << ci.id << " Connected/Confirmed." << std::endl;
            // Media setup happens in onCallMediaState
        }
    }
    catch (const Error &err) {
        std::cerr << "!!! Error getting call info in onCallState: " << err.info() << std::endl;
        // If getting info fails, the call might already be invalid. Try to clean up.
        MyAccount *acc_ptr = dynamic_cast<MyAccount *>(&myAcc);
        if (acc_ptr && acc_ptr->currentCall.get() == this) {
            std::cerr << "--- Error in onCallState, assuming call invalid, resetting account pointer." << std::endl;
            acc_ptr->currentCall.reset();
        }
    }
}

void MyCall::onCallMediaState(OnCallMediaStateParam &prm)
{
    PJ_UNUSED_ARG(prm);
    try {
        CallInfo ci = getInfo();
        std::cout << "*** Call " << ci.id << " Media State Changed" << std::endl;

        for (unsigned i = 0; i < ci.media.size(); ++i) {
            if (ci.media[i].type == PJMEDIA_TYPE_AUDIO) {
                AudioMedia *call_aud_med = nullptr;
                try {
                    call_aud_med = static_cast<AudioMedia *>(getMedia(i));
                    // Store the underlying port pointer if needed for later cleanup
                    call_audio_port = call_aud_med->getMediaPort();
                }
                catch (const Error &err) {
                    std::cerr << "!!! Failed to get audio media for index " << i << ": " << err.info() << std::endl;
                    continue; // Skip to next media stream
                }

                if (ci.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
                    std::cout << "--- Audio media is ACTIVE for call " << ci.id << ". Setting up AI pipeline." << std::endl;

                    // Get format details from the established call media
                    // This is crucial for creating compatible custom ports
                    PortInfo call_pi = call_aud_med->getPortInfo();
                    unsigned clockRate = call_pi.clockRate;
                    unsigned channelCount = call_pi.channelCount;
                    unsigned samplesPerFrame = call_pi.samplesPerFrame;
                    unsigned bitsPerSample = call_pi.bitsPerSample;

                    std::cout << "--- Call Media Format: Rate=" << clockRate << ", Ch=" << channelCount
                              << ", SpF=" << samplesPerFrame << ", BpS=" << bitsPerSample << std::endl;

                    // Create the AI Player and Processor (use make_shared)
                    // Pass the correct format details obtained from the call media
                    ai_player = std::make_shared<AudioAiPlayer>(clockRate, channelCount, samplesPerFrame, bitsPerSample);
                    ai_processor = std::make_shared<AudioAiProcessor>(ai_player, clockRate, channelCount, samplesPerFrame, bitsPerSample);

                    try {
                        // Connect call audio output TO AI Processor input
                        std::cout << "--- Connecting Call Media Output -> AI Processor Input" << std::endl;
                        call_aud_med->startTransmit(*ai_processor);

                        // Connect AI Player output TO Call audio input
                        std::cout << "--- Connecting AI Player Output -> Call Media Input" << std::endl;
                        ai_player->startTransmit(*call_aud_med);

                        std::cout << "--- AI Audio pipeline connected for call " << ci.id << std::endl;
                    }
                    catch (Error &err) {
                        std::cerr << "!!! Failed to connect AI audio pipeline for call " << ci.id << ": " << err.info() << std::endl;
                        // Cleanup partially created ports if connection fails
                        ai_player.reset();
                        ai_processor.reset();
                    }
                }
                else if (ci.media[i].status == PJSUA_CALL_MEDIA_NONE || ci.media[i].status == PJSUA_CALL_MEDIA_ERROR) {
                    std::cout << "--- Audio media is INACTIVE (status: " << ci.media[i].status << ") for call " << ci.id << ". Tearing down AI pipeline." << std::endl;
                    // Stop transmissions (best effort)
                    try {
                        if (ai_processor && call_audio_port) {
                            AudioMedia call_aud_med_wrap(call_audio_port);
                            call_aud_med_wrap.stopTransmit(*ai_processor);
                        }
                        if (ai_player && call_audio_port) {
                            AudioMedia call_aud_med_wrap(call_audio_port);
                            ai_player->stopTransmit(call_aud_med_wrap);
                        }
                    }
                    catch (const Error &e) {
                        std::cerr << "!!! Error stopping media transmission during teardown: " << e.info() << std::endl;
                    }
                    // Release the custom media ports
                    ai_player.reset();
                    ai_processor.reset();
                    call_audio_port = nullptr; // Clear stored port pointer
                    std::cout << "--- AI Audio pipeline disconnected and ports released." << std::endl;
                }
                else {
                    std::cout << "--- Audio media has status: " << ci.media[i].status << " (Not Active/Not Inactive)" << std::endl;
                }
            }
            else if (ci.media[i].type != PJMEDIA_TYPE_AUDIO) {
                std::cout << "--- Non-audio media stream detected (type: " << ci.media[i].type << ", status: " << ci.media[i].status << ")" << std::endl;
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
    // Keep MyAccount itself managed by unique_ptr in main
    std::unique_ptr<MyAccount> acc_main_ptr;

    try {
        // 1. Initialize Endpoint
        std::cout << "Initializing Endpoint..." << std::endl;
        ep.libCreate();
        EpConfig ep_cfg;
        ep_cfg.medConfig.clockRate = 8000; // Set preferred clock rate if needed (e.g., 8000 for G711)
        ep_cfg.medConfig.channelCount = 1;
        // ep_cfg.medConfig.noVad = true; // Consider disabling pjmedia internal VAD if doing custom VAD
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

        // Set audio devices (important: AI pipeline bypasses default devices)
        try {
            AudDevManager &mgr = ep.audDevManager();
            std::cout << "--- Using NULL audio device as AI pipeline bypasses default devices." << std::endl;
            // Using the null device prevents PJSIP from trying to use mic/speaker
            // simultaneously with our custom media ports, which can cause issues.
            mgr.setNullDev();

            // We don't need capture/playback devices if the AI handles everything.
            // If you wanted the *operator* to hear the customer or speak,
            // you would need a more complex setup involving AudioMediaSplitter
            // or multiple connections.
        }
        catch (Error &err) {
            std::cerr << "!!! Error setting NULL audio device: " << err.info() << std::endl;
            // Try to continue, but audio might not work correctly.
        }

        // 4. Configure Account
        AccountConfig acc_cfg;
        acc_cfg.idUri = "sip:" SIP_USER "@" SIP_DOMAIN;
        acc_cfg.regConfig.registrarUri = SIP_REGISTRAR;
        AuthCredInfo cred("digest", "*", SIP_USER, 0, SIP_PASSWORD);
        acc_cfg.sipConfig.authCreds.push_back(cred);
        // Add relevant codecs if needed (e.g., PCMA/PCMU for G711)
        // acc_cfg.medConfig.codecPriorities.clear(); // Optional: clear defaults
        // CodecPriority cp;
        // cp.priority = 200; cp.codecId = "PCMA/8000/1"; acc_cfg.medConfig.codecPriorities.push_back(cp);
        // cp.priority = 190; cp.codecId = "PCMU/8000/1"; acc_cfg.medConfig.codecPriorities.push_back(cp);

        // 5. Create and Register Account
        acc_main_ptr = std::make_unique<MyAccount>();
        acc_main_ptr->create(acc_cfg);
        std::cout << "*** Account created for " << acc_cfg.idUri << ". Registering..." << std::endl;

        // --- Main Interaction Loop ---
        std::cout << "\nCommands:\n";
        std::cout << "  m <sip:user@domain>  : Make call to customer (AI will interact)\n";
        std::cout << "  h                    : Hangup call\n";
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
                if (acc_main_ptr->currentCall) {
                    std::cerr << "!!! Cannot make a new call. A call is already active." << std::endl;
                    continue;
                }
                if (command_line.length() < 3 || command_line[1] != ' ') {
                    std::cerr << "!!! Invalid format. Use: m <sip:user@domain>" << std::endl;
                    continue;
                }
                std::string target_uri = command_line.substr(2);
                std::cout << ">>> Placing call to: " << target_uri << std::endl;

                MyCall *new_call_ptr = nullptr; // Temp raw pointer
                try {
                    new_call_ptr = new MyCall(*acc_main_ptr); // Create raw pointer first
                    CallOpParam prm(true);
                    new_call_ptr->makeCall(target_uri, prm);
                    // Transfer ownership to unique_ptr on success
                    acc_main_ptr->currentCall.reset(new_call_ptr);
                    std::cout << "--- Outgoing call initiated and tracked by unique_ptr." << std::endl;
                }
                catch (Error &err) {
                    std::cerr << "!!! Failed to make call: " << err.info() << std::endl;
                    if (new_call_ptr)
                        delete new_call_ptr;           // Clean up if makeCall failed
                    acc_main_ptr->currentCall.reset(); // Ensure unique_ptr is null
                }
            }
            else if (action == 'h') {
                if (!acc_main_ptr->currentCall) {
                    std::cerr << "!!! No active call to hang up." << std::endl;
                    continue;
                }
                std::cout << ">>> Hanging up call..." << std::endl;
                CallOpParam prm;
                try {
                    // Hangup will trigger onCallState, which should reset the unique_ptr
                    acc_main_ptr->currentCall->hangup(prm);
                    std::cout << "--- Hangup initiated. Call object will be deleted via onCallState/unique_ptr." << std::endl;
                }
                catch (Error &err) {
                    std::cerr << "!!! Failed to hang up call: " << err.info() << std::endl;
                    // If hangup fails, we might need to manually reset the pointer
                    // though it's tricky as the call state might be inconsistent.
                    // Forcing reset might be the safest option to prevent leaks.
                    std::cerr << "--- Force resetting call unique_ptr after hangup error." << std::endl;
                    acc_main_ptr->currentCall.reset();
                }
            }
            else {
                std::cerr << "!!! Unknown command: " << action << std::endl;
            }

            // Let PJSIP event loop run (important for callbacks)
            // pj_thread_sleep(10); // Small sleep to yield CPU
        }

        // 6. Cleanup
        std::cout << "Shutting down..." << std::endl;

        // Resetting unique_ptr will trigger hangup if call active, and destruction
        if (acc_main_ptr->currentCall) {
            std::cout << "--- Initiating hangup via unique_ptr reset before exit..." << std::endl;
            CallOpParam prm;
            try {
                acc_main_ptr->currentCall->hangup(prm);
            }
            catch (...) {
            }
            pj_thread_sleep(500);              // Give hangup time
            acc_main_ptr->currentCall.reset(); // This triggers ~MyCall
        }

        acc_main_ptr.reset(); // Destroy account object (unique_ptr)

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

    // Wait for lingering AI threads? Maybe add a signal/join mechanism if needed.
    std::cout << "Waiting a moment for background threads..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "Exiting application." << std::endl;
    return 0;
}
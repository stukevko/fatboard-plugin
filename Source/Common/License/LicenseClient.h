#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>

// ============================================================================
//  Fatboard – license client   (plugin ↔ account)
//
//  A paid plugin is tied to the buyer's fat-board.com account, not to a serial:
//
//    "Log in" in the plugin
//       → POST /api/device/start   (product, this machine)   ← short code + secret
//       → the browser opens fat-board.com/activate?code=…     (the user confirms, logged in)
//       → POST /api/device/poll    until approved            ← signed license token
//       → token saved in the user's app-data folder
//
//  The token is Ed25519-signed by the server. The plugin checks it OFFLINE with
//  the public key compiled into it: right product, this machine, not expired.
//  Tokens last 30 days and are refreshed silently when the plugin is online
//  (POST /api/license/refresh); a revoked license stops refreshing.
//
//  Build switches (CMake → compile definitions):
//    FATBOARD_LICENSING=0|1        0 = every plugin is open (development builds)
//    FATBOARD_LICENSE_PUBKEY="…"   raw public key, 64 hex chars (`npm run keys` in fatboard-web)
//    FATBOARD_API_URL="https://fat-board.com"
//  Runtime override for testing: environment variable FATBOARD_API.
//
//  Thread-safety: isLicensed() is a lock-free atomic read, safe on the audio thread.
// ============================================================================

#ifndef FATBOARD_LICENSING
 #define FATBOARD_LICENSING 0
#endif
#ifndef FATBOARD_LICENSE_PUBKEY
 #define FATBOARD_LICENSE_PUBKEY ""
#endif
#ifndef FATBOARD_API_URL
 #define FATBOARD_API_URL "https://fat-board.com"
#endif

struct LicenseConfig
{
    bool enabled = FATBOARD_LICENSING != 0;
    juce::String publicKeyHex = FATBOARD_LICENSE_PUBKEY;
    juce::String apiUrl = FATBOARD_API_URL;
    juce::File tokenDir;                 // default: <app data>/Fatboard/licenses
    juce::String machineId;              // default: derived from this computer
    bool openBrowser = true;
};

class LicenseClient : private juce::Thread
{
public:
    enum class State { Open, Demo, Waiting, Licensed };

    struct Status
    {
        State state = State::Demo;
        juce::String userCode;       // while Waiting: the code shown in the browser
        juce::String email;          // while Licensed
        juce::String message;        // last problem, in plain words
    };

    struct Payload { juce::String licenseKey, email, product, machine; juce::int64 iat = 0, exp = 0; };

    using Config = LicenseConfig;

    LicenseClient (juce::String productId, juce::String pluginVersion, Config cfg = Config());
    ~LicenseClient() override;

    /** Audio-thread safe. Always true when licensing is switched off for this build. */
    bool isLicensed() const noexcept { return licensed.load (std::memory_order_relaxed); }
    bool isEnabled() const noexcept  { return config.enabled; }
    Status getStatus() const;

    /** Starts the browser login. Returns at once; watch getStatus(). */
    void login();
    /** Forgets the license on this computer (the activation stays listed in the account until removed there). */
    void logout();

    /** Checks signature, product, machine and expiry. Pure, no I/O. */
    static bool verifyToken (const juce::String& token, const juce::String& product, const juce::String& machine,
                             const juce::String& publicKeyHex, juce::int64 nowSeconds, Payload& out, juce::String* why = nullptr);

    static juce::String thisMachineId();
    const juce::String& getMachineId() const { return config.machineId; }

private:
    void run() override;
    void doLogin();
    void doRefresh();
    bool adopt (const juce::String& token, bool save);
    void setStatus (State s, const juce::String& code, const juce::String& email, const juce::String& message);
    juce::File tokenFile() const;
    juce::var post (const juce::String& path, const juce::var& body, int& statusCode) const;

    const juce::String product, version;
    Config config;
    std::atomic<bool> licensed { false };
    std::atomic<int> job { 0 };          // 0 none, 1 login, 2 refresh
    mutable juce::CriticalSection lock;
    Status status;
    Payload current;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseClient)
};

/** Demo behaviour for an unlicensed paid plugin: it works, but drops to silence for a moment
    every so often. Audio thread. */
class LicenseGate
{
public:
    void prepare (double sampleRate) { sr = sampleRate; pos = 0; gain = 1.0f; }

    template <typename Buffer>
    void process (Buffer& buffer, bool licensed)
    {
        if (licensed && gain >= 1.0f) { pos = 0; return; }
        const int n = buffer.getNumSamples();
        const juce::int64 period = (juce::int64) (periodSeconds * sr), gap = (juce::int64) (gapSeconds * sr);
        const float step = 1.0f / (float) (0.15 * sr);
        for (int i = 0; i < n; ++i)
        {
            const bool mute = ! licensed && (pos % period) >= (period - gap);
            gain = juce::jlimit (0.0f, 1.0f, gain + (mute ? -step : step));
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.getWritePointer (ch)[i] *= gain;
            ++pos;
        }
    }

    // TUNE
    double periodSeconds = 40.0, gapSeconds = 2.5;
private:
    double sr = 48000.0; juce::int64 pos = 0; float gain = 1.0f;
};

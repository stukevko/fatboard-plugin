#include "LicenseClient.h"

extern "C" {
#include "ed25519.h"
}

namespace
{
    juce::MemoryBlock fromBase64Url (juce::String s)
    {
        s = s.replaceCharacter ('-', '+').replaceCharacter ('_', '/');
        while (s.length() % 4 != 0) s << "=";
        juce::MemoryOutputStream out;
        if (! juce::Base64::convertFromBase64 (out, s)) return {};
        return out.getMemoryBlock();
    }

    bool hexToBytes (const juce::String& hex, unsigned char* out, int n)
    {
        if (hex.length() != n * 2) return false;
        for (int i = 0; i < n; ++i)
        {
            const int hi = juce::CharacterFunctions::getHexDigitValue ((juce::juce_wchar) hex[i * 2]);
            const int lo = juce::CharacterFunctions::getHexDigitValue ((juce::juce_wchar) hex[i * 2 + 1]);
            if (hi < 0 || lo < 0) return false;
            out[i] = (unsigned char) (hi * 16 + lo);
        }
        return true;
    }

    juce::int64 nowSeconds() { return juce::Time::currentTimeMillis() / 1000; }
}

juce::String LicenseClient::thisMachineId()
{
    // stable per computer, says nothing about it
    const auto id = juce::SystemStats::getUniqueDeviceID() + "|fatboard";
    return juce::String::toHexString (id.hashCode64()).paddedLeft ('0', 16)
         + juce::String::toHexString ((id + "#2").hashCode64()).paddedLeft ('0', 16);
}

bool LicenseClient::verifyToken (const juce::String& token, const juce::String& productId, const juce::String& machine,
                                 const juce::String& publicKeyHex, juce::int64 now, Payload& out, juce::String* why)
{
    auto fail = [why] (const char* m) { if (why != nullptr) *why = m; return false; };
    const int dot = token.indexOfChar ('.');
    if (dot <= 0) return fail ("not a token");
    const auto data = fromBase64Url (token.substring (0, dot));
    const auto sig  = fromBase64Url (token.substring (dot + 1).trim());
    unsigned char key[32];
    if (data.getSize() == 0 || sig.getSize() != 64) return fail ("damaged token");
    if (! hexToBytes (publicKeyHex.trim(), key, 32)) return fail ("this build has no license key");
    if (ed25519_verify ((const unsigned char*) sig.getData(), (const unsigned char*) data.getData(), data.getSize(), key) != 1)
        return fail ("signature does not match");

    const auto json = juce::JSON::parse (data.toString());
    if (! json.isObject()) return fail ("damaged token");
    out.licenseKey = json["lic"].toString(); out.email = json["email"].toString();
    out.product = json["product"].toString(); out.machine = json["machine"].toString();
    out.iat = (juce::int64) json["iat"]; out.exp = (juce::int64) json["exp"];
    if ((int) json["v"] != 1)        return fail ("unknown token version");
    if (out.product != productId)    return fail ("license is for another plugin");
    if (out.machine != machine)      return fail ("license is for another computer");
    if (out.exp < now)               return fail ("license needs to go online once to renew");
    return true;
}

LicenseClient::LicenseClient (juce::String productId, juce::String pluginVersion, Config cfg)
    : juce::Thread ("Fatboard license"), product (std::move (productId)), version (std::move (pluginVersion)), config (std::move (cfg))
{
    if (! config.enabled)
    {
        licensed.store (true);
        status.state = State::Open;
        return;
    }
    if (config.tokenDir == juce::File())
        config.tokenDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Fatboard").getChildFile ("licenses");
    if (config.machineId.isEmpty()) config.machineId = thisMachineId();
    const auto env = juce::SystemStats::getEnvironmentVariable ("FATBOARD_API", {});
    if (env.isNotEmpty()) config.apiUrl = env;
    config.apiUrl = config.apiUrl.trimCharactersAtEnd ("/");

    const auto file = tokenFile();
    if (file.existsAsFile())
    {
        const auto token = file.loadFileAsString().trim();
        juce::String why;
        if (adopt (token, false))
        {
            // renew in the background once a day
            if (nowSeconds() - current.iat > 86400) { job.store (2); startThread(); }
        }
        else
        {
            // expired while offline? one try to renew with the key from the old token
            Payload p; juce::String w;
            const int dot = token.indexOfChar ('.');
            const auto json = dot > 0 ? juce::JSON::parse (fromBase64Url (token.substring (0, dot)).toString()) : juce::var();
            if (json.isObject() && json["product"].toString() == product && json["machine"].toString() == config.machineId)
            {
                const juce::ScopedLock sl (lock);
                current.licenseKey = json["lic"].toString();
                status.message = "License needs to renew. Go online once.";
                job.store (2); startThread();
            }
        }
    }
}

LicenseClient::~LicenseClient()
{
    signalThreadShouldExit();
    stopThread (6000);
}

juce::File LicenseClient::tokenFile() const { return config.tokenDir.getChildFile (product + ".token"); }

LicenseClient::Status LicenseClient::getStatus() const { const juce::ScopedLock sl (lock); return status; }

void LicenseClient::setStatus (State s, const juce::String& code, const juce::String& email, const juce::String& message)
{
    const juce::ScopedLock sl (lock);
    status.state = s; status.userCode = code; status.email = email; status.message = message;
}

bool LicenseClient::adopt (const juce::String& token, bool save)
{
    Payload p; juce::String why;
    if (! verifyToken (token, product, config.machineId, config.publicKeyHex, nowSeconds(), p, &why))
    {
        licensed.store (false);
        setStatus (State::Demo, {}, {}, why);
        return false;
    }
    { const juce::ScopedLock sl (lock); current = p; }
    licensed.store (true);
    setStatus (State::Licensed, {}, p.email, {});
    if (save) { config.tokenDir.createDirectory(); tokenFile().replaceWithText (token); }
    return true;
}

void LicenseClient::login()
{
    if (! config.enabled || isThreadRunning()) return;
    setStatus (State::Waiting, {}, {}, "Connecting...");
    job.store (1);
    startThread();
}

void LicenseClient::logout()
{
    if (! config.enabled) return;
    signalThreadShouldExit(); stopThread (6000);
    tokenFile().deleteFile();
    licensed.store (false);
    { const juce::ScopedLock sl (lock); current = Payload(); }
    setStatus (State::Demo, {}, {}, {});
}

juce::var LicenseClient::post (const juce::String& path, const juce::var& body, int& statusCode) const
{
    statusCode = 0;
    const auto url = juce::URL (config.apiUrl + path).withPOSTData (juce::JSON::toString (body, true));
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                             .withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json")
                             .withConnectionTimeoutMs (8000)
                             .withStatusCode (&statusCode)
                             .withHttpRequestCmd ("POST");
    if (auto stream = url.createInputStream (options))
        return juce::JSON::parse (stream->readEntireStreamAsString());
    return {};
}

void LicenseClient::run()
{
    const int j = job.exchange (0);
    if (j == 1) doLogin();
    else if (j == 2) doRefresh();
}

void LicenseClient::doLogin()
{
    auto* body = new juce::DynamicObject();
    body->setProperty ("product", product);
    body->setProperty ("machineId", config.machineId);
    body->setProperty ("machineName", juce::SystemStats::getComputerName().substring (0, 80));
    body->setProperty ("os", juce::SystemStats::getOperatingSystemName().substring (0, 40));
    body->setProperty ("pluginVersion", version.substring (0, 20));
    int code = 0;
    const auto start = post ("/api/device/start", juce::var (body), code);
    if (code != 200 || ! start.isObject())
    {
        setStatus (State::Demo, {}, {}, code == 0 ? "Can't reach fatboard.com. Check your internet." : "Login is not available right now (" + juce::String (code) + ").");
        return;
    }
    const auto deviceCode = start["deviceCode"].toString(), userCode = start["userCode"].toString(), verifyUrl = start["verifyUrl"].toString();
    const int interval = juce::jlimit (1, 15, (int) start["interval"]);
    setStatus (State::Waiting, userCode, {}, "Confirm in your browser.");
    if (config.openBrowser) juce::URL (verifyUrl).launchInDefaultBrowser();

    auto* pollBody = new juce::DynamicObject(); pollBody->setProperty ("deviceCode", deviceCode);
    const juce::var poll (pollBody);
    const auto deadline = juce::Time::currentTimeMillis() + 15 * 60 * 1000;
    while (! threadShouldExit() && juce::Time::currentTimeMillis() < deadline)
    {
        if (wait (interval * 1000)) { if (threadShouldExit()) break; }
        const auto r = post ("/api/device/poll", poll, code);
        const auto st = r["status"].toString();
        if (st == "approved")
        {
            if (! adopt (r["token"].toString(), true)) return;       // adopt() set the reason
            return;
        }
        if (st == "denied")  { setStatus (State::Demo, {}, {}, "Login was declined in the browser."); return; }
        if (st == "expired" || st == "invalid") { setStatus (State::Demo, {}, {}, "That took too long. Click Log in again."); return; }
        if (st == "error")   { setStatus (State::Demo, {}, {}, r["error"].toString()); return; }
        // pending or a network hiccup: keep waiting
    }
    if (! threadShouldExit()) setStatus (State::Demo, {}, {}, "That took too long. Click Log in again.");
}

void LicenseClient::doRefresh()
{
    juce::String key;
    { const juce::ScopedLock sl (lock); key = current.licenseKey; }
    if (key.isEmpty()) return;
    auto* body = new juce::DynamicObject();
    body->setProperty ("licenseKey", key);
    body->setProperty ("machineId", config.machineId);
    body->setProperty ("pluginVersion", version.substring (0, 20));
    int code = 0;
    const auto r = post ("/api/license/refresh", juce::var (body), code);
    if (code == 200 && r["token"].toString().isNotEmpty()) { adopt (r["token"].toString(), true); return; }
    if (code == 403)
    {
        // the server says no: revoked, or this computer was removed in the account
        tokenFile().deleteFile();
        licensed.store (false);
        setStatus (State::Demo, {}, {}, r["error"].toString().isNotEmpty() ? r["error"].toString() : juce::String ("This license is no longer active on this computer."));
    }
    // offline or server trouble: keep what we have until it expires
}

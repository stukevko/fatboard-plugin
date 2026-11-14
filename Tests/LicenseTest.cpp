// License client test.
//   LicenseTest verify <pubkeyHex> <token> <product> <machine>     offline checks on a real server token
//   LicenseTest login  <pubkeyHex> <apiUrl> <product> <tokenDir> <codeFile>
//        runs the real device flow against a server; writes the user code to <codeFile>
//        (something else approves it), waits for the license, then proves a second
//        start works OFFLINE from the saved token.
#include "LicenseClient.h"
#include <cstdio>

static int verify (const juce::StringArray& a)
{
    int fails = 0;
    const auto key = a[1], token = a[2], product = a[3], machine = a[4];
    const auto now = juce::Time::currentTimeMillis() / 1000;
    LicenseClient::Payload p; juce::String why;
    auto check = [&] (const char* name, bool got, bool want) { std::printf ("%-46s %s%s\n", name, got == want ? "ok" : "FAIL", got ? "" : (" (" + why + ")").toRawUTF8()); if (got != want) ++fails; why.clear(); };
    check ("real token, right product and machine", LicenseClient::verifyToken (token, product, machine, key, now, p, &why), true);
    std::printf ("   -> %s, %s, valid for %d more days\n", p.email.toRawUTF8(), p.licenseKey.toRawUTF8(), (int) ((p.exp - now) / 86400));
    check ("other plugin", LicenseClient::verifyToken (token, "harun-x", machine, key, now, p, &why), false);
    check ("other computer", LicenseClient::verifyToken (token, product, machine + "0", key, now, p, &why), false);
    check ("31 days later (expired)", LicenseClient::verifyToken (token, product, machine, key, now + 31 * 86400, p, &why), false);
    // tamper: flip one character of the payload → signature must fail
    auto bad = token; const int i = 20; bad = bad.replaceSection (i, 1, juce::String::charToString (bad[i] == 'A' ? 'B' : 'A'));
    check ("payload changed by one character", LicenseClient::verifyToken (bad, product, machine, key, now, p, &why), false);
    // forged: a payload that says "valid forever" with the old signature
    const int dot = token.indexOfChar ('.');
    const auto forgedPayload = juce::Base64::toBase64 (juce::String ("{\"v\":1,\"lic\":\"FB-AAAA-AAAA-AAAA\",\"uid\":\"x\",\"email\":\"x@x\",\"product\":\"" + product + "\",\"machine\":\"" + machine + "\",\"iat\":1,\"exp\":9999999999}"))
                                   .replaceCharacter ('+', '-').replaceCharacter ('/', '_').trimCharactersAtEnd ("=");
    check ("forged payload with a copied signature", LicenseClient::verifyToken (forgedPayload + token.substring (dot), product, machine, key, now, p, &why), false);
    check ("wrong public key", LicenseClient::verifyToken (token, product, machine, juce::String::repeatedString ("ab", 32), now, p, &why), false);
    check ("garbage", LicenseClient::verifyToken ("hello", product, machine, key, now, p, &why), false);
    std::printf (fails == 0 ? "\nALL OK\n" : "\n%d FAILED\n", fails);
    return fails;
}

static int login (const juce::StringArray& a)
{
    juce::ScopedJuceInitialiser_GUI init;
    LicenseClient::Config cfg; cfg.enabled = true; cfg.publicKeyHex = a[1]; cfg.apiUrl = a[2]; cfg.tokenDir = juce::File (a[4]); cfg.openBrowser = false;
    const auto product = a[3]; const juce::File codeFile (a[5]);
    {
        LicenseClient c (product, "0.12.0", cfg);
        std::printf ("fresh start: licensed=%d (want 0)\n", (int) c.isLicensed());
        if (c.isLicensed()) return 1;
        c.login();
        juce::String code;
        for (int i = 0; i < 200 && code.isEmpty(); ++i) { juce::Thread::sleep (100); const auto s = c.getStatus(); code = s.userCode; if (s.state == LicenseClient::State::Demo) { std::printf ("login failed: %s\n", s.message.toRawUTF8()); return 1; } }
        std::printf ("plugin shows code %s, machine %s\n", code.toRawUTF8(), c.getMachineId().toRawUTF8());
        codeFile.replaceWithText (code);
        for (int i = 0; i < 400 && ! c.isLicensed(); ++i) { juce::Thread::sleep (100); if (c.getStatus().state == LicenseClient::State::Demo) { std::printf ("declined: %s\n", c.getStatus().message.toRawUTF8()); return 1; } }
        std::printf ("after approval: licensed=%d, to %s\n", (int) c.isLicensed(), c.getStatus().email.toRawUTF8());
        if (! c.isLicensed()) return 1;
    }
    {
        cfg.apiUrl = "http://127.0.0.1:9";           // nothing listens here = offline
        LicenseClient c (product, "0.12.0", cfg);
        std::printf ("second start, server unreachable: licensed=%d (want 1), to %s\n", (int) c.isLicensed(), c.getStatus().email.toRawUTF8());
        if (! c.isLicensed()) return 1;
        LicenseClient other (product == "harun" ? "smash" : "harun", "0.12.0", cfg);
        std::printf ("another plugin with the same folder: licensed=%d (want 0)\n", (int) other.isLicensed());
        if (other.isLicensed()) return 1;
        c.logout();
        std::printf ("after log out: licensed=%d (want 0)\n", (int) c.isLicensed());
        if (c.isLicensed()) return 1;
    }
    std::printf ("\nALL OK\n");
    return 0;
}

int main (int argc, char** argv)
{
    juce::StringArray a; for (int i = 1; i < argc; ++i) a.add (argv[i]);
    if (a.size() == 5 && a[0] == "verify") return verify (a);
    if (a.size() == 6 && a[0] == "login")  return login (a);
    std::printf ("usage: see the top of Tests/LicenseTest.cpp\n");
    return 2;
}

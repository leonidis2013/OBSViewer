#pragma once

#include <string>

namespace obsviewer::keychain {

// Stores the GitHub access token used by the updater in the macOS Keychain
// (generic password, service "com.leonidis2013.obsviewer", account "github-token").
bool saveToken(const std::string &token);
std::string loadToken();
bool removeToken();

} // namespace obsviewer::keychain

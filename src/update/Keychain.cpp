#include "update/Keychain.hpp"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace obsviewer::keychain {

#ifdef __APPLE__

namespace {

constexpr const char *kService = "com.leonidis2013.obsviewer";
constexpr const char *kAccount = "github-token";

CFStringRef cfString(const char *s)
{
	return CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
}

// Base query identifying our item. Caller releases.
CFMutableDictionaryRef baseQuery()
{
	CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
								 &kCFTypeDictionaryValueCallBacks);
	CFStringRef service = cfString(kService);
	CFStringRef account = cfString(kAccount);
	CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(query, kSecAttrService, service);
	CFDictionarySetValue(query, kSecAttrAccount, account);
	CFRelease(service);
	CFRelease(account);
	return query;
}

} // namespace

bool removeToken()
{
	CFMutableDictionaryRef query = baseQuery();
	const OSStatus status = SecItemDelete(query);
	CFRelease(query);
	return status == errSecSuccess || status == errSecItemNotFound;
}

bool saveToken(const std::string &token)
{
	removeToken();
	if (token.empty())
		return true;

	CFMutableDictionaryRef item = baseQuery();
	CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(token.data()),
				      static_cast<CFIndex>(token.size()));
	CFStringRef label = cfString("OBSViewer GitHub token");
	CFDictionarySetValue(item, kSecValueData, data);
	CFDictionarySetValue(item, kSecAttrLabel, label);
	CFDictionarySetValue(item, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);

	const OSStatus status = SecItemAdd(item, nullptr);

	CFRelease(label);
	CFRelease(data);
	CFRelease(item);
	return status == errSecSuccess;
}

std::string loadToken()
{
	CFMutableDictionaryRef query = baseQuery();
	CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

	CFTypeRef result = nullptr;
	const OSStatus status = SecItemCopyMatching(query, &result);
	CFRelease(query);

	std::string token;
	if (status == errSecSuccess && result && CFGetTypeID(result) == CFDataGetTypeID()) {
		auto data = static_cast<CFDataRef>(result);
		token.assign(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
			     static_cast<size_t>(CFDataGetLength(data)));
	}
	if (result)
		CFRelease(result);
	return token;
}

#else

bool saveToken(const std::string &)
{
	return false;
}
std::string loadToken()
{
	return {};
}
bool removeToken()
{
	return true;
}

#endif

} // namespace obsviewer::keychain

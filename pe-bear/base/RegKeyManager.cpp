#ifdef _WINDOWS
#include <QString>
#include <windows.h>
#include <stdio.h>
#include <strsafe.h>
#endif //_WINDOWS

#include "RegKeyManager.h"

std::string PART = "file\\shell\\Open with ";

#ifdef _WINDOWS
// The registry is written through the W API: the strings come in as UTF-8 (the
// application path from QString::toStdString()), which the A API would read in
// the system ANSI code page and mangle for any non-ASCII install path.
static std::wstring toWide(const std::string &utf8)
{
	return QString::fromUtf8(utf8.c_str(), int(utf8.size())).toStdWString();
}
#endif //_WINDOWS

bool RegKeyManager::removeRegPath(const std::string &extension, const std::string &appName)
{
#ifdef _WINDOWS
	int res1 = 0, res2 = 0;
	HKEY hKey = 0;

	std::string fullName = extension;
	fullName.append(PART).append(appName);
	const std::wstring subKey = toWide(fullName);

	LONG res = RegCreateKeyW(HKEY_CLASSES_ROOT, subKey.c_str(), &hKey);
	if (res != ERROR_SUCCESS) {
		return false;
	}

	res1 = RegDeleteKeyW(hKey, L"command");
	res2 = RegDeleteKeyW(hKey, L"");

	res = RegCloseKey(hKey);
	if (res != ERROR_SUCCESS) {
		return false;
	}
	if (res1 == ERROR_SUCCESS || res2 == ERROR_SUCCESS) {
		return true;
	}
#endif //_WINDOWS
	return false;
}

bool RegKeyManager::addRegPath(const std::string &extension, const std::string &appName, const std::string &path)
{
#ifdef _WINDOWS

	int res1 = 0, res2 = 0;
	HKEY hKey = 0;

	std::string fullName = extension;
	fullName.append(PART).append(appName); // example: "dllfile\\shell\\Open with PE-bear";
	const std::string cmdName = fullName + "\\command";

	// set command:
	const std::wstring cmdPath = toWide(path + " \"%1\"");
	LONG res = RegCreateKeyW(HKEY_CLASSES_ROOT, toWide(cmdName).c_str(), &hKey);
	if (res != ERROR_SUCCESS) {
		return false;
	}
	// REG_SZ: the size is in bytes and includes the terminating null
	res1 = RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)cmdPath.c_str(), DWORD((cmdPath.length() + 1) * sizeof(wchar_t)));
	res = RegCloseKey(hKey);
	if (res1 != ERROR_SUCCESS || res != ERROR_SUCCESS) {
		return false;
	}
	
	//set icon:
	hKey = 0;
	res = RegCreateKeyW(HKEY_CLASSES_ROOT, toWide(fullName).c_str(), &hKey);
	if (res != ERROR_SUCCESS) {
		return false;
	}

	const std::wstring iconPath = toWide(path + ",0");
	res1 = RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (const BYTE*)iconPath.c_str(), DWORD((iconPath.length() + 1) * sizeof(wchar_t)));

	res = RegCloseKey(hKey);
	if (res1 != ERROR_SUCCESS || res != ERROR_SUCCESS) {
		return false;
	}
	if (res1 == ERROR_SUCCESS) {
		return true;
	}
#endif //_WINDOWS
	return false;
}


bool RegKeyManager::isKeySet(const std::string &extension, const std::string &appName)
{
#ifdef _WINDOWS

	HKEY hKey = 0;
	std::string fullName = extension;
	fullName.append(PART).append(appName);
	const std::wstring subKey = toWide(fullName);

	LONG res = RegOpenKeyExW(HKEY_CLASSES_ROOT, subKey.c_str(), 0, KEY_READ, &hKey);
	if (res != ERROR_SUCCESS) {
		return false;
	}
	res = RegCloseKey(hKey);
	if (res == ERROR_SUCCESS) {
		return true;
	}
#endif //_WINDOWS
	return false;
}


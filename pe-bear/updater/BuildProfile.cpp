#include "BuildProfile.h"

using namespace pe_bear::updater;

QString pe_bear::updater::platformToString(Platform p)
{
	switch (p) {
		case PlatformWindows: return QLatin1String("windows");
		case PlatformLinux: return QLatin1String("linux");
		case PlatformMacOS: return QLatin1String("macos");
		default: return QLatin1String("unknown");
	}
}

QString pe_bear::updater::architectureToString(Architecture a)
{
	switch (a) {
		case ArchX86: return QLatin1String("x86");
		case ArchX64: return QLatin1String("x64");
		case ArchArm64: return QLatin1String("arm64");
		default: return QLatin1String("unknown");
	}
}

QString pe_bear::updater::packageTypeToString(PackageType t)
{
	switch (t) {
		case PackageWindowsZip: return QLatin1String("windows-zip");
		case PackageLinuxTarXz: return QLatin1String("linux-tar-xz");
		case PackageLinuxAppImage: return QLatin1String("linux-appimage");
		case PackageMacAppZip: return QLatin1String("macos-app-zip");
		default: return QLatin1String("unknown");
	}
}

bool pe_bear::updater::isPackageTypeOnPlatform(PackageType t, Platform p)
{
	switch (t) {
		case PackageWindowsZip: return (p == PlatformWindows);
		case PackageLinuxTarXz:
		case PackageLinuxAppImage: return (p == PlatformLinux);
		case PackageMacAppZip: return (p == PlatformMacOS);
		default: return false;
	}
}

namespace {

	Platform detectPlatform()
	{
#if defined(Q_OS_WIN)
		return PlatformWindows;
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC) || defined(Q_OS_DARWIN)
		return PlatformMacOS;
#elif defined(Q_OS_LINUX)
		return PlatformLinux;
#else
		return PlatformUnknown;
#endif
	}

	Architecture detectArchitecture()
	{
#if defined(Q_PROCESSOR_ARM_64) || defined(__aarch64__) || defined(_M_ARM64)
		return ArchArm64;
#elif defined(Q_PROCESSOR_X86_64) || defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
		return ArchX64;
#elif defined(Q_PROCESSOR_X86_32) || defined(__i386__) || defined(_M_IX86)
		return ArchX86;
#else
		/* last resort: word size tells x86 from x64 on the platforms we ship */
		return (sizeof(void*) == 8) ? ArchX64 : ArchX86;
#endif
	}

	QString detectRuntime()
	{
#if defined(PEBEAR_BUILD_RUNTIME)
		return QLatin1String(PEBEAR_BUILD_RUNTIME);
#elif defined(_MSC_VER)
		/* the "vsNN" tags used in the published asset names */
		#if _MSC_VER >= 1930
			return QLatin1String("vs22");
		#elif _MSC_VER >= 1920
			return QLatin1String("vs17");
		#elif _MSC_VER >= 1910
			return QLatin1String("vs15");
		#elif _MSC_VER >= 1600
			return QLatin1String("vs10");
		#else
			return QString();
		#endif
#else
		return QString();
#endif
	}

	PackageType detectPackageType()
	{
#if defined(PEBEAR_PACKAGE_TYPE)
		const QString declared = QLatin1String(PEBEAR_PACKAGE_TYPE);
		if (declared == QLatin1String("windows-zip")) return PackageWindowsZip;
		if (declared == QLatin1String("linux-tar-xz")) return PackageLinuxTarXz;
		if (declared == QLatin1String("linux-appimage")) return PackageLinuxAppImage;
		if (declared == QLatin1String("macos-app-zip")) return PackageMacAppZip;
#endif
		/* An AppImage always knows what it is, whatever the build declared. */
		if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
			return PackageLinuxAppImage;
		}
		return PackageUnknown;
	}

	QString detectBuildId()
	{
#if defined(PEBEAR_BUILD_ID)
		return QLatin1String(PEBEAR_BUILD_ID).trimmed();
#else
		return QString();
#endif
	}

	Version detectMinOsVersion()
	{
#if defined(PEBEAR_MIN_OS_VERSION)
		return Version::fromString(QLatin1String(PEBEAR_MIN_OS_VERSION));
#else
		return Version();
#endif
	}

}; // namespace

BuildProfile::BuildProfile()
	: m_platform(PlatformUnknown), m_arch(ArchUnknown),
	m_packageType(PackageUnknown), m_qtMajor(0)
{
}

BuildProfile BuildProfile::current()
{
	BuildProfile p;
	p.setPlatform(detectPlatform());
	p.setArchitecture(detectArchitecture());
	p.setPackageType(detectPackageType());
	p.setQtMajor(QT_VERSION_MAJOR);
	p.setRuntime(detectRuntime());
	p.setBuildId(detectBuildId());
	p.setMinOsVersion(detectMinOsVersion());
	return p;
}

bool BuildProfile::isComplete() const
{
	return (m_platform != PlatformUnknown)
		&& (m_arch != ArchUnknown)
		&& (m_qtMajor > 0);
}

QString BuildProfile::toString() const
{
	QString out = platformToString(m_platform)
		+ QLatin1Char('/') + architectureToString(m_arch)
		+ QLatin1String("/qt") + QString::number(m_qtMajor)
		+ QLatin1Char('/') + packageTypeToString(m_packageType);
	if (!m_runtime.isEmpty()) {
		out += QLatin1Char('/') + m_runtime;
	}
	if (m_minOsVersion.isValid()) {
		out += QLatin1String("/minos:") + m_minOsVersion.toString();
	}
	if (!m_buildId.isEmpty()) {
		out += QLatin1String("/build:") + m_buildId;
	}
	return out;
}

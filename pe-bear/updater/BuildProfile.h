#pragma once

#include <QtCore>
#include "Version.h"

namespace pe_bear {
namespace updater {

enum Platform {
	PlatformUnknown = 0,
	PlatformWindows,
	PlatformLinux,
	PlatformMacOS
};

enum Architecture {
	ArchUnknown = 0,
	ArchX86,
	ArchX64,
	ArchArm64
};

enum PackageType {
	PackageUnknown = 0,
	PackageWindowsZip,     /* portable ZIP, Windows */
	PackageLinuxTarXz,     /* portable TAR.XZ, Linux */
	PackageLinuxAppImage,  /* single-file AppImage */
	PackageMacAppZip       /* zipped .app bundle */
};

QString platformToString(Platform p);
QString architectureToString(Architecture a);
QString packageTypeToString(PackageType t);

/** True when the package type can plausibly exist on that platform. */
bool isPackageTypeOnPlatform(PackageType t, Platform p);

/**
 * Describes the running build precisely enough to pick an asset that is a
 * drop-in replacement for it. Never used to migrate between variants: every
 * field that is known must match, and an unknown field can only ever widen
 * the match, never narrow it.
 *
 * Values come from compile-time macros injected by CMake, falling back to what
 * can be derived from Qt's own platform macros. The version number is not part
 * of the profile; it lives only in rebear_ver_short.h (see Version::current()).
 */
class BuildProfile
{
public:
	/** The profile of the running build. */
	static BuildProfile current();

	BuildProfile();

	Platform platform() const { return m_platform; }
	Architecture architecture() const { return m_arch; }
	PackageType packageType() const { return m_packageType; }
	int qtMajor() const { return m_qtMajor; }

	/** Toolchain/runtime tag, e.g. "vs17" on MSVC builds; empty when N/A. */
	QString runtime() const { return m_runtime; }

	/** Opaque build identifier (commit hash or CI build id); may be empty. */
	QString buildId() const { return m_buildId; }

	/** Lowest OS version this build runs on; invalid when not declared. */
	Version minOsVersion() const { return m_minOsVersion; }

	void setPlatform(Platform p) { m_platform = p; }
	void setArchitecture(Architecture a) { m_arch = a; }
	void setPackageType(PackageType t) { m_packageType = t; }
	void setQtMajor(int major) { m_qtMajor = major; }
	void setRuntime(const QString &r) { m_runtime = r; }
	void setBuildId(const QString &id) { m_buildId = id; }
	void setMinOsVersion(const Version &v) { m_minOsVersion = v; }

	/** True when platform, architecture and Qt major are all known. */
	bool isComplete() const;

	QString toString() const;

private:
	Platform m_platform;
	Architecture m_arch;
	PackageType m_packageType;
	int m_qtMajor;
	QString m_runtime;
	QString m_buildId;
	Version m_minOsVersion;
};

}; // namespace updater
}; // namespace pe_bear

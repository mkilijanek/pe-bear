#include "PlatformInstallerFactory.h"
#include "BuildProfile.h"
#include "DirectoryInstaller.h"

// Platform-specific installers (only include if available)
#if defined(Q_OS_LINUX)
#include "LinuxInstaller.h"
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC) || defined(Q_OS_DARWIN)
#include "MacOSInstaller.h"
#endif

namespace pe_bear {
namespace updater {

Platform PlatformInstallerFactory::currentPlatform()
{
	return BuildProfile::current().platform();
}

PlatformInstaller* PlatformInstallerFactory::create(IFileSystem *fs, IArchiveReader *reader,
		const ExtractionPolicy &policy)
{
	return createForPlatform(currentPlatform(), fs, reader, policy);
}

PlatformInstaller* PlatformInstallerFactory::createForPlatform(Platform platform,
		IFileSystem *fs, IArchiveReader *reader, const ExtractionPolicy &policy)
{
	switch (platform) {
		case PlatformWindows:
			// Windows uses the generic DirectoryInstaller
			// Note: A future WindowsInstaller could handle Windows-specific
			// behaviors like handle sharing violations, UAC, etc.
			return new DirectoryInstaller(fs, reader, policy);

		case PlatformLinux:
			// Linux uses the specialized LinuxInstaller
#if defined(Q_OS_LINUX)
			return new LinuxInstaller(fs, reader, policy);
#else
			// Fall back to DirectoryInstaller if LinuxInstaller is not available
			return new DirectoryInstaller(fs, reader, policy);
#endif

		case PlatformMacOS:
			// macOS uses the specialized MacOSInstaller
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC) || defined(Q_OS_DARWIN)
			return new MacOSInstaller(fs, reader, policy);
#else
			// Fall back to DirectoryInstaller if MacOSInstaller is not available
			return new DirectoryInstaller(fs, reader, policy);
#endif

		default:
			// Unknown platform, use the generic DirectoryInstaller
			return new DirectoryInstaller(fs, reader, policy);
	}
}

}; // namespace updater
}; // namespace pe_bear

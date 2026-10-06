#pragma once

#include <QtCore>
#include "PlatformInstaller.h"

namespace pe_bear {
namespace updater {

/**
 * Factory for creating platform-specific installers.
 *
 * Creates the appropriate PlatformInstaller implementation for the current
 * platform (Windows, Linux, macOS). This abstraction allows the helper
 * process to use platform-optimized installers without having to
 * know which platform it's running on.
 */
class PlatformInstallerFactory
{
public:
	/**
	 * Creates the appropriate platform installer.
	 *
	 * @param fs the filesystem interface to use
	 * @param reader the archive reader to use, may be nullptr
	 * @param policy the extraction policy to use
	 * @return a PlatformInstaller* that the caller owns
	 */
	static PlatformInstaller* create(IFileSystem *fs, IArchiveReader *reader,
		const ExtractionPolicy &policy = ExtractionPolicy());

	/**
	 * Creates the appropriate platform installer for a specific platform.
	 *
	 * @param platform the target platform
	 * @param fs the filesystem interface to use
	 * @param reader the archive reader to use, may be nullptr
	 * @param policy the extraction policy to use
	 * @return a PlatformInstaller* that the caller owns, or nullptr if unsupported
	 */
	static PlatformInstaller* createForPlatform(Platform platform, IFileSystem *fs,
		IArchiveReader *reader, const ExtractionPolicy &policy = ExtractionPolicy());

	/**
	 * Detects the current platform.
	 */
	static Platform currentPlatform();

private:
	PlatformInstallerFactory() = delete;
	~PlatformInstallerFactory() = delete;
};

}; // namespace updater
}; // namespace pe_bear

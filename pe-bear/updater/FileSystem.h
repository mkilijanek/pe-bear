#pragma once

#include <QtCore>

namespace pe_bear {
namespace updater {

/**
 * Every filesystem operation the installer performs, behind one interface.
 *
 * The point is testability. The transaction state machine, the ordering of a
 * rollback and the extraction checks are all platform-independent logic, but
 * they only touch the world through these calls. With a fake implementation
 * they can be driven through every failure at every step on any host; with the
 * real one they do the actual work. Only the platform activation step -- which
 * has to contend with Windows refusing to replace a running image -- needs a
 * real Windows machine.
 *
 * Deliberately narrow: an operation that is not here cannot be performed, so
 * the surface a test has to simulate stays small and complete.
 */
/**
 * The directory containing @p path, as a pure string operation.
 *
 * Deliberately not QFileInfo::absolutePath(), which resolves a path against
 * the process's current directory *and current drive*. On Windows that turns
 * "/opt/pe-bear" into "C:\\opt" -- so the answer depends on ambient state
 * that has nothing to do with the question. Every path this code derives a
 * parent from is already absolute and canonical, and the parent of such a path
 * needs no filesystem and no process state to compute.
 *
 * Requires '/' separators, which is what every Qt path API -- and so
 * IFileSystem::canonicalPath -- returns on every platform, Windows included.
 * Native backslash paths are *not* converted, deliberately: the conversion is
 * a no-op on POSIX, so accepting them would make the same input give different
 * answers on different hosts, which is the exact fault this function exists to
 * remove. A path with no '/' in it therefore has no parent, everywhere, and
 * the result is empty.
 *
 * Returns an empty string whenever there is no parent to name.
 *
 * On Windows a UNC path reduced as far as "//server/share" yields "//server",
 * which is not a directory. Stated rather than handled: the caller asks
 * whether it is writable, it is not, and the step is refused -- the safe
 * direction. (On POSIX "//server/share" is an ordinary path and normalises to
 * "/server/share", which is also correct there.)
 */
QString parentDirectoryOf(const QString &path);

class IFileSystem
{
public:
	virtual ~IFileSystem() {}

	/* --- queries --- */
	virtual bool exists(const QString &path) const = 0;
	virtual bool isDir(const QString &path) const = 0;
	virtual qint64 fileSize(const QString &path) const = 0;
	virtual QStringList listDir(const QString &path) const = 0;
	/** Resolves symlinks and "..", returning empty when the path cannot be resolved. */
	virtual QString canonicalPath(const QString &path) const = 0;
	/** Probed, not inferred from permissions bits, which lie on network shares. */
	virtual bool isWritableDir(const QString &path) const = 0;

	/* --- mutations --- */
	virtual bool makeDir(const QString &path) = 0;
	virtual bool removeFile(const QString &path) = 0;
	virtual bool removeDirRecursively(const QString &path) = 0;
	/**
	 * Moves a file or directory. Expected to be atomic within one volume; the
	 * installer relies on that, which is why staging prefers the target volume.
	 */
	virtual bool movePath(const QString &from, const QString &to) = 0;
	virtual bool copyFile(const QString &from, const QString &to) = 0;

	/* --- small whole-file IO, for the journal --- */
	virtual bool writeFile(const QString &path, const QByteArray &data) = 0;
	virtual QByteArray readFile(const QString &path) const = 0;

	/** Restricts a path to the owner. Best-effort; false where unsupported. */
	/**
	 * Gives @p path the permissions an installed file should have: readable by
	 * everyone, writable by its owner, and executable by everyone when
	 * @p executable.
	 *
	 * Needed because writeFile deliberately restricts what it writes to the
	 * owner -- right for a transaction record or a handshake nonce, wrong for
	 * the contents of a package that is about to become a shared
	 * installation. Without this step an update silently takes away the access
	 * other users had to the installation it replaced.
	 *
	 * Never sets setuid, setgid or the sticky bit; QFile::Permissions cannot
	 * express them, which is exactly why this goes through Qt rather than
	 * chmod.
	 */
	virtual bool setStandardPermissions(const QString &path, bool executable) = 0;
	virtual bool restrictToOwner(const QString &path) = 0;

	/** Last error text from the most recent failed call, for diagnostics. */
	virtual QString lastError() const = 0;
};

//----------------------------------------------------------------------

/** The real filesystem, on top of Qt. */
class RealFileSystem : public IFileSystem
{
public:
	RealFileSystem() {}

	virtual bool exists(const QString &path) const;
	virtual bool isDir(const QString &path) const;
	virtual qint64 fileSize(const QString &path) const;
	virtual QStringList listDir(const QString &path) const;
	virtual QString canonicalPath(const QString &path) const;
	virtual bool isWritableDir(const QString &path) const;

	virtual bool makeDir(const QString &path);
	virtual bool removeFile(const QString &path);
	virtual bool removeDirRecursively(const QString &path);
	virtual bool movePath(const QString &from, const QString &to);
	virtual bool copyFile(const QString &from, const QString &to);

	virtual bool writeFile(const QString &path, const QByteArray &data);
	virtual QByteArray readFile(const QString &path) const;

	virtual bool setStandardPermissions(const QString &path, bool executable);
	virtual bool restrictToOwner(const QString &path);

	virtual QString lastError() const { return m_lastError; }

private:
	bool fail(const QString &what) const;

	mutable QString m_lastError;
};

}; // namespace updater
}; // namespace pe_bear

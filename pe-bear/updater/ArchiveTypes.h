#pragma once

#include <QtCore>

namespace pe_bear {
namespace updater {

/**
 * One entry as an archive describes itself.
 *
 * Deliberately a plain description, not a handle: the security checks run over
 * these before anything is read or written, and they must be expressible
 * without a decoder present. That separation is what lets every rejection rule
 * be tested exhaustively and reviewed on its own.
 */
struct ArchiveEntry
{
	enum Kind {
		KindFile = 0,
		KindDir,
		KindSymlink,
		KindHardlink,
		/** Anything else an archive can claim: device, fifo, socket, ... */
		KindOther,
		ENTRY_KINDS_COUNT
	};

	ArchiveEntry()
		: kind(KindFile), uncompressedSize(0), compressedSize(0), isExecutable(false) {}

	ArchiveEntry(const QString &p, Kind k, qint64 uncompressed = 0, qint64 compressed = 0)
		: path(p), kind(k), uncompressedSize(uncompressed), compressedSize(compressed),
		isExecutable(false) {}

	/** The path exactly as stored in the archive, unmodified. */
	QString path;
	Kind kind;
	qint64 uncompressedSize;
	qint64 compressedSize;
	/** For a link, the target exactly as stored. */
	QString linkTarget;

	/**
	 * Whether the archive marks this entry as executable.
	 *
	 * A single flag rather than the stored mode, deliberately. An archive's
	 * mode is attacker-controlled, and applying it wholesale would carry
	 * setuid and setgid bits straight out of a downloaded file -- which would
	 * turn "the updater unpacks a package" into "the updater creates a setuid
	 * binary". Only the execute bit is worth carrying, and it has to be
	 * carried: a build whose binary arrives without it does not run, and the
	 * handshake then rolls back a package that was perfectly good.
	 */
	bool isExecutable;
};

QString archiveEntryKindToString(ArchiveEntry::Kind k);

//----------------------------------------------------------------------

/**
 * Reads entries out of an archive.
 *
 * An interface for the same reason IFileSystem is one: the extraction logic
 * can then be driven through every malformed archive imaginable without
 * needing to construct one, and without the choice of decoder being settled
 * first. Deciding which zip and tar.xz implementation to depend on is a
 * supply-chain question for a tool of this kind, and is deliberately left to
 * its own change.
 */
class IArchiveReader
{
public:
	virtual ~IArchiveReader() {}

	/** Opens the archive. */
	virtual bool open(const QString &path) = 0;
	virtual void close() = 0;

	/** Every entry, in the order the archive lists them. */
	virtual QList<ArchiveEntry> entries() const = 0;

	/** Reads one entry's bytes. Empty on failure, or for a non-file entry. */
	virtual QByteArray readEntry(const QString &path) = 0;

	virtual QString lastError() const = 0;
};

}; // namespace updater
}; // namespace pe_bear

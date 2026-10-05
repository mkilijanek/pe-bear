#pragma once

#include <QtCore>
#include "ArchiveTypes.h"
#include "ExtractionPolicy.h"
#include "FileSystem.h"
#include "TransactionTypes.h"

namespace pe_bear {
namespace updater {

/**
 * Unpacks a verified package into a staging directory, under the policy.
 *
 * Nothing is written until the whole entry list has been judged. An archive
 * that would be refused at its last entry therefore leaves no files behind at
 * all, rather than a partial tree that looks like a finished one -- which is
 * the state a later step could not tell from success.
 *
 * Every path created is recorded as a TransactionOp in the order it happened,
 * so the transaction can undo the extraction by replaying them backwards.
 */
class PackageExtractor
{
public:
	/** @param fs, @param reader borrowed; both must outlive this object */
	PackageExtractor(IFileSystem *fs, IArchiveReader *reader,
		const ExtractionPolicy &policy = ExtractionPolicy());

	/**
	 * Judges the archive and, if it passes, writes it into @p destDir.
	 *
	 * @param destDir must not already exist: extracting over something would
	 *                make the recorded steps impossible to undo cleanly
	 */
	bool extract(const QString &packagePath, const QString &destDir);

	/** Steps performed, oldest first, for the journal. */
	const QList<TransactionOp>& ops() const { return m_ops; }

	/** Set when the refusal came from the policy rather than from I/O. */
	ExtractionPolicy::Rejection rejection() const { return m_rejection; }

	QString lastError() const { return m_lastError; }

private:
	bool fail(const QString &why);

	IFileSystem *m_fs;
	IArchiveReader *m_reader;
	ExtractionPolicy m_policy;

	QList<TransactionOp> m_ops;
	ExtractionPolicy::Rejection m_rejection;
	QString m_lastError;
};

}; // namespace updater
}; // namespace pe_bear

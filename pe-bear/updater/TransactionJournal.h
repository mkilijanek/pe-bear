#pragma once

#include <QtCore>
#include "TransactionTypes.h"
#include "FileSystem.h"

namespace pe_bear {
namespace updater {

/**
 * Durable record of installation attempts.
 *
 * One file per attempt, in the transactions directory, written whole each time
 * the state advances. Writing goes through IFileSystem::writeFile, which lands
 * the file by rename, so a reader after an interruption sees either the old
 * record or the new one and never a half-written one. That property is the
 * entire reason the journal exists.
 *
 * The journal stores and reads; it does not decide. Deciding what to do with a
 * record found on disk is Transaction::planRecovery.
 */
class TransactionJournal
{
public:
	static const char* FILE_SUFFIX;

	/** @param fs borrowed, must outlive this object */
	TransactionJournal(IFileSystem *fs, const QString &transactionsDir);

	/** Creates the directory if needed. */
	bool prepare();

	QString pathFor(const QString &id) const;

	/** Writes the record, stamping updatedAt. */
	bool write(TransactionRecord &record);

	/**
	 * Reads one record.
	 * @param ok false when the file is absent, unparseable, or carries a state
	 *           this build does not recognise
	 */
	TransactionRecord read(const QString &id, bool *ok = NULL) const;

	/** Ids of every record present, oldest file name first. */
	QStringList listIds() const;

	/**
	 * Records that are not in a terminal state -- the ones an interrupted run
	 * leaves behind and that recovery has to deal with.
	 */
	QList<TransactionRecord> findUnfinished() const;

	/**
	 * Ids whose file exists but cannot be read. Reported separately because
	 * silence would be indistinguishable from "nothing to recover", and an
	 * unreadable journal beside a half-replaced installation is precisely the
	 * case that needs a person.
	 */
	QStringList findUnreadable() const;

	bool remove(const QString &id);

	QString lastError() const { return m_lastError; }

private:
	IFileSystem *m_fs;
	QString m_dir;
	mutable QString m_lastError;
};

}; // namespace updater
}; // namespace pe_bear

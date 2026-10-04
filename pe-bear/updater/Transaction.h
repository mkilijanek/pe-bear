#pragma once

#include <QtCore>
#include "TransactionTypes.h"
#include "TransactionJournal.h"
#include "FileSystem.h"

namespace pe_bear {
namespace updater {

/**
 * Drives one installation through its states, recording each reversible step
 * as it happens so that any interruption can be undone.
 *
 * The rule the whole design rests on: **the journal is written before the
 * action it describes becomes irreversible, never after.** A step that is
 * recorded but not performed costs a harmless no-op during rollback; a step
 * performed but not recorded is a change nobody can undo.
 *
 * Platform activation -- putting the new files in place on a running
 * installation -- is not here. It is the one part that cannot be done without
 * a real Windows machine, and it lives behind PlatformInstaller. What is here
 * is the ordering, the bookkeeping and the recovery decision, all of which are
 * platform-independent and testable with a fake filesystem.
 */
class Transaction
{
public:
	/** What a record found on disk calls for. */
	enum RecoveryAction {
		/** Terminal, or never started: nothing to do beyond tidying up. */
		RecoveryNone = 0,
		/** Changes were made and must be undone. */
		RecoveryRollBack,
		/** Everything succeeded bar the final bookkeeping; finish it. */
		RecoveryFinishCommit,
		/** Cannot be decided automatically; a person has to look. */
		RecoveryManual
	};

	static QString recoveryActionToString(RecoveryAction a);

	/**
	 * Decides what to do with a record, from the record alone.
	 *
	 * Pure: no filesystem, no clock. The same record always yields the same
	 * answer, which is what makes recovery testable and reviewable.
	 */
	static RecoveryAction planRecovery(const TransactionRecord &record);

	/** @param fs, @param journal borrowed; both must outlive this object */
	Transaction(IFileSystem *fs, TransactionJournal *journal);

	/**
	 * Starts an attempt. Writes the Prepared record before touching anything.
	 * @param id empty to generate one
	 */
	bool begin(const TransactionRecord &seed, const QString &id = QString());

	/** Moves the current installation aside. Prepared -> BackedUp. */
	bool backup(const QString &backupDir);

	/**
	 * Records that activation happened. BackedUp -> Activated.
	 * The move itself belongs to the platform layer; what this guarantees is
	 * that it was written down first.
	 */
	bool markActivated(const QList<TransactionOp> &activationOps);

	/** The new build confirmed itself. Activated -> Validated. */
	bool markValidated();

	/** Discards the backup and closes the record. Validated -> Committed. */
	bool commit();

	/**
	 * Undoes every recorded step in reverse order.
	 *
	 * Keeps going after a failed step rather than stopping, because stopping
	 * halfway leaves strictly more damage than trying the rest; the outcome is
	 * RolledBack only if every step succeeded, and Failed otherwise.
	 */
	bool rollBack(const QString &reason);

	/** Marks the attempt as needing a person, with a reason. */
	bool fail(const QString &reason);

	TransactionState state() const { return m_record.state; }
	const TransactionRecord& record() const { return m_record; }
	QString lastError() const { return m_lastError; }

	/** Loads an existing record so recovery can act on it. */
	bool load(const QString &id);

private:
	bool setState(TransactionState next);
	bool undoOp(const TransactionOp &op, QString *error);
	bool refuse(const QString &why);

	IFileSystem *m_fs;
	TransactionJournal *m_journal;
	TransactionRecord m_record;
	QString m_lastError;
};

}; // namespace updater
}; // namespace pe_bear

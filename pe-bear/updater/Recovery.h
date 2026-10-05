#pragma once

#include <QtCore>
#include "FileSystem.h"
#include "TransactionJournal.h"
#include "Transaction.h"

namespace pe_bear {
namespace updater {

/**
 * Acts on what the journal says was left unfinished.
 *
 * Everything needed for this existed and was tested -- planRecovery decides,
 * Transaction::load and rollBack/commit act -- and nothing ever called it. A
 * helper killed between moving the installation aside and putting the new
 * build in place left a directory called .PE-bear-backup-<id> beside an empty
 * space, a record in the journal describing exactly how to undo it, and no
 * code that would read that record. This is that code.
 *
 * It runs in two places, for two different reasons:
 *
 *  - at the start of every helper run, before a new transaction is opened,
 *    so that a second attempt never stacks on top of a half-finished first
 *    one, and so that an abandoned attempt is undone by the next thing that
 *    can undo it;
 *  - when PE-bear starts normally, because after a crashed helper the user
 *    does not launch the helper -- they launch PE-bear.
 *
 * One rule governs both, and it is what makes running this safe at all: a
 * record touched within LIVE_WINDOW_SECONDS is left alone, whatever it says.
 * Every step a helper takes rewrites its record, so a recent record may belong
 * to a helper that is still working -- waiting out the startup handshake, say
 * -- and undoing its work from underneath it would be exactly the damage the
 * journal exists to prevent. An old record belongs to nobody. The window is
 * the same one the helper applies to its own instructions.
 */
class Recovery
{
public:
	/** Same as HelperHandoff::MAX_AGE_SECONDS, for the same reason. */
	static const int LIVE_WINDOW_SECONDS = 15 * 60;

	enum Disposition {
		/** Changes were undone; the previous installation is back. */
		RolledBack = 0,
		/** The new build was kept and the bookkeeping finished. */
		Committed,
		/** Touched too recently; may belong to a helper that is still running. */
		LeftAlive,
		/** Cannot be decided automatically. */
		LeftForAPerson,
		/** The attempt to undo or finish it did not fully succeed. */
		Failed,
		DISPOSITIONS_COUNT
	};

	static QString dispositionToString(Disposition d);

	struct Outcome
	{
		Outcome() : state(TxFailed), planned(Transaction::RecoveryManual), disposition(LeftForAPerson) {}

		QString id;
		QString targetDir;
		TransactionState state;
		Transaction::RecoveryAction planned;
		Disposition disposition;
		/** Human-readable; what the log says. */
		QString note;
	};

	/** @param fs, @param journal borrowed; both must outlive this object */
	Recovery(IFileSystem *fs, TransactionJournal *journal);

	/** Overridden in tests only. */
	void setNow(const QDateTime &now) { m_now = now; }

	/**
	 * Acts on every unfinished record old enough to be safe.
	 *
	 * @param runningFrom  the directory a working PE-bear is running from, or
	 *   empty when called from the helper. An old Activated record for that
	 *   directory is *committed* rather than rolled back: the installation it
	 *   describes is demonstrably working -- this process is it -- and rolling
	 *   it back would move the running installation aside, which on POSIX
	 *   would even succeed.
	 */
	QList<Outcome> run(const QString &runningFrom = QString());

	/**
	 * Whether a new update of @p targetDir must not start. True after run()
	 * when a record for it was left alive, left for a person, or could not be
	 * recovered -- a second transaction on top of any of those would be two
	 * records disagreeing about one directory.
	 */
	bool blocksNewUpdateOf(const QString &targetDir) const;

	/** Records that could not be parsed. Reported, never removed. */
	QStringList unreadableIds() const { return m_unreadable; }

	/** Every line worth putting in a log, in order. */
	QStringList journal() const { return m_log; }

private:
	bool isYoung(const TransactionRecord &record) const;
	Outcome recoverOne(const TransactionRecord &record, const QString &runningFrom);

	IFileSystem *m_fs;
	TransactionJournal *m_journal;
	QDateTime m_now;
	QList<Outcome> m_outcomes;
	QStringList m_unreadable;
	QStringList m_log;
};

}; // namespace updater
}; // namespace pe_bear

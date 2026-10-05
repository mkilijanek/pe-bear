#include "Recovery.h"

namespace pe_bear {
namespace updater {

QString Recovery::dispositionToString(Disposition d)
{
	switch (d) {
		case RolledBack: return QLatin1String("rolled back");
		case Committed: return QLatin1String("committed");
		case LeftAlive: return QLatin1String("left alone, possibly still running");
		case LeftForAPerson: return QLatin1String("left for a person");
		case Failed: return QLatin1String("recovery failed");
		default: return QLatin1String("unknown");
	}
}

Recovery::Recovery(IFileSystem *fs, TransactionJournal *journal)
	: m_fs(fs), m_journal(journal)
{
}

bool Recovery::isYoung(const TransactionRecord &record) const
{
	const QDateTime now = m_now.isValid() ? m_now : QDateTime::currentDateTimeUtc();
	const QDateTime touched = QDateTime::fromString(record.updatedAt, Qt::ISODate);

	/* An unreadable timestamp counts as old, not young. The safe direction
	   would seem to be "young" -- leave it alone -- but a record a live helper
	   is writing always carries a valid stamp, so an unreadable one cannot be
	   live; treating it as young would only block this installation from ever
	   being updated again, with nothing to show for it. */
	if (!touched.isValid()) return false;
	return touched.secsTo(now) < LIVE_WINDOW_SECONDS;
}

Recovery::Outcome Recovery::recoverOne(const TransactionRecord &record, const QString &runningFrom)
{
	Outcome o;
	o.id = record.id;
	o.targetDir = record.targetDir;
	o.state = record.state;
	o.planned = Transaction::planRecovery(record);

	if (isYoung(record)) {
		o.disposition = LeftAlive;
		o.note = QLatin1String("touched less than ")
			+ QString::number(LIVE_WINDOW_SECONDS / 60)
			+ QLatin1String(" minutes ago; may still be running");
		return o;
	}

	/* The one case the planner cannot see: the installation this record says
	   is mid-replacement is the one this very process is running from, and it
	   got as far as being activated. That is stronger proof of a working build
	   than any handshake. Rolling it back would move a running installation
	   aside -- which Windows refuses and POSIX, worse, allows. */
	const bool runningFromIt = !runningFrom.isEmpty()
		&& m_fs->canonicalPath(record.targetDir) == m_fs->canonicalPath(runningFrom);
	if (record.state == TxActivated && runningFromIt) {
		Transaction tx(m_fs, m_journal);
		if (tx.load(record.id) && tx.markValidated() && tx.commit()) {
			o.disposition = Committed;
			o.note = QLatin1String("the activated build is the one now running; kept it");
		} else {
			o.disposition = Failed;
			o.note = QLatin1String("could not keep the running build: ") + tx.lastError();
		}
		return o;
	}

	switch (o.planned) {
		case Transaction::RecoveryRollBack: {
			Transaction tx(m_fs, m_journal);
			if (tx.load(record.id)
				&& tx.rollBack(QLatin1String("recovered after an interrupted update")))
			{
				o.disposition = RolledBack;
				o.note = QLatin1String("previous installation restored");
			} else {
				o.disposition = Failed;
				o.note = QLatin1String("could not restore: ") + tx.lastError();
			}
			return o;
		}
		case Transaction::RecoveryFinishCommit: {
			Transaction tx(m_fs, m_journal);
			if (tx.load(record.id) && tx.commit()) {
				o.disposition = Committed;
				o.note = QLatin1String("the validated build was kept; bookkeeping finished");
			} else {
				o.disposition = Failed;
				o.note = QLatin1String("could not finish: ") + tx.lastError();
			}
			return o;
		}
		case Transaction::RecoveryNone: {
			/* Old, open, and with nothing recorded: a helper that began and
			   died before its first step. Nothing to undo, but the record is
			   closed so it stops looking like work in progress. rollBack on a
			   Prepared record with no steps is exactly that close. */
			Transaction tx(m_fs, m_journal);
			if (tx.load(record.id)
				&& tx.rollBack(QLatin1String("abandoned before any step; closed by recovery")))
			{
				o.disposition = RolledBack;
				o.note = QLatin1String("nothing had happened; record closed");
			} else {
				o.disposition = Failed;
				o.note = QLatin1String("could not close an empty record: ") + tx.lastError();
			}
			return o;
		}
		default:
			o.disposition = LeftForAPerson;
			o.note = QLatin1String("cannot be recovered automatically: ")
				+ (record.error.isEmpty() ? QLatin1String("see the record") : record.error);
			return o;
	}
}

QList<Recovery::Outcome> Recovery::run(const QString &runningFrom)
{
	m_outcomes.clear();
	m_unreadable.clear();
	m_log.clear();

	if (!m_fs || !m_journal) return m_outcomes;

	m_unreadable = m_journal->findUnreadable();
	for (int i = 0; i < m_unreadable.size(); i++) {
		/* Reported and kept. A record that cannot be parsed is the only trace
		   of whatever happened; deleting it would convert a recoverable
		   mystery into an unrecoverable one. */
		m_log << (QLatin1String("recovery: record ") + m_unreadable.at(i)
			+ QLatin1String(" is unreadable and was left in place"));
	}

	/* Every record, not findUnfinished(): that excludes Failed as terminal,
	   and Failed is precisely the state that says a rollback did not complete
	   and a person is needed. Recovery has to see it to report it and to
	   refuse a new attempt on top of it. The planner decides what each one
	   means; records it calls RecoveryNone -- committed, rolled back -- are
	   skipped silently, or every past update would be listed forever. */
	const QStringList ids = m_journal->listIds();
	for (int i = 0; i < ids.size(); i++) {
		bool ok = false;
		const TransactionRecord rec = m_journal->read(ids.at(i), &ok);
		if (!ok) continue; /* in m_unreadable already */
		/* Only the two states that mean "finished, nothing owed" are skipped.
		   Not "plan is None": a record that has only just been begun -- Prepared,
		   no steps yet -- also plans to nothing, and skipping it would make a
		   helper that started seconds ago invisible to the next one. */
		if (rec.state == TxCommitted || rec.state == TxRolledBack) continue;
		const Outcome o = recoverOne(rec, runningFrom);
		m_outcomes << o;
		m_log << (QLatin1String("recovery: ") + o.id + QLatin1String(" (")
			+ transactionStateToString(o.state) + QLatin1String(", ")
			+ QDir::toNativeSeparators(o.targetDir) + QLatin1String("): ")
			+ dispositionToString(o.disposition) + QLatin1String(" -- ") + o.note);
	}
	return m_outcomes;
}

bool Recovery::blocksNewUpdateOf(const QString &targetDir) const
{
	const QString wanted = m_fs ? m_fs->canonicalPath(targetDir) : targetDir;
	for (int i = 0; i < m_outcomes.size(); i++) {
		const Outcome &o = m_outcomes.at(i);
		if (o.disposition != LeftAlive && o.disposition != LeftForAPerson
			&& o.disposition != Failed) continue;
		const QString theirs = m_fs ? m_fs->canonicalPath(o.targetDir) : o.targetDir;
		/* A target that no longer resolves -- moved aside and never restored --
		   is compared by the recorded string instead, or it could never block. */
		if ((!theirs.isEmpty() && theirs == wanted) || o.targetDir == targetDir) return true;
	}
	return false;
}

}; // namespace updater
}; // namespace pe_bear

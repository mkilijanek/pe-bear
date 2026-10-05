/*
 * Covers the one thing the journal was always for and nothing ever did:
 * acting on an unfinished record.
 *
 * planRecovery decides and Transaction::load/rollBack/commit act, and both
 * were tested on their own. What was missing -- and what #33 is about -- is
 * the step that reads the journal and calls them. These tests drive that
 * step over a fake filesystem with real Transaction and TransactionJournal
 * objects, so what is checked is the whole chain from "a record exists" to
 * "the installation is back".
 *
 * The clock is injected. The live window -- records touched within the last
 * fifteen minutes are left alone -- is the rule that makes recovery safe to
 * run at all, and it has to be testable without waiting fifteen minutes.
 */
#include <QtTest>
#include "../Recovery.h"
#include "../Transaction.h"
#include "../TransactionJournal.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	const char* TARGET = "/opt/pe-bear";
	const char* TXDIR  = "/u/.pe-bear/updates/transactions";
	const char* STAGE  = "/opt/.PE-bear-staging/tx";
	const char* BACKUP = "/opt/.PE-bear-backup-tx";

	TransactionRecord seed()
	{
		TransactionRecord r;
		r.targetDir = QLatin1String(TARGET);
		r.stagingDir = QLatin1String(STAGE);
		r.stagingRoot = QLatin1String("/opt/.PE-bear-staging");
		r.packagePath = QLatin1String("/u/.pe-bear/updates/downloads/r/pkg.tar.xz");
		r.packageSize = 4096;
		r.packageSha256 = QString(64, QLatin1Char('a'));
		r.fromVersion = QLatin1String("0.7.2");
		r.toVersion = QLatin1String("0.7.3");
		return r;
	}

	void installTarget(FakeFileSystem &fs)
	{
		fs.setWritableDefault(true);
		fs.addFile(QLatin1String(TARGET) + QLatin1String("/PE-bear"), QByteArray("old-binary"));
		fs.addFile(QLatin1String(TARGET) + QLatin1String("/notes.tag"), QByteArray("keep"));
	}

	/** Well past the live window. */
	QDateTime later() { return QDateTime::currentDateTimeUtc().addSecs(Recovery::LIVE_WINDOW_SECONDS + 60); }

}; // namespace

class TestRecovery : public QObject
{
	Q_OBJECT

private slots:
	void nothingToDoIsNotAnError();
	void anAbandonedBackupIsRolledBack();
	void anAbandonedActivationIsRolledBackByTheHelper();
	void anAbandonedActivationIsKeptWhenThatBuildIsRunning();
	void aValidatedRecordIsCommitted();
	void aYoungRecordIsLeftAloneAndBlocksTheTarget();
	void aJustBegunRecordBlocksWhileYoungAndIsClosedWhenOld();
	void aFailedRecordIsLeftForAPersonAndBlocksTheTarget();
	void anUnreadableRecordIsReportedNotRemoved();
	void aFailedRollbackBlocksTheTarget();
	void otherTargetsDoNotBlock();
	void theLogSaysWhatHappened();

private:
	/** Leaves a record in @p state behind, as a dead helper would. */
	void abandonAt(FakeFileSystem &fs, TransactionJournal &j, TransactionState state,
		const QString &id = QLatin1String("tx"));
};

void TestRecovery::abandonAt(FakeFileSystem &fs, TransactionJournal &j, TransactionState state,
		const QString &id)
{
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), id));
	if (state == TxPrepared) return;
	QVERIFY(tx.backup(QLatin1String(BACKUP)));
	if (state == TxBackedUp) return;
	/* activation: the staged build lands where the installation was */
	fs.addFile(QLatin1String(STAGE) + QLatin1String("/PE-bear"), QByteArray("new-binary"));
	QVERIFY(fs.movePath(QLatin1String(STAGE), QLatin1String(TARGET)));
	QList<TransactionOp> ops;
	ops << TransactionOp(TransactionOp::OpMoved, QLatin1String(STAGE), QLatin1String(TARGET));
	QVERIFY(tx.markActivated(ops));
	if (state == TxActivated) return;
	QVERIFY(tx.markValidated());
	if (state == TxValidated) return;
	QFAIL("abandonAt: unsupported state");
}

void TestRecovery::nothingToDoIsNotAnError()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());

	Recovery r(&fs, &j);
	QVERIFY(r.run().isEmpty());
	QVERIFY(!r.blocksNewUpdateOf(QLatin1String(TARGET)));
	QVERIFY(r.unreadableIds().isEmpty());
}

void TestRecovery::anAbandonedBackupIsRolledBack()
{
	/* The scenario the journal exists for: the helper died after moving the
	   installation aside and before putting anything in its place. */
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	abandonAt(fs, j, TxBackedUp);
	QVERIFY(!fs.hasFile(QLatin1String(TARGET) + QLatin1String("/PE-bear")));

	Recovery r(&fs, &j);
	r.setNow(later());
	const QList<Recovery::Outcome> out = r.run();

	QCOMPARE(out.size(), 1);
	QCOMPARE(out.at(0).disposition, Recovery::RolledBack);
	QCOMPARE(fs.contentOf(QLatin1String(TARGET) + QLatin1String("/PE-bear")), QByteArray("old-binary"));
	QCOMPARE(fs.contentOf(QLatin1String(TARGET) + QLatin1String("/notes.tag")), QByteArray("keep"));
	QVERIFY(j.findUnfinished().isEmpty());
	QVERIFY(!r.blocksNewUpdateOf(QLatin1String(TARGET)));
}

void TestRecovery::anAbandonedActivationIsRolledBackByTheHelper()
{
	/* The new build is in place but nothing ever confirmed it. The helper
	   has no way to vouch for it, so the previous build comes back. */
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	abandonAt(fs, j, TxActivated);
	QCOMPARE(fs.contentOf(QLatin1String(TARGET) + QLatin1String("/PE-bear")), QByteArray("new-binary"));

	Recovery r(&fs, &j);
	r.setNow(later());
	const QList<Recovery::Outcome> out = r.run(/* not running from it */);

	QCOMPARE(out.size(), 1);
	QCOMPARE(out.at(0).disposition, Recovery::RolledBack);
	QCOMPARE(fs.contentOf(QLatin1String(TARGET) + QLatin1String("/PE-bear")), QByteArray("old-binary"));
}

void TestRecovery::anAbandonedActivationIsKeptWhenThatBuildIsRunning()
{
	/* Same record, but the caller is a PE-bear running from that directory.
	   That is stronger proof the build works than any handshake, and rolling
	   it back would move a running installation aside. */
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	abandonAt(fs, j, TxActivated);

	Recovery r(&fs, &j);
	r.setNow(later());
	const QList<Recovery::Outcome> out = r.run(QLatin1String(TARGET));

	QCOMPARE(out.size(), 1);
	QCOMPARE(out.at(0).disposition, Recovery::Committed);
	QCOMPARE(fs.contentOf(QLatin1String(TARGET) + QLatin1String("/PE-bear")), QByteArray("new-binary"));
	QVERIFY2(!fs.hasDir(QLatin1String(BACKUP)), "the backup should have been discarded on commit");
	QVERIFY(j.findUnfinished().isEmpty());
}

void TestRecovery::aValidatedRecordIsCommitted()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	abandonAt(fs, j, TxValidated);

	Recovery r(&fs, &j);
	r.setNow(later());
	const QList<Recovery::Outcome> out = r.run();

	QCOMPARE(out.size(), 1);
	QCOMPARE(out.at(0).disposition, Recovery::Committed);
	QCOMPARE(fs.contentOf(QLatin1String(TARGET) + QLatin1String("/PE-bear")), QByteArray("new-binary"));
	QVERIFY(!fs.hasDir(QLatin1String(BACKUP)));
}

void TestRecovery::aYoungRecordIsLeftAloneAndBlocksTheTarget()
{
	/* Touched a moment ago: a helper may be mid-run, waiting out the startup
	   handshake. Undoing its work from underneath it is exactly the damage
	   the journal exists to prevent. */
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	abandonAt(fs, j, TxBackedUp);

	Recovery r(&fs, &j);
	/* real clock: the record was written just now */
	const QList<Recovery::Outcome> out = r.run();

	QCOMPARE(out.size(), 1);
	QCOMPARE(out.at(0).disposition, Recovery::LeftAlive);
	QVERIFY2(!fs.hasFile(QLatin1String(TARGET) + QLatin1String("/PE-bear")),
		"a young record was acted on");
	QVERIFY(r.blocksNewUpdateOf(QLatin1String(TARGET)));
	QVERIFY(!j.findUnfinished().isEmpty());
}

void TestRecovery::aJustBegunRecordBlocksWhileYoungAndIsClosedWhenOld()
{
	/* begin() and nothing else: the planner says there is nothing to undo,
	   which is true -- and is also what a helper looks like seconds after it
	   started. Young, it must block the next helper; old, it is an abandoned
	   beginning and gets closed rather than listed forever. */
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	abandonAt(fs, j, TxPrepared);

	{
		Recovery young(&fs, &j);
		const QList<Recovery::Outcome> out = young.run();
		QCOMPARE(out.size(), 1);
		QCOMPARE(out.at(0).disposition, Recovery::LeftAlive);
		QVERIFY2(young.blocksNewUpdateOf(QLatin1String(TARGET)),
			"a helper that has just begun was invisible to the next one");
	}
	{
		Recovery old(&fs, &j);
		old.setNow(later());
		const QList<Recovery::Outcome> out = old.run();
		QCOMPARE(out.size(), 1);
		QCOMPARE(out.at(0).disposition, Recovery::RolledBack);
		QVERIFY(!old.blocksNewUpdateOf(QLatin1String(TARGET)));
		/* closed: a second pass finds nothing owed */
		Recovery again(&fs, &j);
		again.setNow(later());
		QVERIFY(again.run().isEmpty());
	}
	/* And the installation was never touched by any of it. */
	QCOMPARE(fs.contentOf(QLatin1String(TARGET) + QLatin1String("/PE-bear")), QByteArray("old-binary"));
}

void TestRecovery::aFailedRecordIsLeftForAPersonAndBlocksTheTarget()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	{
		Transaction tx(&fs, &j);
		QVERIFY(tx.begin(seed(), QLatin1String("tx")));
		QVERIFY(tx.fail(QLatin1String("gave up once already")));
	}

	Recovery r(&fs, &j);
	r.setNow(later());
	const QList<Recovery::Outcome> out = r.run();

	QCOMPARE(out.size(), 1);
	QCOMPARE(out.at(0).disposition, Recovery::LeftForAPerson);
	QVERIFY(out.at(0).note.contains(QLatin1String("gave up once already")));
	QVERIFY(r.blocksNewUpdateOf(QLatin1String(TARGET)));
}

void TestRecovery::anUnreadableRecordIsReportedNotRemoved()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	const QString path = j.pathFor(QLatin1String("broken"));
	fs.addFile(path, QByteArray("{ this is not a record"));

	Recovery r(&fs, &j);
	r.setNow(later());
	r.run();

	QCOMPARE(r.unreadableIds().size(), 1);
	QVERIFY2(fs.hasFile(path), "an unreadable record was deleted");
	/* It cannot be matched to a target, so it does not block one. */
	QVERIFY(!r.blocksNewUpdateOf(QLatin1String(TARGET)));
	QVERIFY(r.journal().join(QLatin1String("\n")).contains(QLatin1String("unreadable")));
}

void TestRecovery::aFailedRollbackBlocksTheTarget()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	abandonAt(fs, j, TxBackedUp);
	/* The restore cannot happen. */
	fs.failAlways(QLatin1String("movePath"));

	Recovery r(&fs, &j);
	r.setNow(later());
	const QList<Recovery::Outcome> out = r.run();

	QCOMPARE(out.size(), 1);
	QCOMPARE(out.at(0).disposition, Recovery::Failed);
	QVERIFY(r.blocksNewUpdateOf(QLatin1String(TARGET)));
}

void TestRecovery::otherTargetsDoNotBlock()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	abandonAt(fs, j, TxBackedUp);

	Recovery r(&fs, &j);
	/* young, so it blocks its own target... */
	r.run();
	QVERIFY(r.blocksNewUpdateOf(QLatin1String(TARGET)));
	/* ...and nobody else's */
	QVERIFY(!r.blocksNewUpdateOf(QLatin1String("/opt/other")));
}

void TestRecovery::theLogSaysWhatHappened()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	QVERIFY(j.prepare());
	abandonAt(fs, j, TxBackedUp);

	Recovery r(&fs, &j);
	r.setNow(later());
	r.run();

	const QString log = r.journal().join(QLatin1String("\n"));
	QVERIFY2(log.contains(QLatin1String("tx")), qPrintable(log));
	QVERIFY2(log.contains(QLatin1String("rolled back")), qPrintable(log));
	QVERIFY2(log.contains(QLatin1String("restored")), qPrintable(log));
}

QTEST_GUILESS_MAIN(TestRecovery)
#include "tst_recovery.moc"

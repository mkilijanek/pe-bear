/*
 * Covers the transaction state machine of #18: legal transitions, the ordering
 * of a rollback, and what a record found on disk calls for.
 *
 * All of it runs against FakeFileSystem, which is the point of the interface:
 * a real filesystem cannot be asked to fail a rename at the third call, and
 * that is precisely the situation the journal exists for.
 */
#include <QtTest>
#include "../Transaction.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	const char* DIGEST = "c9aeff95175eed76d47a8dad7d20d531fd1f318ccd202220841813a55e21e4fe";
	const char* TARGET = "/opt/pe-bear";
	const char* BACKUP = "/var/pe-bear-updates/backups/tx-1";
	const char* TXDIR  = "/var/pe-bear-updates/transactions";
	const char* STAGING_ROOT = "/var/pe-bear-updates/staging";

	TransactionRecord seed()
	{
		TransactionRecord r;
		r.targetDir = QLatin1String(TARGET);
		r.stagingDir = QLatin1String(STAGING_ROOT) + QLatin1String("/tx-1");
		r.stagingRoot = QLatin1String(STAGING_ROOT);
		r.packagePath = QLatin1String("/var/pe-bear-updates/downloads/pkg.zip");
		r.packageSize = 2048;
		r.packageSha256 = QLatin1String(DIGEST);
		r.fromVersion = QLatin1String("0.7.2");
		r.toVersion = QLatin1String("0.7.3");
		return r;
	}

	/** An installed tree, so a backup has something real to move. */
	void installTarget(FakeFileSystem &fs)
	{
		fs.addDir(QLatin1String(TARGET));
		fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("old-binary"));
		fs.addFile(QLatin1String("/opt/pe-bear/SIG.txt"), QByteArray("old-sigs"));
	}

	QList<TransactionOp> activationOps()
	{
		QList<TransactionOp> ops;
		ops << TransactionOp(TransactionOp::OpCreatedDir, QLatin1String(TARGET))
		    << TransactionOp(TransactionOp::OpCreated, QLatin1String("/opt/pe-bear/PE-bear"));
		return ops;
	}

}; // namespace

class TestTransaction : public QObject
{
	Q_OBJECT

private slots:
	/* --- the happy path and its guards --- */
	void beginWritesTheRecordBeforeTouchingAnything();
	void beginRefusesAnIncompleteAttempt();
	void fullPathReachesCommitted();
	void commitRemovesTheBackup();
	void commitSurvivesABackupThatCannotBeRemoved();

	/* --- transitions are not optional --- */
	void stepsOutOfOrderAreRefused_data();
	void stepsOutOfOrderAreRefused();
	void activationRefusesAnIncompleteStep();

	/* --- the backup step --- */
	void backupMovesTheInstallationAside();
	void backupRefusesAnExistingBackupDirectory();
	void backupRefusesWhenThereIsNothingInstalled();
	void aRecordedMoveSurvivesAFailedMove();

	/* --- rollback --- */
	void rollBackRestoresTheInstallation();
	void rollBackUndoesInReverseOrder();
	void rollBackIsANoOpForStepsThatNeverRan();
	void rollBackKeepsGoingAfterAFailedStepAndReportsFailed();
	void rollBackIsRefusedFromATerminalState_data();
	void rollBackIsRefusedFromATerminalState();

	/* --- the staging root's lifecycle --- */
	void commitReclaimsAnEmptyStagingRoot();
	void commitLeavesAnotherRunInTheRootAlone();
	void commitRecordsARootThatCannotBeRemoved();
	void rollBackReclaimsAnEmptyStagingRoot();
	void rollBackLeavesAnotherRunInTheRootAlone();
	void aRecordFromBeforeTheRootFieldSkipsReclaim();
	void aRecordFromBeforeTheRootFieldRoundTrips();

	/* --- recovery decisions, pure --- */
	void planRecovery_data();
	void planRecovery();
	void planRecoveryRefusesAnInvalidRecord();
	void recoveryFromAnInterruptedBackupRestoresEverything();

	/* --- journal failures --- */
	void aFailedJournalWriteAbandonsTheTransition();
};

void TestTransaction::beginWritesTheRecordBeforeTouchingAnything()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);

	QVERIFY2(tx.begin(seed(), QLatin1String("tx-1")), qPrintable(tx.lastError()));
	QCOMPARE(tx.state(), TxPrepared);
	/* the record exists before any installation file has moved */
	QVERIFY(fs.hasFile(QString(TXDIR) + "/tx-1.json"));
	QVERIFY(fs.hasFile(QLatin1String("/opt/pe-bear/PE-bear")));
}

void TestTransaction::beginRefusesAnIncompleteAttempt()
{
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);

	TransactionRecord noDigest = seed();
	noDigest.packageSha256.clear();
	QVERIFY2(!tx.begin(noDigest), "an attempt with no digest was accepted");

	TransactionRecord noTarget = seed();
	noTarget.targetDir.clear();
	QVERIFY(!tx.begin(noTarget));
}

void TestTransaction::fullPathReachesCommitted()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);

	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QCOMPARE(tx.state(), TxPrepared);
	QVERIFY2(tx.backup(QLatin1String(BACKUP)), qPrintable(tx.lastError()));
	QCOMPARE(tx.state(), TxBackedUp);

	/* the platform layer would have put the new files in place here */
	fs.addDir(QLatin1String(TARGET));
	fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("new-binary"));
	QVERIFY2(tx.markActivated(activationOps()), qPrintable(tx.lastError()));
	QCOMPARE(tx.state(), TxActivated);

	QVERIFY(tx.markValidated());
	QCOMPARE(tx.state(), TxValidated);
	QVERIFY2(tx.commit(), qPrintable(tx.lastError()));
	QCOMPARE(tx.state(), TxCommitted);
}

void TestTransaction::commitRemovesTheBackup()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);

	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));
	QVERIFY(fs.hasFile(QString(BACKUP) + "/PE-bear"));

	fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("new-binary"));
	QVERIFY(tx.markActivated(activationOps()));
	QVERIFY(tx.markValidated());
	QVERIFY(tx.commit());

	QVERIFY2(!fs.hasDir(QLatin1String(BACKUP)), "the backup outlived the commit");
}

void TestTransaction::commitSurvivesABackupThatCannotBeRemoved()
{
	/* A leftover backup is wasted space, not a broken installation: the commit
	   must stand and the problem be recorded. */
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);

	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));
	fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("new"));
	QVERIFY(tx.markActivated(activationOps()));
	QVERIFY(tx.markValidated());

	fs.failAlways(QLatin1String("removeDirRecursively"));
	QVERIFY2(tx.commit(), "a leftover backup was treated as a failed install");
	QCOMPARE(tx.state(), TxCommitted);
	QVERIFY2(!tx.record().error.isEmpty(), "the leftover was not recorded");
}

void TestTransaction::stepsOutOfOrderAreRefused_data()
{
	QTest::addColumn<QString>("step");
	QTest::newRow("activate before backup") << "activate";
	QTest::newRow("validate before activate") << "validate";
	QTest::newRow("commit before validate") << "commit";
}

void TestTransaction::stepsOutOfOrderAreRefused()
{
	QFETCH(QString, step);
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));

	bool accepted = true;
	if (step == QLatin1String("activate")) accepted = tx.markActivated(activationOps());
	else if (step == QLatin1String("validate")) accepted = tx.markValidated();
	else accepted = tx.commit();

	QVERIFY2(!accepted, qPrintable(QString("accepted out of order: ") + step));
	QCOMPARE(tx.state(), TxPrepared);
	QVERIFY(!tx.lastError().isEmpty());
}

void TestTransaction::activationRefusesAnIncompleteStep()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));

	QList<TransactionOp> broken;
	broken << TransactionOp(TransactionOp::OpCreatedDir, QLatin1String(TARGET))
	       << TransactionOp(); /* kind OpNone */
	QVERIFY2(!tx.markActivated(broken), "an un-undoable step was recorded");
	QCOMPARE(tx.state(), TxBackedUp);
}

void TestTransaction::backupMovesTheInstallationAside()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));

	QVERIFY(!fs.hasFile(QLatin1String("/opt/pe-bear/PE-bear")));
	QCOMPARE(fs.contentOf(QString(BACKUP) + "/PE-bear"), QByteArray("old-binary"));
	QCOMPARE(fs.contentOf(QString(BACKUP) + "/SIG.txt"), QByteArray("old-sigs"));
	QCOMPARE(tx.record().ops.size(), 1);
	QCOMPARE(tx.record().ops.at(0).kind, TransactionOp::OpMoved);
}

void TestTransaction::backupRefusesAnExistingBackupDirectory()
{
	FakeFileSystem fs;
	installTarget(fs);
	fs.addDir(QLatin1String(BACKUP));
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));

	QVERIFY2(!tx.backup(QLatin1String(BACKUP)), "wrote into an existing backup directory");
	QCOMPARE(tx.state(), TxPrepared);
}

void TestTransaction::backupRefusesWhenThereIsNothingInstalled()
{
	FakeFileSystem fs; /* no target */
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));

	QVERIFY(!tx.backup(QLatin1String(BACKUP)));
	QCOMPARE(tx.state(), TxPrepared);
}

void TestTransaction::aRecordedMoveSurvivesAFailedMove()
{
	/* The step stays in the journal even though the move failed. Undoing a
	   move that never happened is a harmless no-op; removing the record and
	   being wrong about whether it happened is unrecoverable. */
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));

	fs.failAlways(QLatin1String("movePath"));
	QVERIFY2(!tx.backup(QLatin1String(BACKUP)), "a failed move reported success");
	QCOMPARE(tx.state(), TxPrepared);
	QCOMPARE(tx.record().ops.size(), 1);

	fs.clearFailures();
	QVERIFY2(tx.rollBack(QLatin1String("move failed")), qPrintable(tx.lastError()));
	QCOMPARE(tx.state(), TxRolledBack);
	QVERIFY2(fs.hasFile(QLatin1String("/opt/pe-bear/PE-bear")), "the installation was lost");
}

void TestTransaction::rollBackRestoresTheInstallation()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));

	fs.addDir(QLatin1String(TARGET));
	fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("new-binary"));
	QVERIFY(tx.markActivated(activationOps()));

	QVERIFY2(tx.rollBack(QLatin1String("startup validation failed")), qPrintable(tx.lastError()));
	QCOMPARE(tx.state(), TxRolledBack);
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/SIG.txt")), QByteArray("old-sigs"));
	QVERIFY(!fs.hasDir(QLatin1String(BACKUP)));
}

void TestTransaction::rollBackUndoesInReverseOrder()
{
	/* Forward order would try to restore the backup while the new tree is
	   still sitting on top of the target. */
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));

	fs.addDir(QLatin1String(TARGET));
	fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("new"));
	QVERIFY(tx.markActivated(activationOps()));

	/* ops are: Moved(target->backup), CreatedDir(target), Created(target/PE-bear) */
	QCOMPARE(tx.record().ops.size(), 3);
	QVERIFY(tx.rollBack(QLatin1String("reverse order check")));

	/* only a reverse walk can both remove the new tree and restore the old */
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
	QVERIFY(fs.hasFile(QLatin1String("/opt/pe-bear/SIG.txt")));
}

void TestTransaction::rollBackIsANoOpForStepsThatNeverRan()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));

	/* nothing was done, so there is nothing to undo */
	QVERIFY(tx.rollBack(QLatin1String("user cancelled")));
	QCOMPARE(tx.state(), TxRolledBack);
	QVERIFY(fs.hasFile(QLatin1String("/opt/pe-bear/PE-bear")));
}

void TestTransaction::rollBackKeepsGoingAfterAFailedStepAndReportsFailed()
{
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));

	fs.addDir(QLatin1String(TARGET));
	fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("new"));
	QVERIFY(tx.markActivated(activationOps()));

	/* the new binary cannot be removed; the rest must still be attempted */
	fs.failForPath(QLatin1String("removeFile"), QLatin1String("/opt/pe-bear/PE-bear"));
	QVERIFY2(!tx.rollBack(QLatin1String("validation failed")),
		"an incomplete rollback reported success");
	QCOMPARE(tx.state(), TxFailed);
	QVERIFY2(tx.record().error.contains(QLatin1String("rollback incomplete")),
		qPrintable(tx.record().error));
	/* and it did keep going: the later steps were still attempted */
	QVERIFY(fs.callCount(QLatin1String("movePath")) >= 2);
}

void TestTransaction::rollBackIsRefusedFromATerminalState_data()
{
	QTest::addColumn<int>("state");
	QTest::newRow("Committed") << int(TxCommitted);
	QTest::newRow("RolledBack") << int(TxRolledBack);
	QTest::newRow("Failed") << int(TxFailed);
}

void TestTransaction::rollBackIsRefusedFromATerminalState()
{
	QFETCH(int, state);
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));

	TransactionRecord r = seed();
	r.id = QLatin1String("tx-term");
	r.state = TransactionState(state);
	QVERIFY(j.prepare());
	QVERIFY(j.write(r));

	Transaction tx(&fs, &j);
	QVERIFY(tx.load(QLatin1String("tx-term")));
	QVERIFY2(!tx.rollBack(QLatin1String("again")), "rolled back from a terminal state");
}

void TestTransaction::planRecovery_data()
{
	QTest::addColumn<int>("state");
	QTest::addColumn<bool>("withOps");
	QTest::addColumn<int>("expected");

	QTest::newRow("Prepared, nothing done")   << int(TxPrepared)   << false << int(Transaction::RecoveryNone);
	QTest::newRow("Prepared, a step recorded")<< int(TxPrepared)   << true  << int(Transaction::RecoveryRollBack);
	QTest::newRow("BackedUp")                 << int(TxBackedUp)   << true  << int(Transaction::RecoveryRollBack);
	QTest::newRow("Activated")                << int(TxActivated)  << true  << int(Transaction::RecoveryRollBack);
	QTest::newRow("Validated")                << int(TxValidated)  << true  << int(Transaction::RecoveryFinishCommit);
	QTest::newRow("Committed")                << int(TxCommitted)  << true  << int(Transaction::RecoveryNone);
	QTest::newRow("RolledBack")               << int(TxRolledBack) << true  << int(Transaction::RecoveryNone);
	QTest::newRow("Failed")                   << int(TxFailed)     << true  << int(Transaction::RecoveryManual);
}

void TestTransaction::planRecovery()
{
	QFETCH(int, state);
	QFETCH(bool, withOps);
	QFETCH(int, expected);

	TransactionRecord r = seed();
	r.id = QLatin1String("tx-1");
	r.state = TransactionState(state);
	if (withOps) {
		r.ops << TransactionOp(TransactionOp::OpMoved, QLatin1String(TARGET), QLatin1String(BACKUP));
	}
	QCOMPARE(int(Transaction::planRecovery(r)), expected);
}

void TestTransaction::planRecoveryRefusesAnInvalidRecord()
{
	/* A record missing its details may still describe a half-replaced
	   installation; guessing would be worse than asking for a person. */
	TransactionRecord broken;
	QCOMPARE(Transaction::planRecovery(broken), Transaction::RecoveryManual);

	TransactionRecord noDigest = seed();
	noDigest.id = QLatin1String("tx-1");
	noDigest.state = TxActivated;
	noDigest.packageSha256.clear();
	QCOMPARE(Transaction::planRecovery(noDigest), Transaction::RecoveryManual);
}

void TestTransaction::recoveryFromAnInterruptedBackupRestoresEverything()
{
	/* The scenario the journal exists for: the process died between the backup
	   and activation. A new run loads the record and must put things back. */
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));

	{
		Transaction first(&fs, &j);
		QVERIFY(first.begin(seed(), QLatin1String("tx-1")));
		QVERIFY(first.backup(QLatin1String(BACKUP)));
		/* ... and the process is gone here */
	}

	QVERIFY(!fs.hasFile(QLatin1String("/opt/pe-bear/PE-bear")));

	const QList<TransactionRecord> unfinished = j.findUnfinished();
	QCOMPARE(unfinished.size(), 1);
	QCOMPARE(unfinished.at(0).state, TxBackedUp);
	QCOMPARE(Transaction::planRecovery(unfinished.at(0)), Transaction::RecoveryRollBack);

	Transaction second(&fs, &j);
	QVERIFY(second.load(QLatin1String("tx-1")));
	QVERIFY2(second.rollBack(QLatin1String("recovered after interruption")),
		qPrintable(second.lastError()));

	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/SIG.txt")), QByteArray("old-sigs"));
	QVERIFY(j.findUnfinished().isEmpty());
}

void TestTransaction::aFailedJournalWriteAbandonsTheTransition()
{
	/* If the state cannot be recorded, the state must not advance in memory
	   either -- otherwise the process believes something the disk denies. */
	FakeFileSystem fs;
	installTarget(fs);
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);
	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));

	fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("new"));
	fs.failAlways(QLatin1String("writeFile"));
	QVERIFY2(!tx.markActivated(activationOps()), "a transition survived a failed journal write");
	QCOMPARE(tx.state(), TxBackedUp);
	QCOMPARE(tx.record().ops.size(), 1);
}

void TestTransaction::commitReclaimsAnEmptyStagingRoot()
{
	/* The root is shared ground beside somebody's installation; an empty one
	   left after every update is litter in a place the updater does not own.
	   Reclaiming is the transaction's job now the record names it. */
	FakeFileSystem fs;
	installTarget(fs);
	fs.addDir(QLatin1String(STAGING_ROOT));
	fs.addDir(QLatin1String(STAGING_ROOT) + QLatin1String("/tx-1"));
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);

	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));
	fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("new"));
	QVERIFY(tx.markActivated(activationOps()));
	QVERIFY(tx.markValidated());

	QVERIFY2(tx.commit(), qPrintable(tx.lastError()));
	QVERIFY2(!fs.hasDir(QLatin1String(STAGING_ROOT)), "an empty staging root outlived the commit");
	/* and the reclaim was the careful kind: the two recursive removals are the
	   backup and the staging tree, never the root itself */
	QCOMPARE(fs.callCount(QLatin1String("removeEmptyDir")), 1);
	QCOMPARE(fs.callCount(QLatin1String("removeDirRecursively")), 2);
}

void TestTransaction::commitLeavesAnotherRunInTheRootAlone()
{
	/* Two helpers can be mid-update at once. A commit must not take the other
	   run's tree with it, which is why the root goes only when empty. */
	FakeFileSystem fs;
	installTarget(fs);
	fs.addDir(QLatin1String(STAGING_ROOT));
	fs.addDir(QLatin1String(STAGING_ROOT) + QLatin1String("/tx-1"));
	fs.addFile(QLatin1String(STAGING_ROOT) + QLatin1String("/other-run/pkg.zip"),
		QByteArray("in flight"));
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);

	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));
	fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("new"));
	QVERIFY(tx.markActivated(activationOps()));
	QVERIFY(tx.markValidated());

	QVERIFY2(tx.commit(), "a shared staging root failed the commit");
	QCOMPARE(tx.state(), TxCommitted);
	/* the other run's tree is untouched, and the leftover is recorded so a
	   person reading the journal can tell "shared" from "stuck" */
	QVERIFY(fs.hasFile(QLatin1String(STAGING_ROOT) + QLatin1String("/other-run/pkg.zip")));
	QVERIFY2(tx.record().error.contains(QLatin1String("staging root")),
		qPrintable(tx.record().error));
}

void TestTransaction::commitRecordsARootThatCannotBeRemoved()
{
	/* Same contract as a leftover backup: the installation is already correct,
	   so the failure is a recorded problem, never a failed install. */
	FakeFileSystem fs;
	installTarget(fs);
	fs.addDir(QLatin1String(STAGING_ROOT));
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);

	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));
	fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("new"));
	QVERIFY(tx.markActivated(activationOps()));
	QVERIFY(tx.markValidated());

	fs.failAlways(QLatin1String("removeEmptyDir"));
	QVERIFY2(tx.commit(), "a stuck staging root was treated as a failed install");
	QCOMPARE(tx.state(), TxCommitted);
	QVERIFY2(tx.record().error.contains(QLatin1String("staging root")),
		qPrintable(tx.record().error));
}

void TestTransaction::rollBackReclaimsAnEmptyStagingRoot()
{
	FakeFileSystem fs;
	installTarget(fs);
	fs.addDir(QLatin1String(STAGING_ROOT));
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);

	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));

	QVERIFY2(tx.rollBack(QLatin1String("package rejected")), qPrintable(tx.lastError()));
	QCOMPARE(tx.state(), TxRolledBack);
	QVERIFY2(!fs.hasDir(QLatin1String(STAGING_ROOT)), "an empty staging root outlived the rollback");
	/* the installation is whole, and a reclaimed root is not an error */
	QVERIFY(fs.hasFile(QLatin1String("/opt/pe-bear/PE-bear")));
	QVERIFY(!tx.record().error.contains(QLatin1String("staging root")));
}

void TestTransaction::rollBackLeavesAnotherRunInTheRootAlone()
{
	/* A root that will not go away is a note, not a problem: by this point the
	   installation is restored, and refusing to report success over litter
	   would tell the user to reinstall for no reason. */
	FakeFileSystem fs;
	installTarget(fs);
	fs.addDir(QLatin1String(STAGING_ROOT));
	fs.addFile(QLatin1String(STAGING_ROOT) + QLatin1String("/other-run/pkg.zip"),
		QByteArray("in flight"));
	TransactionJournal j(&fs, QLatin1String(TXDIR));
	Transaction tx(&fs, &j);

	QVERIFY(tx.begin(seed(), QLatin1String("tx-1")));
	QVERIFY(tx.backup(QLatin1String(BACKUP)));

	QVERIFY2(tx.rollBack(QLatin1String("package rejected")), qPrintable(tx.lastError()));
	QCOMPARE(tx.state(), TxRolledBack);
	QVERIFY(fs.hasFile(QLatin1String(STAGING_ROOT) + QLatin1String("/other-run/pkg.zip")));
	QVERIFY2(tx.record().error.contains(QLatin1String("staging root could not be removed")),
		qPrintable(tx.record().error));
}

void TestTransaction::aRecordFromBeforeTheRootFieldSkipsReclaim()
{
	/* Records written before the field existed have no stagingRoot, and their
	   reclamation behaved like this already: nothing to name, nothing to
	   remove. The old record must neither crash nor invent a root. */
	FakeFileSystem fs;
	installTarget(fs);
	fs.addDir(QLatin1String(STAGING_ROOT));
	TransactionJournal j(&fs, QLatin1String(TXDIR));

	TransactionRecord old = seed();
	old.stagingRoot.clear();
	old.id = QLatin1String("tx-old");
	old.state = TxBackedUp;
	old.ops << TransactionOp(TransactionOp::OpMoved, QLatin1String(TARGET), QLatin1String(BACKUP));
	QVERIFY(j.prepare());
	QVERIFY(j.write(old));

	Transaction tx(&fs, &j);
	QVERIFY(tx.load(QLatin1String("tx-old")));
	QVERIFY2(tx.rollBack(QLatin1String("recovered old-format record")), qPrintable(tx.lastError()));
	QCOMPARE(tx.state(), TxRolledBack);
	QCOMPARE(fs.callCount(QLatin1String("removeEmptyDir")), 0);
	/* and the directory on disk was nobody's business to touch */
	QVERIFY(fs.hasDir(QLatin1String(STAGING_ROOT)));
}

void TestTransaction::aRecordFromBeforeTheRootFieldRoundTrips()
{
	/* The field is optional in the format, in both directions: a new record
	   carries it, an old one reads back without it, and neither direction
	   changes the journal version. */
	TransactionRecord r = seed();
	r.id = QLatin1String("tx-1");
	r.state = TxBackedUp;
	bool ok = false;
	const TransactionRecord back = TransactionRecord::fromJson(r.toJson(), &ok);
	QVERIFY(ok);
	QCOMPARE(back.stagingRoot, QString(QLatin1String(STAGING_ROOT)));

	/* A record without the field parses to an empty one, not to garbage. The
	   object is hand-stripped: like a journal from before the field, rather
	   than one with the field removed after the fact. */
	TransactionRecord old = seed();
	old.id = QLatin1String("tx-old");
	old.state = TxPrepared;
	const QJsonDocument doc = QJsonDocument::fromJson(old.toJson());
	QJsonObject obj = doc.object();
	QVERIFY(obj.contains(QLatin1String("journalVersion")));
	obj.remove(QLatin1String("stagingRoot"));
	const TransactionRecord read = TransactionRecord::fromJson(
		QJsonDocument(obj).toJson(), &ok);
	QVERIFY(ok);
	QVERIFY2(read.stagingRoot.isEmpty(), "an absent field was invented on read");
}

QTEST_GUILESS_MAIN(TestTransaction)
#include "tst_transaction.moc"

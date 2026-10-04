/*
 * Covers the transaction journal of #18.
 *
 * The property that matters: after an interruption at any point, whatever is
 * on disk must be unambiguously readable -- either the previous record or the
 * new one, never a mixture, and never something that reads as "nothing
 * happened" when something did.
 */
#include <QtTest>
#include "../TransactionJournal.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	const char* DIGEST = "c9aeff95175eed76d47a8dad7d20d531fd1f318ccd202220841813a55e21e4fe";

	TransactionRecord seed(const QString &id = QLatin1String("tx-1"))
	{
		TransactionRecord r;
		r.id = id;
		r.targetDir = QLatin1String("/opt/pe-bear");
		r.stagingDir = QLatin1String("/var/stage/tx-1");
		r.packagePath = QLatin1String("/var/dl/pkg.tar.xz");
		r.packageSize = 1024;
		r.packageSha256 = QLatin1String(DIGEST);
		r.fromVersion = QLatin1String("0.7.2");
		r.toVersion = QLatin1String("0.7.3");
		return r;
	}

}; // namespace

class TestTransactionJournal : public QObject
{
	Q_OBJECT

private slots:
	void writesAndReadsBackARecord();
	void stampsTimestamps();
	void rejectsAnIncompleteRecord();
	void rejectsAnIdThatCouldEscapeTheDirectory_data();
	void rejectsAnIdThatCouldEscapeTheDirectory();
	void roundTripsOperations();
	void roundTripsEveryState_data();
	void roundTripsEveryState();
	void reportsAnUnparseableRecordRatherThanGuessing();
	void refusesARecordFromAnotherFormatVersion();
	void refusesARecordWithAnUnreadableStep();
	void anUnknownStateIsNotMistakenForPrepared();
	void listsAndFindsUnfinishedRecords();
	void reportsUnreadableRecordsSeparately();
	void removesARecord();
	void reportsFailureWhenTheJournalCannotBeWritten();
};

void TestTransactionJournal::writesAndReadsBackARecord()
{
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	QVERIFY(j.prepare());

	TransactionRecord r = seed();
	QVERIFY2(j.write(r), qPrintable(j.lastError()));

	bool ok = false;
	const TransactionRecord back = j.read(QLatin1String("tx-1"), &ok);
	QVERIFY2(ok, qPrintable(j.lastError()));
	QCOMPARE(back.id, QString("tx-1"));
	QCOMPARE(back.targetDir, QString("/opt/pe-bear"));
	QCOMPARE(back.packageSha256, QString(DIGEST));
	QCOMPARE(back.packageSize, Q_INT64_C(1024));
	QCOMPARE(back.toVersion, QString("0.7.3"));
	QCOMPARE(back.state, TxPrepared);
}

void TestTransactionJournal::stampsTimestamps()
{
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	QVERIFY(j.prepare());

	TransactionRecord r = seed();
	QVERIFY(r.createdAt.isEmpty());
	QVERIFY(j.write(r));
	QVERIFY(!r.createdAt.isEmpty());
	QVERIFY(!r.updatedAt.isEmpty());

	const QString firstCreated = r.createdAt;
	QVERIFY(j.write(r));
	/* createdAt is the start of the attempt and must not move */
	QCOMPARE(r.createdAt, firstCreated);
}

void TestTransactionJournal::rejectsAnIncompleteRecord()
{
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	QVERIFY(j.prepare());

	TransactionRecord noDigest = seed();
	noDigest.packageSha256.clear();
	QVERIFY2(!j.write(noDigest), "a record with no digest was accepted");

	TransactionRecord noTarget = seed();
	noTarget.targetDir.clear();
	QVERIFY(!j.write(noTarget));

	TransactionRecord noSize = seed();
	noSize.packageSize = 0;
	QVERIFY(!j.write(noSize));
}

void TestTransactionJournal::rejectsAnIdThatCouldEscapeTheDirectory_data()
{
	QTest::addColumn<QString>("id");
	QTest::newRow("traversal") << "../escape";
	QTest::newRow("nested traversal") << "a/../../b";
	QTest::newRow("forward slash") << "sub/tx";
	QTest::newRow("backslash") << "sub\\tx";
	QTest::newRow("empty") << "";
}

void TestTransactionJournal::rejectsAnIdThatCouldEscapeTheDirectory()
{
	QFETCH(QString, id);
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	QVERIFY(j.prepare());

	/* Refused outright rather than sanitised: a quietly rewritten id would
	   make the record unfindable when recovery needs it. */
	QVERIFY(j.pathFor(id).isEmpty());

	TransactionRecord r = seed(id);
	QVERIFY2(!j.write(r), qPrintable(QString("accepted id: ") + id));
}

void TestTransactionJournal::roundTripsOperations()
{
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	QVERIFY(j.prepare());

	TransactionRecord r = seed();
	r.ops << TransactionOp(TransactionOp::OpMoved, QLatin1String("/opt/pe-bear"), QLatin1String("/var/backup/1"))
	      << TransactionOp(TransactionOp::OpCreatedDir, QLatin1String("/opt/pe-bear"))
	      << TransactionOp(TransactionOp::OpCreated, QLatin1String("/opt/pe-bear/PE-bear"));
	QVERIFY(j.write(r));

	bool ok = false;
	const TransactionRecord back = j.read(QLatin1String("tx-1"), &ok);
	QVERIFY(ok);
	QCOMPARE(back.ops.size(), 3);
	/* order is load-bearing: rollback replays it backwards */
	QCOMPARE(back.ops.at(0).kind, TransactionOp::OpMoved);
	QCOMPARE(back.ops.at(0).to, QString("/var/backup/1"));
	QCOMPARE(back.ops.at(1).kind, TransactionOp::OpCreatedDir);
	QCOMPARE(back.ops.at(2).from, QString("/opt/pe-bear/PE-bear"));
}

void TestTransactionJournal::roundTripsEveryState_data()
{
	QTest::addColumn<int>("state");
	QTest::newRow("Prepared") << int(TxPrepared);
	QTest::newRow("BackedUp") << int(TxBackedUp);
	QTest::newRow("Activated") << int(TxActivated);
	QTest::newRow("Validated") << int(TxValidated);
	QTest::newRow("Committed") << int(TxCommitted);
	QTest::newRow("RolledBack") << int(TxRolledBack);
	QTest::newRow("Failed") << int(TxFailed);
}

void TestTransactionJournal::roundTripsEveryState()
{
	QFETCH(int, state);
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	QVERIFY(j.prepare());

	TransactionRecord r = seed();
	r.state = TransactionState(state);
	QVERIFY(j.write(r));

	bool ok = false;
	const TransactionRecord back = j.read(QLatin1String("tx-1"), &ok);
	QVERIFY2(ok, qPrintable(QString("state did not survive: ")
		+ transactionStateToString(TransactionState(state))));
	QCOMPARE(int(back.state), state);
}

void TestTransactionJournal::reportsAnUnparseableRecordRatherThanGuessing()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/tx"));
	fs.addFile(QLatin1String("/var/tx/broken.json"), QByteArray("{ this is not json"));

	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	bool ok = false;
	j.read(QLatin1String("broken"), &ok);
	QVERIFY2(!ok, "garbage parsed as a record");
	QVERIFY(!j.lastError().isEmpty());
}

void TestTransactionJournal::refusesARecordFromAnotherFormatVersion()
{
	/* A future build may change the format. Acting on a record we do not
	   understand is worse than reporting it. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/tx"));
	fs.addFile(QLatin1String("/var/tx/future.json"), QByteArray(
		"{\"journalVersion\":99,\"id\":\"future\",\"state\":\"Activated\","
		"\"targetDir\":\"/opt/pe-bear\",\"packagePath\":\"/p\",\"packageSize\":1,"
		"\"packageSha256\":\"c9aeff95175eed76d47a8dad7d20d531fd1f318ccd202220841813a55e21e4fe\"}"));

	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	bool ok = false;
	j.read(QLatin1String("future"), &ok);
	QVERIFY2(!ok, "a record from an unknown format version was accepted");
}

void TestTransactionJournal::refusesARecordWithAnUnreadableStep()
{
	/* A partial undo sequence is worse than none: if one step cannot be read,
	   the record as a whole is untrustworthy. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/tx"));
	fs.addFile(QLatin1String("/var/tx/partial.json"), QByteArray(
		"{\"journalVersion\":1,\"id\":\"partial\",\"state\":\"Activated\","
		"\"targetDir\":\"/opt/pe-bear\",\"packagePath\":\"/p\",\"packageSize\":1,"
		"\"packageSha256\":\"c9aeff95175eed76d47a8dad7d20d531fd1f318ccd202220841813a55e21e4fe\","
		"\"ops\":[{\"kind\":\"Moved\",\"from\":\"/a\",\"to\":\"/b\"},"
		"{\"kind\":\"Moved\",\"from\":\"/c\"}]}"));

	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	bool ok = false;
	j.read(QLatin1String("partial"), &ok);
	QVERIFY2(!ok, "a record with an un-undoable step was accepted");
}

void TestTransactionJournal::anUnknownStateIsNotMistakenForPrepared()
{
	/* Prepared means "nothing has been touched". Reading an unknown state as
	   Prepared would skip a rollback that is actually needed -- the worst
	   possible misreading of this file. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/tx"));
	fs.addFile(QLatin1String("/var/tx/weird.json"), QByteArray(
		"{\"journalVersion\":1,\"id\":\"weird\",\"state\":\"Sideways\","
		"\"targetDir\":\"/opt/pe-bear\",\"packagePath\":\"/p\",\"packageSize\":1,"
		"\"packageSha256\":\"c9aeff95175eed76d47a8dad7d20d531fd1f318ccd202220841813a55e21e4fe\"}"));

	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	bool ok = false;
	const TransactionRecord r = j.read(QLatin1String("weird"), &ok);
	QVERIFY2(!ok, "an unknown state was reported as readable");
	QVERIFY2(r.state != TxPrepared, "an unknown state decoded as Prepared");
}

void TestTransactionJournal::listsAndFindsUnfinishedRecords()
{
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	QVERIFY(j.prepare());

	TransactionRecord a = seed(QLatin1String("tx-a"));
	a.state = TxActivated;
	QVERIFY(j.write(a));

	TransactionRecord b = seed(QLatin1String("tx-b"));
	b.state = TxCommitted;
	QVERIFY(j.write(b));

	TransactionRecord c = seed(QLatin1String("tx-c"));
	c.state = TxBackedUp;
	QVERIFY(j.write(c));

	QCOMPARE(j.listIds().size(), 3);

	const QList<TransactionRecord> unfinished = j.findUnfinished();
	QCOMPARE(unfinished.size(), 2);
	QStringList ids;
	for (int i = 0; i < unfinished.size(); i++) ids << unfinished.at(i).id;
	ids.sort();
	QCOMPARE(ids, QStringList() << "tx-a" << "tx-c");
}

void TestTransactionJournal::reportsUnreadableRecordsSeparately()
{
	/* Silence here would be indistinguishable from "nothing to recover", and
	   an unreadable journal beside a half-replaced installation is exactly the
	   case that needs a person. */
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	QVERIFY(j.prepare());

	TransactionRecord good = seed(QLatin1String("tx-good"));
	good.state = TxActivated;
	QVERIFY(j.write(good));
	fs.addFile(QLatin1String("/var/tx/tx-bad.json"), QByteArray("{]"));

	QCOMPARE(j.findUnfinished().size(), 1);
	QCOMPARE(j.findUnreadable(), QStringList() << "tx-bad");
}

void TestTransactionJournal::removesARecord()
{
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	QVERIFY(j.prepare());

	TransactionRecord r = seed();
	QVERIFY(j.write(r));
	QCOMPARE(j.listIds().size(), 1);

	QVERIFY(j.remove(QLatin1String("tx-1")));
	QCOMPARE(j.listIds().size(), 0);
	/* removing something already gone is not an error */
	QVERIFY(j.remove(QLatin1String("tx-1")));
}

void TestTransactionJournal::reportsFailureWhenTheJournalCannotBeWritten()
{
	FakeFileSystem fs;
	TransactionJournal j(&fs, QLatin1String("/var/tx"));
	QVERIFY(j.prepare());

	fs.failAlways(QLatin1String("writeFile"));
	TransactionRecord r = seed();
	QVERIFY2(!j.write(r), "a failed write reported success");
	QVERIFY(!j.lastError().isEmpty());
}

QTEST_GUILESS_MAIN(TestTransactionJournal)
#include "tst_transactionjournal.moc"

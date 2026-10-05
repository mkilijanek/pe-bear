/*
 * Covers the file the helper leaves for the next PE-bear.
 *
 * The exit code is a contract nobody is left to read -- the process that
 * started the helper has closed by the time it decides anything -- so the
 * outcome is written down and consumed once. The tests are about that "once",
 * and about the same JSON leniency the handoff tests guard against: a missing
 * boolean reads as false, and leftUntouched is the field that decides what
 * the user is told about their installation.
 */
#include <QtTest>
#include "../HelperResult.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	const char* ROOT = "/u/.pe-bear/updates";

	HelperResult good()
	{
		HelperResult r;
		r.runId = QString(32, QLatin1Char('a'));
		r.result = QLatin1String("RolledBack");
		r.exitCode = 20;
		r.leftUntouched = false;
		r.message = QLatin1String("The update failed and the previous version was restored.");
		r.detail = QLatin1String("the new build did not answer");
		r.finishedAtUtc = QLatin1String("2026-10-05T12:00:00Z");
		return r;
	}

}; // namespace

class TestHelperResult : public QObject
{
	Q_OBJECT

private slots:
	void roundTripKeepsEveryField();
	void rejectsAMissingLeftUntouched();
	void rejectsStringsWhereNumbersBelong();
	void rejectsAnEmptyMessage();
	void writeThenConsumeReturnsItOnce();
	void absentIsNotAnError();
	void anUnreadableFileIsRemovedAndReported();
	void writeRefusesAnInvalidResult();
};

void TestHelperResult::roundTripKeepsEveryField()
{
	bool ok = false;
	const HelperResult before = good();
	const HelperResult after = HelperResult::fromJson(before.toJson(), &ok);
	QVERIFY(ok);
	QCOMPARE(after.runId, before.runId);
	QCOMPARE(after.result, before.result);
	QCOMPARE(after.exitCode, before.exitCode);
	QCOMPARE(after.leftUntouched, before.leftUntouched);
	QCOMPARE(after.message, before.message);
	QCOMPARE(after.detail, before.detail);
	QCOMPARE(after.finishedAtUtc, before.finishedAtUtc);
}

void TestHelperResult::rejectsAMissingLeftUntouched()
{
	/* Absent would read as false -- "your installation was changed" -- which
	   is a statement about the user's files made by omission. */
	QJsonObject o = QJsonDocument::fromJson(good().toJson()).object();
	o.remove(QLatin1String("leftUntouched"));
	bool ok = true;
	HelperResult::fromJson(QJsonDocument(o).toJson(), &ok);
	QVERIFY(!ok);
}

void TestHelperResult::rejectsStringsWhereNumbersBelong()
{
	QJsonObject o = QJsonDocument::fromJson(good().toJson()).object();
	o[QLatin1String("exitCode")] = QLatin1String("20");
	bool ok = true;
	HelperResult::fromJson(QJsonDocument(o).toJson(), &ok);
	QVERIFY(!ok);
}

void TestHelperResult::rejectsAnEmptyMessage()
{
	HelperResult r = good();
	r.message.clear();
	QVERIFY(!r.isValid());
}

void TestHelperResult::writeThenConsumeReturnsItOnce()
{
	FakeFileSystem fs;
	fs.setWritableDefault(true);
	fs.addDir(QLatin1String(ROOT));
	const QString root = QLatin1String(ROOT);
	const UpdatePaths paths(root, root);

	QVERIFY(HelperResult::write(fs, paths, good()));
	QVERIFY(fs.hasFile(HelperResult::pathIn(paths)));

	bool ok = false, unreadable = true;
	const HelperResult r = HelperResult::consume(fs, paths, &ok, &unreadable);
	QVERIFY(ok);
	QVERIFY(!unreadable);
	QCOMPARE(r.result, QLatin1String("RolledBack"));
	QCOMPARE(r.exitCode, 20);

	/* Consumed: gone from disk, and a second look finds nothing. Shown once
	   is information; shown at every start is nagging. */
	QVERIFY(!fs.hasFile(HelperResult::pathIn(paths)));
	bool again = true;
	HelperResult::consume(fs, paths, &again, NULL);
	QVERIFY(!again);
}

void TestHelperResult::absentIsNotAnError()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String(ROOT));
	const QString root = QLatin1String(ROOT);
	const UpdatePaths paths(root, root);
	bool ok = true, unreadable = true;
	HelperResult::consume(fs, paths, &ok, &unreadable);
	QVERIFY(!ok);
	QVERIFY(!unreadable);
}

void TestHelperResult::anUnreadableFileIsRemovedAndReported()
{
	/* A file this process cannot parse must not get another chance to be
	   unparseable at every start. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String(ROOT));
	const QString root = QLatin1String(ROOT);
	const UpdatePaths paths(root, root);
	fs.addFile(HelperResult::pathIn(paths), QByteArray("{ not a result"));

	bool ok = true, unreadable = false;
	HelperResult::consume(fs, paths, &ok, &unreadable);
	QVERIFY(!ok);
	QVERIFY(unreadable);
	QVERIFY(!fs.hasFile(HelperResult::pathIn(paths)));
}

void TestHelperResult::writeRefusesAnInvalidResult()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String(ROOT));
	const QString root = QLatin1String(ROOT);
	const UpdatePaths paths(root, root);
	HelperResult r = good();
	r.runId.clear();
	QVERIFY(!HelperResult::write(fs, paths, r));
	QVERIFY(!fs.hasFile(HelperResult::pathIn(paths)));
}

QTEST_GUILESS_MAIN(TestHelperResult)
#include "tst_helperresult.moc"

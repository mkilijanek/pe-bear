/*
 * Covers the extraction rules of #19 (SEC-02, SEC-04..SEC-07).
 *
 * ExtractionPolicy is a pure function of an entry list, so every rule can be
 * driven directly with no archive, no decoder and no filesystem. That is the
 * point: these are security rules, and a rule that is awkward to test is a
 * rule that goes untested.
 *
 * One caveat is recorded in the issue and repeated here: the case-collision
 * rule is checked on every host, because two entries differing only in case
 * extract to one file on Windows. A Linux-only run must still reject such an
 * archive, or the test would pass for the wrong reason.
 */
#include <QtTest>
#include "../ExtractionPolicy.h"

using namespace pe_bear::updater;

namespace {

	ArchiveEntry file(const QString &path, qint64 uncompressed = 100, qint64 compressed = 100)
	{
		return ArchiveEntry(path, ArchiveEntry::KindFile, uncompressed, compressed);
	}

	ArchiveEntry dir(const QString &path)
	{
		return ArchiveEntry(path, ArchiveEntry::KindDir);
	}

	/* A plausible Windows portable package, as a baseline that must pass. */
	QList<ArchiveEntry> goodPackage()
	{
		QList<ArchiveEntry> e;
		e << dir(QLatin1String("PE-bear"))
		  << file(QLatin1String("PE-bear/PE-bear.exe"), 12000000, 4000000)
		  << file(QLatin1String("PE-bear/Qt6Core.dll"), 6000000, 2000000)
		  << file(QLatin1String("PE-bear/SIG.txt"), 40000, 8000)
		  << dir(QLatin1String("PE-bear/platforms"))
		  << file(QLatin1String("PE-bear/platforms/qwindows.dll"), 1500000, 500000);
		return e;
	}

}; // namespace

class TestExtractionPolicy : public QObject
{
	Q_OBJECT

private slots:
	void acceptsAPlausiblePackage();

	void rejectsByPathShape_data();
	void rejectsByPathShape();

	void rejectsByEntryKind_data();
	void rejectsByEntryKind();

	void normalisationIsAppliedToAcceptedPaths();
	void acceptsTheLeadingDotSlashThatTarProduces();
	void normalisePathRefusesWhatCannotBeMadeSafe_data();
	void normalisePathRefusesWhatCannotBeMadeSafe();

	void rejectsDuplicateEntries();
	void rejectsCaseCollisionsOnEveryHost();
	void caseCollisionIsDetectedAcrossDirectorySeparators();

	void rejectsAnOversizedEntry();
	void rejectsAnOversizedTotal();
	void rejectsTooManyEntries();
	void rejectsADecompressionBomb();
	void toleratesOddRatiosOnTinyEntries();

	void reservedWindowsNames_data();
	void reservedWindowsNames();

	void everyRejectionHasAMessageNamingTheEntry();
	void theFirstOffenceIsReportedNotTheLast();
	void anEmptyArchiveIsAccepted();
};

void TestExtractionPolicy::acceptsAPlausiblePackage()
{
	ExtractionPolicy policy;
	const ExtractionPolicy::Verdict v = policy.check(goodPackage());
	QVERIFY2(v.ok, qPrintable(v.message()));
	QCOMPARE(v.accepted.size(), 6);
	QCOMPARE(v.totalBytes, Q_INT64_C(12000000) + 6000000 + 40000 + 1500000);
}

void TestExtractionPolicy::rejectsByPathShape_data()
{
	QTest::addColumn<QString>("path");
	QTest::addColumn<int>("rejection");

	QTest::newRow("empty") << "" << int(ExtractionPolicy::EmptyPath);
	QTest::newRow("whitespace only") << "   " << int(ExtractionPolicy::EmptyPath);

	QTest::newRow("posix absolute") << "/etc/cron.d/x" << int(ExtractionPolicy::AbsolutePath);
	QTest::newRow("drive qualified") << "C:\\Windows\\System32\\x.dll" << int(ExtractionPolicy::DriveQualifiedPath);
	QTest::newRow("drive qualified forward") << "C:/Windows/x.dll" << int(ExtractionPolicy::DriveQualifiedPath);
	QTest::newRow("lowercase drive") << "d:/x" << int(ExtractionPolicy::DriveQualifiedPath);
	QTest::newRow("unc backslash") << "\\\\server\\share\\x" << int(ExtractionPolicy::UncPath);
	QTest::newRow("unc forward") << "//server/share/x" << int(ExtractionPolicy::UncPath);

	QTest::newRow("traversal") << "../x" << int(ExtractionPolicy::PathTraversal);
	QTest::newRow("traversal nested") << "PE-bear/../../x" << int(ExtractionPolicy::PathTraversal);
	/* Backslash separators must be folded before the check, or this escapes. */
	QTest::newRow("traversal backslash") << "..\\x" << int(ExtractionPolicy::PathTraversal);
	QTest::newRow("traversal mid-path") << "a/../b" << int(ExtractionPolicy::PathTraversal);
	QTest::newRow("traversal cancelling out") << "a/../a/x" << int(ExtractionPolicy::PathTraversal);

	QTest::newRow("trailing dot") << "PE-bear/x." << int(ExtractionPolicy::TrailingDotOrSpace);
	QTest::newRow("trailing space") << "PE-bear/x " << int(ExtractionPolicy::TrailingDotOrSpace);
	QTest::newRow("trailing dot on dir") << "PE-bear./x" << int(ExtractionPolicy::TrailingDotOrSpace);

	QTest::newRow("control char") << QString("PE-bear/x%1y").arg(QChar(0x01)) << int(ExtractionPolicy::ControlCharacterInPath);
	QTest::newRow("embedded NUL") << QString("PE-bear/x%1y").arg(QChar(0x00)) << int(ExtractionPolicy::ControlCharacterInPath);
	QTest::newRow("delete char") << QString("PE-bear/x%1").arg(QChar(0x7F)) << int(ExtractionPolicy::ControlCharacterInPath);
}

void TestExtractionPolicy::rejectsByPathShape()
{
	QFETCH(QString, path);
	QFETCH(int, rejection);

	ExtractionPolicy policy;
	const ExtractionPolicy::Rejection r = policy.checkEntry(file(path));
	QCOMPARE(int(r), rejection);

	/* and the whole-archive path reports it too, naming the entry */
	QList<ArchiveEntry> e;
	e << file(path);
	const ExtractionPolicy::Verdict v = policy.check(e);
	QVERIFY(!v.ok);
	QCOMPARE(int(v.rejection), rejection);
}

void TestExtractionPolicy::rejectsByEntryKind_data()
{
	QTest::addColumn<int>("kind");
	QTest::addColumn<int>("rejection");

	QTest::newRow("file") << int(ArchiveEntry::KindFile) << int(ExtractionPolicy::NotRejected);
	QTest::newRow("dir") << int(ArchiveEntry::KindDir) << int(ExtractionPolicy::NotRejected);
	QTest::newRow("symlink") << int(ArchiveEntry::KindSymlink) << int(ExtractionPolicy::LinkEntry);
	QTest::newRow("hardlink") << int(ArchiveEntry::KindHardlink) << int(ExtractionPolicy::LinkEntry);
	QTest::newRow("device or fifo") << int(ArchiveEntry::KindOther) << int(ExtractionPolicy::UnsupportedEntryKind);
}

void TestExtractionPolicy::rejectsByEntryKind()
{
	QFETCH(int, kind);
	QFETCH(int, rejection);

	ExtractionPolicy policy;
	ArchiveEntry e(QLatin1String("PE-bear/x"), ArchiveEntry::Kind(kind), 10, 10);
	e.linkTarget = QLatin1String("PE-bear/y");
	QCOMPARE(int(policy.checkEntry(e)), rejection);
}

void TestExtractionPolicy::normalisationIsAppliedToAcceptedPaths()
{
	ExtractionPolicy policy;
	QList<ArchiveEntry> e;
	e << file(QLatin1String("PE-bear\\sub\\x.dll"))
	  << file(QLatin1String("./PE-bear/y.dll"))
	  << file(QLatin1String("PE-bear//z.dll"));

	const ExtractionPolicy::Verdict v = policy.check(e);
	QVERIFY2(v.ok, qPrintable(v.message()));
	QCOMPARE(v.accepted.at(0).path, QString("PE-bear/sub/x.dll"));
	QCOMPARE(v.accepted.at(1).path, QString("PE-bear/y.dll"));
	QCOMPARE(v.accepted.at(2).path, QString("PE-bear/z.dll"));
}

void TestExtractionPolicy::acceptsTheLeadingDotSlashThatTarProduces()
{
	/* Regression. A "." component used to be caught by the trailing-dot rule,
	   which would have rejected ordinary Linux packages: GNU tar routinely
	   stores paths as "./name". Found by normalisationIsAppliedToAcceptedPaths
	   before this rule ever met a real archive. */
	ExtractionPolicy policy;
	QCOMPARE(policy.checkEntry(file(QLatin1String("./PE-bear/x.dll"))),
		ExtractionPolicy::NotRejected);
	QCOMPARE(policy.checkEntry(file(QLatin1String("./x"))),
		ExtractionPolicy::NotRejected);
	QCOMPARE(policy.checkEntry(dir(QLatin1String("./PE-bear"))),
		ExtractionPolicy::NotRejected);

	/* but a genuine trailing dot is still refused */
	QCOMPARE(policy.checkEntry(file(QLatin1String("./PE-bear/x."))),
		ExtractionPolicy::TrailingDotOrSpace);
	/* and ".." is still traversal, not a skippable component */
	QCOMPARE(policy.checkEntry(file(QLatin1String("./../x"))),
		ExtractionPolicy::PathTraversal);
}

void TestExtractionPolicy::normalisePathRefusesWhatCannotBeMadeSafe_data()
{
	QTest::addColumn<QString>("path");
	QTest::newRow("traversal") << "../x";
	QTest::newRow("traversal deep") << "a/../../x";
	QTest::newRow("only dots") << "./.";
	QTest::newRow("only slashes") << "///";
	QTest::newRow("empty") << "";
}

void TestExtractionPolicy::normalisePathRefusesWhatCannotBeMadeSafe()
{
	QFETCH(QString, path);
	/* Empty is the refusal signal; a caller treating it as a name would write
	   into the destination root. */
	QVERIFY2(ExtractionPolicy::normalisePath(path).isEmpty(),
		qPrintable(QString("normalised to: ") + ExtractionPolicy::normalisePath(path)));
}

void TestExtractionPolicy::rejectsDuplicateEntries()
{
	ExtractionPolicy policy;
	QList<ArchiveEntry> e;
	e << file(QLatin1String("PE-bear/x.dll"))
	  << file(QLatin1String("PE-bear/x.dll"));

	const ExtractionPolicy::Verdict v = policy.check(e);
	QVERIFY(!v.ok);
	QCOMPARE(v.rejection, ExtractionPolicy::DuplicateEntry);

	/* also caught when the duplicate only appears after normalisation */
	QList<ArchiveEntry> e2;
	e2 << file(QLatin1String("PE-bear/x.dll"))
	   << file(QLatin1String("PE-bear\\x.dll"));
	QCOMPARE(policy.check(e2).rejection, ExtractionPolicy::DuplicateEntry);
}

void TestExtractionPolicy::rejectsCaseCollisionsOnEveryHost()
{
	/* The rule exists because Windows would extract both of these to one
	   file. This test must fail on Linux too, where the filesystem would
	   happily keep them apart -- otherwise it passes for the wrong reason and
	   proves nothing about the platform that matters. */
	ExtractionPolicy policy;
	QList<ArchiveEntry> e;
	e << file(QLatin1String("PE-bear/Qt6Core.dll"))
	  << file(QLatin1String("PE-bear/qt6core.dll"));

	const ExtractionPolicy::Verdict v = policy.check(e);
	QVERIFY2(!v.ok, "a case collision was accepted");
	QCOMPARE(v.rejection, ExtractionPolicy::CaseCollision);
	QCOMPARE(v.offendingPath, QString("PE-bear/qt6core.dll"));
}

void TestExtractionPolicy::caseCollisionIsDetectedAcrossDirectorySeparators()
{
	ExtractionPolicy policy;
	QList<ArchiveEntry> e;
	e << file(QLatin1String("PE-bear/Sub/x.dll"))
	  << file(QLatin1String("pe-bear/sub/X.dll"));
	QCOMPARE(policy.check(e).rejection, ExtractionPolicy::CaseCollision);
}

void TestExtractionPolicy::rejectsAnOversizedEntry()
{
	ExtractionPolicy::Limits limits;
	limits.maxEntryBytes = 1000;
	ExtractionPolicy policy(limits);

	QCOMPARE(policy.checkEntry(file(QLatin1String("PE-bear/x"), 1001, 1001)),
		ExtractionPolicy::EntryTooLarge);
	QCOMPARE(policy.checkEntry(file(QLatin1String("PE-bear/x"), 1000, 1000)),
		ExtractionPolicy::NotRejected);
	/* a negative size is a malformed header, not a small file */
	QCOMPARE(policy.checkEntry(file(QLatin1String("PE-bear/x"), -1, 10)),
		ExtractionPolicy::EntryTooLarge);
}

void TestExtractionPolicy::rejectsAnOversizedTotal()
{
	ExtractionPolicy::Limits limits;
	limits.maxTotalBytes = 1000;
	limits.maxEntryBytes = 10000;
	ExtractionPolicy policy(limits);

	QList<ArchiveEntry> e;
	e << file(QLatin1String("PE-bear/a"), 600, 600)
	  << file(QLatin1String("PE-bear/b"), 600, 600);

	const ExtractionPolicy::Verdict v = policy.check(e);
	QVERIFY(!v.ok);
	QCOMPARE(v.rejection, ExtractionPolicy::TotalTooLarge);
	QCOMPARE(v.offendingPath, QString("PE-bear/b"));
}

void TestExtractionPolicy::rejectsTooManyEntries()
{
	ExtractionPolicy::Limits limits;
	limits.maxEntries = 3;
	ExtractionPolicy policy(limits);

	QList<ArchiveEntry> e;
	for (int i = 0; i < 4; i++) e << file(QString("PE-bear/f%1").arg(i));
	QCOMPARE(policy.check(e).rejection, ExtractionPolicy::TooManyEntries);
}

void TestExtractionPolicy::rejectsADecompressionBomb()
{
	ExtractionPolicy policy; /* default ratio limit 200 */
	/* 1 KiB compressed expanding to 100 MiB */
	QCOMPARE(policy.checkEntry(file(QLatin1String("PE-bear/bomb"), 100 * 1024 * 1024, 1024)),
		ExtractionPolicy::CompressionRatioTooHigh);
}

void TestExtractionPolicy::toleratesOddRatiosOnTinyEntries()
{
	/* Below the meaningfulness threshold the ratio says nothing useful: a
	   40-byte entry stored in 1 byte is not an attack. Rejecting these would
	   make the rule fire on ordinary packages. */
	ExtractionPolicy policy;
	QCOMPARE(policy.checkEntry(file(QLatin1String("PE-bear/tiny"), 40000, 100)),
		ExtractionPolicy::NotRejected);
}

void TestExtractionPolicy::reservedWindowsNames_data()
{
	QTest::addColumn<QString>("path");
	QTest::addColumn<bool>("rejected");

	QTest::newRow("CON") << "PE-bear/CON" << true;
	QTest::newRow("con lowercase") << "PE-bear/con" << true;
	QTest::newRow("NUL with extension") << "PE-bear/nul.txt" << true;
	QTest::newRow("COM1") << "PE-bear/COM1" << true;
	QTest::newRow("LPT9 in a directory") << "PE-bear/LPT9/x" << true;
	QTest::newRow("AUX") << "aux.dll" << true;
	/* names that merely start with a reserved stem are fine */
	QTest::newRow("CONSOLE") << "PE-bear/console.dll" << false;
	QTest::newRow("COM10") << "PE-bear/COM10" << false;
	QTest::newRow("NULL") << "PE-bear/null.dll" << false;
}

void TestExtractionPolicy::reservedWindowsNames()
{
	QFETCH(QString, path);
	QFETCH(bool, rejected);

	ExtractionPolicy policy;
	const ExtractionPolicy::Rejection r = policy.checkEntry(file(path));
	if (rejected) {
		QCOMPARE(r, ExtractionPolicy::ReservedWindowsName);
	} else {
		QCOMPARE(r, ExtractionPolicy::NotRejected);
	}
}

void TestExtractionPolicy::everyRejectionHasAMessageNamingTheEntry()
{
	/* A refusal the user cannot act on is barely better than a crash, and an
	   unnamed enum value in a dialog is exactly that. */
	for (int i = ExtractionPolicy::EmptyPath; i < ExtractionPolicy::REJECTIONS_COUNT; i++) {
		const ExtractionPolicy::Rejection r = ExtractionPolicy::Rejection(i);
		const QString name = ExtractionPolicy::rejectionToString(r);
		QVERIFY2(name != QLatin1String("Invalid"), qPrintable(QString::number(i)));

		const QString msg = ExtractionPolicy::rejectionMessage(r, QLatin1String("PE-bear/x.dll"));
		QVERIFY2(!msg.isEmpty(), qPrintable(name));
		QVERIFY2(msg != ExtractionPolicy::rejectionMessage(ExtractionPolicy::NotRejected, QString()),
			qPrintable(name));
	}
	/* NotRejected is the one value with deliberately no message */
	QVERIFY(ExtractionPolicy::rejectionMessage(ExtractionPolicy::NotRejected, QString()).isEmpty());
}

void TestExtractionPolicy::theFirstOffenceIsReportedNotTheLast()
{
	/* Reporting the last would describe a consequence rather than the cause. */
	ExtractionPolicy policy;
	QList<ArchiveEntry> e;
	e << file(QLatin1String("PE-bear/ok.dll"))
	  << file(QLatin1String("../escape"))
	  << ArchiveEntry(QLatin1String("PE-bear/link"), ArchiveEntry::KindSymlink);

	const ExtractionPolicy::Verdict v = policy.check(e);
	QCOMPARE(v.rejection, ExtractionPolicy::PathTraversal);
	QCOMPARE(v.offendingPath, QString("../escape"));
}

void TestExtractionPolicy::anEmptyArchiveIsAccepted()
{
	/* Nothing to write is not an error here; whether a package may be empty is
	   the installer's business, not the extractor's. */
	ExtractionPolicy policy;
	const ExtractionPolicy::Verdict v = policy.check(QList<ArchiveEntry>());
	QVERIFY(v.ok);
	QCOMPARE(v.accepted.size(), 0);
	QCOMPARE(v.totalBytes, Q_INT64_C(0));
}

QTEST_GUILESS_MAIN(TestExtractionPolicy)
#include "tst_extractionpolicy.moc"

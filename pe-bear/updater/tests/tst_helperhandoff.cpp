/*
 * Covers the instruction file the helper acts on.
 *
 * Worth its own suite because this is the only input to a program that moves
 * directories about, and because JSON is forgiving in exactly the way that
 * matters here: ask a string for an integer and it answers 0, ask a missing
 * key for a boolean and it answers false. Every field below decides something
 * destructive, so "it parsed" is not the same as "it is usable", and the tests
 * are mostly about the difference.
 */
#include <QtTest>
#include "../HelperHandoff.h"

using namespace pe_bear::updater;

namespace {

	/* QTestData columns need a real QString: a const char* or a QLatin1String
	   is not a registered metatype and fails to compile inside newRow(). */
	QString S(const char *text) { return QString::fromLatin1(text); }

	HelperHandoff good()
	{
		HelperHandoff h;
		h.runId = QString(32, QLatin1Char('a'));
		h.packagePath = QLatin1String("/home/u/.pe-bear/updates/downloads/x/pkg.zip");
		h.packageSha256 = QString(64, QLatin1Char('b'));
		h.packageSize = 1024;
		h.targetDir = QLatin1String("/home/u/pe-bear");
		h.expectedVersion = QLatin1String("0.7.3");
		h.parentPid = 4242;
		h.relaunch = true;
		h.createdAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
		h.assetUrl = QLatin1String("https://example.invalid/pkg.zip");
		h.releaseTag = QLatin1String("v0.7.3");
		h.assetName = QLatin1String("pkg.zip");
		return h;
	}

}; // namespace

class TestHelperHandoff : public QObject
{
	Q_OBJECT

private slots:
	void roundTripKeepsEveryField();
	void acceptsAWellFormedInstruction();

	void rejectsShapeProblems();
	void rejectsShapeProblems_data();

	void rejectsAWrongVersion();
	void rejectsUnparseableJson();
	void rejectsNonObjectJson();
	void rejectsStringsWhereNumbersBelong();
	void rejectsAMissingRelaunchFlag();
	void rejectsAMissingSizeRatherThanReadingItAsZero();

	void ageIsMeasuredFromCreatedAt();
	void ageIsNegativeForAnUnreadableTimestamp();
	void anUnreadableTimestampIsReportedSeparatelyFromItsAge();

	void runIdIsUnpredictableAndWellFormed();
};

void TestHelperHandoff::acceptsAWellFormedInstruction()
{
	QVERIFY(good().isValid());
}

void TestHelperHandoff::roundTripKeepsEveryField()
{
	const HelperHandoff before = good();
	bool ok = false;
	const HelperHandoff after = HelperHandoff::fromJson(before.toJson(), &ok);

	QVERIFY(ok);
	QCOMPARE(after.version, before.version);
	QCOMPARE(after.runId, before.runId);
	QCOMPARE(after.packagePath, before.packagePath);
	QCOMPARE(after.packageSha256, before.packageSha256);
	QCOMPARE(after.packageSize, before.packageSize);
	QCOMPARE(after.targetDir, before.targetDir);
	QCOMPARE(after.expectedVersion, before.expectedVersion);
	QCOMPARE(after.parentPid, before.parentPid);
	QCOMPARE(after.relaunch, before.relaunch);
	QCOMPARE(after.createdAtUtc, before.createdAtUtc);
	QCOMPARE(after.assetUrl, before.assetUrl);
	QCOMPARE(after.releaseTag, before.releaseTag);
	QCOMPARE(after.assetName, before.assetName);
}

void TestHelperHandoff::rejectsShapeProblems_data()
{
	QTest::addColumn<QString>("field");
	QTest::addColumn<QString>("value");

	/* Each row breaks exactly one thing, so a failure names the rule that
	   stopped holding rather than "the document is bad". */
	QTest::newRow("run id too short")      << S("runId")   << QString(31, QLatin1Char('a'));
	QTest::newRow("run id not hex")        << S("runId")   << QString(32, QLatin1Char('g'));
	QTest::newRow("run id upper case")     << S("runId")   << QString(32, QLatin1Char('A'));
	QTest::newRow("digest too short")      << S("sha")     << QString(63, QLatin1Char('b'));
	QTest::newRow("digest not hex")        << S("sha")     << QString(64, QLatin1Char('z'));
	QTest::newRow("digest upper case")     << S("sha")     << QString(64, QLatin1Char('B'));
	QTest::newRow("empty package path")    << S("package") << QString();
	QTest::newRow("relative package path") << S("package") << S("pkg.zip");
	QTest::newRow("empty target")          << S("target")  << QString();
	QTest::newRow("relative target")       << S("target")  << S("pe-bear");
	QTest::newRow("unparseable version")   << S("version") << S("not-a-version");
	QTest::newRow("empty version")         << S("version") << QString();
	QTest::newRow("no timestamp")          << S("created") << QString();
	QTest::newRow("asset url not a url")   << S("url")     << S("   ");
	QTest::newRow("asset url is http")     << S("url")     << S("http://example.invalid/p.zip");
	QTest::newRow("asset url is a file")   << S("url")     << S("file:///etc/passwd");
	QTest::newRow("asset url is empty")    << S("url")     << QString();
}

void TestHelperHandoff::rejectsShapeProblems()
{
	QFETCH(QString, field);
	QFETCH(QString, value);

	HelperHandoff h = good();
	if (field == QLatin1String("runId")) h.runId = value;
	else if (field == QLatin1String("sha")) h.packageSha256 = value;
	else if (field == QLatin1String("package")) h.packagePath = value;
	else if (field == QLatin1String("target")) h.targetDir = value;
	else if (field == QLatin1String("version")) h.expectedVersion = value;
	else if (field == QLatin1String("created")) h.createdAtUtc = value;
	else if (field == QLatin1String("url")) h.assetUrl = value;
	else QFAIL("unknown field in the data row");

	QVERIFY(!h.isValid());

	/* And it must not become valid by going through JSON: a document is
	   judged on the way in, not only when it is built by hand. */
	bool ok = true;
	HelperHandoff::fromJson(h.toJson(), &ok);
	QVERIFY(!ok);
}

void TestHelperHandoff::rejectsAWrongVersion()
{
	HelperHandoff h = good();
	h.version = HelperHandoff::CURRENT_VERSION + 1;
	QVERIFY(!h.isValid());

	h.version = 0;
	QVERIFY(!h.isValid());
}

void TestHelperHandoff::rejectsUnparseableJson()
{
	bool ok = true;
	HelperHandoff::fromJson(QByteArray("{ not json"), &ok);
	QVERIFY(!ok);

	ok = true;
	HelperHandoff::fromJson(QByteArray(), &ok);
	QVERIFY(!ok);
}

void TestHelperHandoff::rejectsNonObjectJson()
{
	bool ok = true;
	HelperHandoff::fromJson(QByteArray("[1, 2, 3]"), &ok);
	QVERIFY(!ok);
}

void TestHelperHandoff::rejectsStringsWhereNumbersBelong()
{
	/* "4242" converts to 0 through QJsonValue::toInt, not to 4242. A parser
	   that took it would be handed a pid of zero and wait for nothing. */
	QJsonObject o = QJsonDocument::fromJson(good().toJson()).object();
	o[QLatin1String("parentPid")] = QLatin1String("4242");

	bool ok = true;
	const HelperHandoff h = HelperHandoff::fromJson(QJsonDocument(o).toJson(), &ok);
	QVERIFY(!ok);
	QCOMPARE(h.parentPid, Q_INT64_C(0));
}

void TestHelperHandoff::rejectsAMissingRelaunchFlag()
{
	/* Absent would read as false, which is a decision about what the user
	   asked for, made by accident. */
	QJsonObject o = QJsonDocument::fromJson(good().toJson()).object();
	o.remove(QLatin1String("relaunch"));

	bool ok = true;
	HelperHandoff::fromJson(QJsonDocument(o).toJson(), &ok);
	QVERIFY(!ok);
}

void TestHelperHandoff::rejectsAMissingSizeRatherThanReadingItAsZero()
{
	QJsonObject o = QJsonDocument::fromJson(good().toJson()).object();
	o.remove(QLatin1String("packageSize"));

	bool ok = true;
	HelperHandoff::fromJson(QJsonDocument(o).toJson(), &ok);
	QVERIFY(!ok);
}

void TestHelperHandoff::ageIsMeasuredFromCreatedAt()
{
	const QDateTime base = QDateTime::fromString(QLatin1String("2026-10-05T12:00:00Z"), Qt::ISODate);
	QVERIFY(base.isValid());

	HelperHandoff h = good();
	h.createdAtUtc = base.toString(Qt::ISODate);

	QCOMPARE(h.ageSeconds(base), Q_INT64_C(0));
	QCOMPARE(h.ageSeconds(base.addSecs(90)), Q_INT64_C(90));
	/* Dated in the future reads as negative, which is what lets the helper
	   tell "slightly skewed clock" from "fabricated timestamp". */
	QCOMPARE(h.ageSeconds(base.addSecs(-30)), Q_INT64_C(-30));
}

void TestHelperHandoff::ageIsNegativeForAnUnreadableTimestamp()
{
	HelperHandoff h = good();
	h.createdAtUtc = QLatin1String("last Tuesday");
	QCOMPARE(h.ageSeconds(QDateTime::currentDateTimeUtc()), Q_INT64_C(-1));
}

void TestHelperHandoff::anUnreadableTimestampIsReportedSeparatelyFromItsAge()
{
	/* The two questions must not share a return value. An age of -1 is an
	   instruction written a second ago by a clock a second ahead, which is
	   ordinary; an unreadable timestamp is a malformed instruction. Folding
	   them together is how the tolerance for clock skew becomes unreachable,
	   which is exactly what happened the first time this was written. */
	HelperHandoff h = good();
	QVERIFY(h.hasUsableTimestamp());

	h.createdAtUtc = QLatin1String("last Tuesday");
	QVERIFY(!h.hasUsableTimestamp());

	h.createdAtUtc = QString();
	QVERIFY(!h.hasUsableTimestamp());

	/* A timestamp one second in the future is readable, and its age is -1.
	   Built from whole seconds on purpose: an ISO-8601 string carries no
	   milliseconds, so comparing it against a "now" that has them makes
	   secsTo truncate towards zero and the answer becomes 0. */
	const QDateTime now = QDateTime::fromString(
		QLatin1String("2026-10-05T12:00:00Z"), Qt::ISODate);
	QVERIFY(now.isValid());
	h.createdAtUtc = now.addSecs(1).toString(Qt::ISODate);
	QVERIFY(h.hasUsableTimestamp());
	QCOMPARE(h.ageSeconds(now), Q_INT64_C(-1));
}

void TestHelperHandoff::runIdIsUnpredictableAndWellFormed()
{
	QSet<QString> seen;
	for (int i = 0; i < 200; i++) {
		const QString id = HelperHandoff::generateRunId();
		QCOMPARE(id.length(), HelperHandoff::RUN_ID_HEX_LENGTH);
		QVERIFY(QRegularExpression(QLatin1String("^[0-9a-f]+$")).match(id).hasMatch());
		seen.insert(id);
	}
	/* Not a randomness test -- it cannot be one. It catches the mistake that
	   actually happens: a generator that returns the same value every time. */
	QCOMPARE(seen.size(), 200);
}

QTEST_MAIN(TestHelperHandoff)
#include "tst_helperhandoff.moc"

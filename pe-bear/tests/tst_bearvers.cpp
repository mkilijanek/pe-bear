/*
 * Covers pe_bear::BearVers -- the pre-existing version model used for the
 * window title and the About box.
 *
 * Note on scope: these tests pin down BearVers as it is meant to behave. One of
 * them fails against the original implementation, which had an inverted
 * condition in the string constructor; see the comment on
 * parsesThreeComponentVersions().
 */
#include <QtTest>
#include "base/BearVers.h"
#include "REbear.h"

using namespace pe_bear;

class TestBearVers : public QObject
{
	Q_OBJECT

private slots:
	void constructsFromComponents();
	void parsesFourComponentVersions();
	void parsesThreeComponentVersions();
	void rejectsTooFewComponents_data();
	void rejectsTooFewComponents();
	void rendersToString_data();
	void rendersToString();
	void appendsDescriptionToString();
	void comparesEquality();
	void comparesOrdering();
	void comparesNumericallyNotLexically();
	void compareReportsRelationToLatest();
	void compareRejectsInvalidOperands();
	void matchesTheVersionMacros();
};

void TestBearVers::constructsFromComponents()
{
	BearVers v(1, 2, 3, 4);
	QVERIFY(v.isValid());
	QCOMPARE(v.toString(), QString("1.2.3.4"));
}

void TestBearVers::parsesFourComponentVersions()
{
	BearVers v("0.7.0.4");
	QVERIFY(v.isValid());
	QCOMPARE(v.toString(), QString("0.7.0.4"));
}

void TestBearVers::parsesThreeComponentVersions()
{
	/* The original code read strings[3] on a three-element list -- the
	   condition selecting between the fourth component and zero was inverted,
	   so the common case indexed one past the end. QList::operator[] only
	   asserts in a debug build, so a release build read whatever was there.
	   This is the case that catches it. */
	BearVers v("0.7.2");
	QVERIFY(v.isValid());
	QCOMPARE(v.toString(), QString("0.7.2"));
}

void TestBearVers::rejectsTooFewComponents_data()
{
	QTest::addColumn<QString>("input");

	QTest::newRow("empty") << "";
	QTest::newRow("one component") << "7";
	QTest::newRow("two components") << "0.7";
	QTest::newRow("only separators") << "..";
}

void TestBearVers::rejectsTooFewComponents()
{
	QFETCH(QString, input);
	BearVers v(input);
	QVERIFY2(!v.isValid(), qPrintable(QString("accepted: '") + input + QLatin1Char('\'')));
	/* an invalid version renders as nothing, so it cannot leak into a caption */
	QCOMPARE(v.toString(), QString());
}

void TestBearVers::rendersToString_data()
{
	QTest::addColumn<QString>("input");
	QTest::addColumn<QString>("expected");

	QTest::newRow("three components") << "0.7.2" << "0.7.2";
	QTest::newRow("zero fourth dropped") << "0.7.2.0" << "0.7.2";
	QTest::newRow("nonzero fourth kept") << "0.7.0.4" << "0.7.0.4";
	QTest::newRow("surrounding space") << "  1.2.3  " << "1.2.3";
	QTest::newRow("double digits") << "0.10.0" << "0.10.0";
}

void TestBearVers::rendersToString()
{
	QFETCH(QString, input);
	QFETCH(QString, expected);
	QCOMPARE(BearVers(input).toString(), expected);
}

void TestBearVers::appendsDescriptionToString()
{
	BearVers v(0, 7, 2, 0, "beta");
	QCOMPARE(v.toString(), QString("0.7.2-beta"));
}

void TestBearVers::comparesEquality()
{
	QVERIFY(BearVers(1, 2, 3, 4) == BearVers(1, 2, 3, 4));
	QVERIFY(BearVers(1, 2, 3, 4) != BearVers(1, 2, 3, 5));
	QVERIFY(BearVers(1, 2, 3, 0) != BearVers(1, 2, 4, 0));

	/* the description is a label, not part of the version identity */
	QVERIFY(BearVers(1, 2, 3, 4, "beta") == BearVers(1, 2, 3, 4));
}

void TestBearVers::comparesOrdering()
{
	QVERIFY(BearVers(1, 2, 3, 4) < BearVers(1, 2, 3, 5));
	QVERIFY(BearVers(1, 2, 3, 5) > BearVers(1, 2, 3, 4));
	QVERIFY(BearVers(0, 9, 0, 0) < BearVers(1, 0, 0, 0));

	/* equal versions are neither less nor greater */
	BearVers a(1, 2, 3, 4);
	BearVers b(1, 2, 3, 4);
	QVERIFY(!(a < b));
	QVERIFY(!(a > b));
}

void TestBearVers::comparesNumericallyNotLexically()
{
	QVERIFY(BearVers("0.10.0") > BearVers("0.9.0"));
	QVERIFY(BearVers("0.7.10") > BearVers("0.7.9"));
	QVERIFY(BearVers("1.0.0") > BearVers("0.99.99"));
}

void TestBearVers::compareReportsRelationToLatest()
{
	BearVers installed("0.7.2");

	BearVers same("0.7.2");
	QCOMPARE(installed.compare(same), BearVers::VER_OK);

	BearVers newer("0.7.3");
	QCOMPARE(installed.compare(newer), BearVers::VER_OLD);

	BearVers older("0.7.1");
	QCOMPARE(installed.compare(older), BearVers::VER_NEW);
}

void TestBearVers::compareRejectsInvalidOperands()
{
	BearVers valid("0.7.2");
	BearVers invalid("nonsense");
	QCOMPARE(valid.compare(invalid), BearVers::VER_INVALID);

	BearVers alsoInvalid("");
	QCOMPARE(alsoInvalid.compare(valid), BearVers::VER_INVALID);
}

void TestBearVers::matchesTheVersionMacros()
{
	/* What the About box and the window caption actually show. */
	BearVers fromMacros(V_MAJOR, V_MINOR, V_PATCH, V_PATCH_SUB, V_DESC);
	BearVers fromString(QLatin1String(REBEAR_VERSION_STR));

	QVERIFY(fromMacros.isValid());
	QVERIFY2(fromString.isValid(), "REBEAR_VERSION_STR does not parse as a BearVers");
	QVERIFY2(fromMacros == fromString,
		qPrintable(QString("macros say %1, REBEAR_VERSION_STR says %2")
			.arg(fromMacros.toString()).arg(fromString.toString())));
}

QTEST_APPLESS_MAIN(TestBearVers)
#include "tst_bearvers.moc"

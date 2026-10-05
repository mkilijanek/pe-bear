/*
 * Covers the updater preferences of PR #4 (UT-10 and the consent defaults).
 *
 * The defaults are the policy: checking on, downloading off, installing never
 * a setting at all.
 */
#include <QtTest>
#include "../UpdateSettings.h"

using namespace pe_bear::updater;

class TestUpdateSettings : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void defaultsMatchTheConsentModel();
	void automaticCheckRunsAtMostOncePerInterval();
	void automaticCheckIsDueWhenNeverRun();
	void automaticCheckIsDueAfterTheClockMovesBackwards();
	void automaticCheckIsNeverDueWhenDisabled();
	void skippingOneVersionDoesNotSilenceLaterOnes();
	void intervalIsClampedToSaneValues();
	void survivesARoundTripThroughQSettings();
	void ignoresACorruptStoredVersion();
	void repositoryIsEmptyByDefaultAndFallsBackToTheBuild();
	void repositoryAcceptsOnlyPlausibleGitHubNames_data();
	void repositoryAcceptsOnlyPlausibleGitHubNames();
	void repositorySurvivesARoundTripAndACorruptValueIsDropped();
};

void TestUpdateSettings::initTestCase()
{
	QCoreApplication::setOrganizationName(QLatin1String("PE-bear-tests"));
	QCoreApplication::setApplicationName(QLatin1String("UpdateSettingsTest"));
	QSettings settings;
	settings.clear();
}

void TestUpdateSettings::defaultsMatchTheConsentModel()
{
	const UpdateSettings s;
	QVERIFY2(s.isAutoCheckEnabled(), "checking should be on by default");
	QVERIFY2(!s.isAutoDownloadEnabled(), "downloading must be opt-in");
	QCOMPARE(s.checkIntervalHours(), int(UpdateSettings::DEFAULT_CHECK_INTERVAL_HOURS));
	QVERIFY(s.skippedVersion().isEmpty());
}

void TestUpdateSettings::automaticCheckRunsAtMostOncePerInterval()
{
	const QDateTime now = QDateTime::currentDateTime();
	UpdateSettings s;
	s.setLastCheck(now.addSecs(-23 * 3600));
	QVERIFY2(!s.isAutomaticCheckDue(now), "checked again after 23 hours");

	s.setLastCheck(now.addSecs(-25 * 3600));
	QVERIFY2(s.isAutomaticCheckDue(now), "did not check after 25 hours");

	s.setLastCheck(now.addSecs(-24 * 3600));
	QVERIFY(s.isAutomaticCheckDue(now));
}

void TestUpdateSettings::automaticCheckIsDueWhenNeverRun()
{
	UpdateSettings s;
	QVERIFY(s.isAutomaticCheckDue(QDateTime::currentDateTime()));
}

void TestUpdateSettings::automaticCheckIsDueAfterTheClockMovesBackwards()
{
	/* A stored timestamp in the future would otherwise block checks forever. */
	const QDateTime now = QDateTime::currentDateTime();
	UpdateSettings s;
	s.setLastCheck(now.addSecs(3600));
	QVERIFY(s.isAutomaticCheckDue(now));
}

void TestUpdateSettings::automaticCheckIsNeverDueWhenDisabled()
{
	UpdateSettings s;
	s.setAutoCheckEnabled(false);
	QVERIFY(!s.isAutomaticCheckDue(QDateTime::currentDateTime()));
}

void TestUpdateSettings::skippingOneVersionDoesNotSilenceLaterOnes()
{
	UpdateSettings s;
	s.setSkippedVersion(Version::fromString(QLatin1String("0.7.3")));

	QVERIFY(s.isVersionSkipped(Version::fromString(QLatin1String("0.7.3"))));
	QVERIFY(s.isVersionSkipped(Version::fromString(QLatin1String("0.7.3.0"))));
	QVERIFY(!s.isVersionSkipped(Version::fromString(QLatin1String("0.7.4"))));
	QVERIFY(!s.isVersionSkipped(Version::fromString(QLatin1String("0.8.0"))));

	s.clearSkippedVersion();
	QVERIFY(!s.isVersionSkipped(Version::fromString(QLatin1String("0.7.3"))));
}

void TestUpdateSettings::intervalIsClampedToSaneValues()
{
	UpdateSettings s;
	s.setCheckIntervalHours(0);
	QVERIFY(s.checkIntervalHours() >= 1);
	s.setCheckIntervalHours(-5);
	QVERIFY(s.checkIntervalHours() >= 1);
	s.setCheckIntervalHours(100000);
	QVERIFY(s.checkIntervalHours() <= 24 * 30);
}

void TestUpdateSettings::survivesARoundTripThroughQSettings()
{
	const QDateTime when = QDateTime::fromString(
		QLatin1String("2026-09-01T10:00:00"), Qt::ISODate);

	UpdateSettings written;
	written.setAutoCheckEnabled(false);
	written.setAutoDownloadEnabled(true);
	written.setCheckIntervalHours(12);
	written.setLastCheck(when);
	written.setSkippedVersion(Version::fromString(QLatin1String("0.9.1")));

	QSettings settings;
	written.write(settings);
	settings.sync();

	UpdateSettings read;
	read.read(settings);

	QCOMPARE(read.isAutoCheckEnabled(), false);
	QCOMPARE(read.isAutoDownloadEnabled(), true);
	QCOMPARE(read.checkIntervalHours(), 12);
	QCOMPARE(read.lastCheck(), when);
	QCOMPARE(read.skippedVersion(), QString("0.9.1"));
}

void TestUpdateSettings::ignoresACorruptStoredVersion()
{
	QSettings settings;
	settings.beginGroup(QLatin1String(UpdateSettings::SETTINGS_GROUP));
	settings.setValue(QLatin1String("SkippedVersion"), QLatin1String("not-a-version"));
	settings.endGroup();
	settings.sync();

	UpdateSettings s;
	s.read(settings);
	QVERIFY2(s.skippedVersion().isEmpty(), "a corrupt stored version was trusted");
}

void TestUpdateSettings::repositoryIsEmptyByDefaultAndFallsBackToTheBuild()
{
	UpdateSettings s;
	QVERIFY(s.repository().isEmpty());
	QCOMPARE(s.effectiveRepository(QLatin1String("hasherezade/pe-bear")), QString("hasherezade/pe-bear"));
	QVERIFY(s.setRepository(QLatin1String("  mkilijanek/pe-bear ")));
	QCOMPARE(s.repository(), QString("mkilijanek/pe-bear"));
	QCOMPARE(s.effectiveRepository(QLatin1String("hasherezade/pe-bear")), QString("mkilijanek/pe-bear"));
	/* Empty clears, and the build's default is back. */
	QVERIFY(s.setRepository(QString()));
	QVERIFY(s.repository().isEmpty());
	QCOMPARE(s.effectiveRepository(QLatin1String("hasherezade/pe-bear")), QString("hasherezade/pe-bear"));
}

void TestUpdateSettings::repositoryAcceptsOnlyPlausibleGitHubNames_data()
{
	QTest::addColumn<QString>("input");
	QTest::addColumn<bool>("accepted");

	QTest::newRow("upstream") << "hasherezade/pe-bear" << true;
	QTest::newRow("fork") << "mkilijanek/pe-bear" << true;
	QTest::newRow("dots and underscores in the name") << "owner/my.repo_v2" << true;
	QTest::newRow("hyphen in the owner") << "my-org/repo" << true;
	QTest::newRow("no slash") << "hasherezade" << false;
	QTest::newRow("empty owner") << "/pe-bear" << false;
	QTest::newRow("empty name") << "hasherezade/" << false;
	QTest::newRow("second slash") << "hasherezade/pe-bear/releases" << false;
	QTest::newRow("dot-dot name") << "owner/.." << false;
	QTest::newRow("dot name") << "owner/." << false;
	QTest::newRow("query string") << "owner/repo?x=1" << false;
	QTest::newRow("at sign") << "owner/repo@main" << false;
	QTest::newRow("space") << "owner/pe bear" << false;
	QTest::newRow("url") << "https://github.com/owner/repo" << false;
	QTest::newRow("underscore in the owner") << "my_org/repo" << false;
	QTest::newRow("owner starting with a hyphen") << "-org/repo" << false;
	QTest::newRow("owner too long") << QString(40, QLatin1Char('a')) + "/repo" << false;
	QTest::newRow("non-ascii") << QString::fromUtf8("wła\u015bciciel/repo") << false;
}

void TestUpdateSettings::repositoryAcceptsOnlyPlausibleGitHubNames()
{
	QFETCH(QString, input);
	QFETCH(bool, accepted);
	QCOMPARE(UpdateSettings::isValidRepository(input), accepted);

	UpdateSettings s;
	QVERIFY(s.setRepository(QLatin1String("hasherezade/pe-bear")));
	QCOMPARE(s.setRepository(input), accepted);
	/* A refused value leaves the previous one in place. */
	QCOMPARE(s.repository(), accepted ? input : QString("hasherezade/pe-bear"));
}

void TestUpdateSettings::repositorySurvivesARoundTripAndACorruptValueIsDropped()
{
	UpdateSettings written;
	QVERIFY(written.setRepository(QLatin1String("mkilijanek/pe-bear")));
	QSettings settings;
	written.write(settings);
	settings.sync();

	UpdateSettings read;
	read.read(settings);
	QCOMPARE(read.repository(), QString("mkilijanek/pe-bear"));

	/* Edited by hand into something that is not a repository: dropped,
	   so the build's default is used, rather than trusted. */
	settings.beginGroup(QLatin1String(UpdateSettings::SETTINGS_GROUP));
	settings.setValue(QLatin1String("Repository"), QLatin1String("evil.example/x/../../y"));
	settings.endGroup();
	settings.sync();
	UpdateSettings again;
	again.read(settings);
	QVERIFY(again.repository().isEmpty());
}

QTEST_MAIN(TestUpdateSettings)
#include "tst_updatesettings.moc"

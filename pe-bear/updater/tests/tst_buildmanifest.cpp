#include <QtTest>
#include "../BuildProfile.h"
#include "../Version.h"

using namespace pe_bear::updater;

/*
 * pe-bear-build.json is written by CMake from the configure-time inputs; the
 * BuildProfile is compiled from the same inputs. Two descriptions of one
 * build can drift -- a new runtime threshold added on one side only, a
 * package type spelled differently -- and the asset selector would then
 * trust a manifest that no longer says what the binary says. This test is
 * the one place where the two are held together.
 */
class TestBuildManifest : public QObject
{
	Q_OBJECT
private slots:
	void theManifestExistsAndParses();
	void theManifestAgreesWithTheCompiledProfile();
	void theManifestVersionIsTheRunningVersion();
	void theManifestNamesTheReleaseSource();

private:
	QJsonObject load()
	{
		QFile f(QString::fromLocal8Bit(PEBEAR_BUILD_MANIFEST_PATH));
		if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
		QJsonParseError err;
		const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
		if (err.error != QJsonParseError::NoError || !doc.isObject()) return QJsonObject();
		return doc.object();
	}
};

void TestBuildManifest::theManifestExistsAndParses()
{
	QVERIFY2(QFile::exists(QString::fromLocal8Bit(PEBEAR_BUILD_MANIFEST_PATH)),
		"pe-bear-build.json was not generated at configure time");
	const QJsonObject m = load();
	QVERIFY(!m.isEmpty());
	QCOMPARE(m.value(QLatin1String("manifestVersion")).toInt(), 1);
	QCOMPARE(m.value(QLatin1String("name")).toString(), QString("PE-bear"));
	const char *required[] = { "version", "baseVersion", "forkPatch", "commit", "platform", "arch",
		"qtMajor", "qtVersion", "runtime", "packageType", "minOsVersion", "updateRepository", "updaterHelper" };
	for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
		QVERIFY2(m.contains(QLatin1String(required[i])), required[i]);
	}
}

void TestBuildManifest::theManifestAgreesWithTheCompiledProfile()
{
	const QJsonObject m = load();
	const BuildProfile p = BuildProfile::current();

	QCOMPARE(m.value(QLatin1String("platform")).toString(), platformToString(p.platform()));
	QCOMPARE(m.value(QLatin1String("arch")).toString(), architectureToString(p.architecture()));
	QCOMPARE(m.value(QLatin1String("qtMajor")).toInt(), p.qtMajor());
	QCOMPARE(m.value(QLatin1String("runtime")).toString(), p.runtime());
	/* An undeclared package type is "" in the manifest and PackageUnknown here. */
	const QString packageType = m.value(QLatin1String("packageType")).toString();
	if (packageType.isEmpty()) {
		QCOMPARE(p.packageType(), PackageUnknown);
	} else {
		QCOMPARE(packageType, packageTypeToString(p.packageType()));
	}
	QCOMPARE(m.value(QLatin1String("commit")).toString(), p.buildId());
	const QString minOs = m.value(QLatin1String("minOsVersion")).toString();
	if (minOs.isEmpty()) {
		QVERIFY(!p.minOsVersion().isValid());
	} else {
		QCOMPARE(Version::fromString(minOs), p.minOsVersion());
	}
	/* qtVersion is informational, but it must at least be of the declared major. */
	QVERIFY(m.value(QLatin1String("qtVersion")).toString().startsWith(
		QString::number(p.qtMajor()) + QLatin1Char('.')));
}

void TestBuildManifest::theManifestVersionIsTheRunningVersion()
{
	const QJsonObject m = load();
	QCOMPARE(m.value(QLatin1String("version")).toString(), Version::current().toString());
	QCOMPARE(m.value(QLatin1String("forkPatch")).toInt(), Version::current().forkPatch());
	const Version base = Version::fromString(m.value(QLatin1String("baseVersion")).toString());
	QVERIFY(base.isValid());
	QCOMPARE(base.forkPatch(), 0);
	QCOMPARE(base.major(), Version::current().major());
	QCOMPARE(base.minor(), Version::current().minor());
	QCOMPARE(base.micro(), Version::current().micro());
	QCOMPARE(base.patch(), Version::current().patch());
}

void TestBuildManifest::theManifestNamesTheReleaseSource()
{
	const QJsonObject m = load();
	const QString repo = m.value(QLatin1String("updateRepository")).toString();
#ifdef PEBEAR_UPDATE_REPOSITORY
	QCOMPARE(repo, QString::fromLatin1(PEBEAR_UPDATE_REPOSITORY));
#else
	QCOMPARE(repo, QString("hasherezade/pe-bear"));
#endif
#ifdef PEBEAR_WITH_LIBARCHIVE
	QCOMPARE(m.value(QLatin1String("updaterHelper")).toBool(false), true);
#else
	QCOMPARE(m.value(QLatin1String("updaterHelper")).toBool(true), false);
#endif
}

QTEST_GUILESS_MAIN(TestBuildManifest)
#include "tst_buildmanifest.moc"

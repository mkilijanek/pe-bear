/*
 * Covers PR #2 against the real GitHub Releases payload.
 *
 * data/release_v0.7.2.json is the actual "releases/latest" response for
 * v0.7.2, reduced to the fields the updater reads. It is checked in on purpose:
 * asset names are a hand-maintained convention upstream, so if that convention
 * drifts, this test says so instead of a user finding out that no package
 * matches their build.
 *
 * Nothing here touches the network.
 */
#include <QtTest>
#include "../ReleaseClient.h"
#include "../AssetSelector.h"

using namespace pe_bear::updater;

namespace {

	BuildProfile profileFor(Platform p, Architecture a, int qtMajor,
		PackageType t, const QString &runtime = QString())
	{
		BuildProfile profile;
		profile.setPlatform(p);
		profile.setArchitecture(a);
		profile.setQtMajor(qtMajor);
		profile.setPackageType(t);
		profile.setRuntime(runtime);
		return profile;
	}

}; // namespace

class TestLiveRelease : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void parsesTheRealPayload();
	void everyPublishedAssetHasAUsableDigest();
	void everyPublishedAssetNameIsUnderstood();
	void eachShippedVariantResolvesToExactlyOnePackage_data();
	void eachShippedVariantResolvesToExactlyOnePackage();
	void aVariantThatIsNotPublishedGetsNoPackage();

private:
	QByteArray m_payload;
	ReleaseInfo m_release;
};

void TestLiveRelease::initTestCase()
{
	const QString path = QLatin1String(FIXTURE_DIR) + QLatin1String("/release_v0.7.2.json");
	QFile f(path);
	QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(QString("cannot open fixture: ") + path));
	m_payload = f.readAll();
	f.close();
	QVERIFY(!m_payload.isEmpty());

	UpdateError error = ErrorNone;
	QString detail;
	QVERIFY2(ReleaseClient::parseLatestRelease(m_payload, m_release, error, detail),
		qPrintable(detail));
}

void TestLiveRelease::parsesTheRealPayload()
{
	QCOMPARE(m_release.tagName, QString("v0.7.2"));
	QCOMPARE(m_release.version.toString(), QString("0.7.2"));
	QCOMPARE(m_release.assets.size(), 8);
	QVERIFY(m_release.htmlUrl.isValid());
}

void TestLiveRelease::everyPublishedAssetHasAUsableDigest()
{
	/* Verification is mandatory, so an asset without a digest is unusable. */
	for (int i = 0; i < m_release.assets.size(); i++) {
		const ReleaseAsset &a = m_release.assets.at(i);
		QVERIFY2(a.hasDigest(), qPrintable(a.name + QLatin1String(": no SHA-256 published")));
		QVERIFY2(!a.digestMalformed, qPrintable(a.name + QLatin1String(": malformed digest")));
		QCOMPARE(a.sha256.length(), 64);
		QVERIFY(a.size > 0);
	}
}

void TestLiveRelease::everyPublishedAssetNameIsUnderstood()
{
	for (int i = 0; i < m_release.assets.size(); i++) {
		ReleaseAsset a = m_release.assets.at(i);
		QVERIFY2(AssetSelector::parseAssetName(a.name, a),
			qPrintable(a.name + QLatin1String(": the naming convention has drifted")));
	}
}

void TestLiveRelease::eachShippedVariantResolvesToExactlyOnePackage_data()
{
	QTest::addColumn<int>("platform");
	QTest::addColumn<int>("arch");
	QTest::addColumn<int>("qtMajor");
	QTest::addColumn<int>("packageType");
	QTest::addColumn<QString>("runtime");
	QTest::addColumn<QString>("expected");

	QTest::newRow("win qt4 x86 vs10")
		<< int(PlatformWindows) << int(ArchX86) << 4 << int(PackageWindowsZip) << "vs10"
		<< "PE-bear_0.7.2_qt4_x86_win_vs10.zip";
	QTest::newRow("win qt5 x64 vs17")
		<< int(PlatformWindows) << int(ArchX64) << 5 << int(PackageWindowsZip) << "vs17"
		<< "PE-bear_0.7.2_qt5_x64_win_vs17.zip";
	QTest::newRow("win qt5 x86 vs17")
		<< int(PlatformWindows) << int(ArchX86) << 5 << int(PackageWindowsZip) << "vs17"
		<< "PE-bear_0.7.2_qt5_x86_win_vs17.zip";
	QTest::newRow("win qt6 x64 vs22")
		<< int(PlatformWindows) << int(ArchX64) << 6 << int(PackageWindowsZip) << "vs22"
		<< "PE-bear_0.7.2_qt6_x64_win_vs22.zip";
	QTest::newRow("linux qt5 x64 tar.xz")
		<< int(PlatformLinux) << int(ArchX64) << 5 << int(PackageLinuxTarXz) << ""
		<< "PE-bear_0.7.2_qt5.15.13_x64_linux.tar.xz";
	QTest::newRow("linux qt6 x64 tar.xz")
		<< int(PlatformLinux) << int(ArchX64) << 6 << int(PackageLinuxTarXz) << ""
		<< "PE-bear_0.7.2_qt6.4.2_x64_linux.tar.xz";
	QTest::newRow("linux qt6 x64 appimage")
		<< int(PlatformLinux) << int(ArchX64) << 6 << int(PackageLinuxAppImage) << ""
		<< "PE-bear_0.7.2_qt6_x86_64_linux.AppImage";
	QTest::newRow("macos qt6 x64 bundle")
		<< int(PlatformMacOS) << int(ArchX64) << 6 << int(PackageMacAppZip) << ""
		<< "PE-bear_0.7.2_qt6_x64_macos.app.zip";
}

void TestLiveRelease::eachShippedVariantResolvesToExactlyOnePackage()
{
	QFETCH(int, platform);
	QFETCH(int, arch);
	QFETCH(int, qtMajor);
	QFETCH(int, packageType);
	QFETCH(QString, runtime);
	QFETCH(QString, expected);

	const BuildProfile profile = profileFor(Platform(platform), Architecture(arch),
		qtMajor, PackageType(packageType), runtime);
	const AssetSelector selector(profile);

	ReleaseAsset selected;
	QStringList reasons;
	const AssetSelector::Outcome outcome = selector.select(m_release, selected, &reasons);

	if (outcome != AssetSelector::Selected) {
		QString message = QLatin1String("no unique package for ") + profile.toString()
			+ QLatin1String("\n");
		for (int i = 0; i < reasons.size(); i++) {
			message += QLatin1String("  ") + reasons.at(i) + QLatin1Char('\n');
		}
		QFAIL(qPrintable(message));
	}
	QCOMPARE(selected.name, expected);
	QVERIFY(selected.hasDigest());
}

void TestLiveRelease::aVariantThatIsNotPublishedGetsNoPackage()
{
	/* No arm64 package exists yet: such a build must be told so, not handed an
	   x64 one. */
	const AssetSelector selector(profileFor(PlatformLinux, ArchArm64, 6, PackageLinuxAppImage));
	ReleaseAsset selected;
	QCOMPARE(selector.select(m_release, selected), AssetSelector::NoCompatible);

	/* Nor does a Qt6 macOS arm64 build silently get the Intel bundle. */
	const AssetSelector macSelector(profileFor(PlatformMacOS, ArchArm64, 6, PackageMacAppZip));
	QCOMPARE(macSelector.select(m_release, selected), AssetSelector::NoCompatible);
}

QTEST_MAIN(TestLiveRelease)
#include "tst_liverelease.moc"

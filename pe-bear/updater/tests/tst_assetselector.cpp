/*
 * Covers asset selection of PR #2 (UT-06, UT-07, UT-09, UT-14).
 *
 * The fixtures are the asset names actually published for v0.7.2, so a change
 * to the naming convention upstream will fail here rather than silently in
 * front of a user.
 */
#include <QtTest>
#include "../AssetSelector.h"

using namespace pe_bear::updater;

namespace {

	const char* REAL_ASSET_NAMES[] = {
		"PE-bear_0.7.2_qt4_x86_win_vs10.zip",
		"PE-bear_0.7.2_qt5.15.13_x64_linux.tar.xz",
		"PE-bear_0.7.2_qt5_x64_win_vs17.zip",
		"PE-bear_0.7.2_qt5_x86_win_vs17.zip",
		"PE-bear_0.7.2_qt6.4.2_x64_linux.tar.xz",
		"PE-bear_0.7.2_qt6_x64_macos.app.zip",
		"PE-bear_0.7.2_qt6_x64_win_vs22.zip",
		"PE-bear_0.7.2_qt6_x86_64_linux.AppImage"
	};
	const int REAL_ASSET_COUNT = sizeof(REAL_ASSET_NAMES) / sizeof(REAL_ASSET_NAMES[0]);

	const char* DIGEST_A = "1c44f2e8f1db6ff38292c1a3d84c8b5be981adffe374726de076cb16e1f0ab27";

	ReleaseAsset makeAsset(const QString &name, const QString &digest = QLatin1String(DIGEST_A))
	{
		ReleaseAsset a;
		a.name = name;
		a.downloadUrl = QUrl(QLatin1String("https://github.com/hasherezade/pe-bear/releases/download/v0.7.2/") + name);
		a.size = 1024;
		a.sha256 = digest;
		return a;
	}

	ReleaseInfo makeRelease(const QStringList &names)
	{
		ReleaseInfo r;
		r.tagName = QLatin1String("v0.7.3");
		r.version = Version::fromString(QLatin1String("0.7.3"));
		for (int i = 0; i < names.size(); i++) {
			r.assets.append(makeAsset(names.at(i)));
		}
		return r;
	}

	ReleaseInfo realRelease()
	{
		QStringList names;
		for (int i = 0; i < REAL_ASSET_COUNT; i++) {
			names << QLatin1String(REAL_ASSET_NAMES[i]);
		}
		return makeRelease(names);
	}

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

class TestAssetSelector : public QObject
{
	Q_OBJECT

private slots:
	void parsesEveryPublishedAssetName_data();
	void parsesEveryPublishedAssetName();

	void rejectsNamesOutsideTheGrammar_data();
	void rejectsNamesOutsideTheGrammar();

	void picksTheLinuxTarXzForAQt5Build();
	void picksTheAppImageForAnAppImageBuild();
	void anAppImageBuildIsToldButNotOffered();
	void aMacBundleBuildIsToldButNotOffered();
	void installsByDefaultOnlyWhatTheDirectoryInstallerHandles();
	void picksTheWindowsPackageMatchingTheRuntime();
	void refusesToMigrateBetweenQtMajors();
	void refusesToMigrateBetweenArchitectures();
	void refusesWhenTheRuntimeCannotBeProven();
	void reportsAmbiguityInsteadOfGuessing();
	void reportsMissingDigestDistinctlyFromNoAsset();
	void reportsMalformedDigestDistinctly();
	void refusesWhenThisBuildCannotDescribeItself();
	void refusesWhenNoInstallerHandlesThePackage();
};

void TestAssetSelector::parsesEveryPublishedAssetName_data()
{
	QTest::addColumn<QString>("name");
	QTest::addColumn<int>("platform");
	QTest::addColumn<int>("arch");
	QTest::addColumn<int>("qtMajor");
	QTest::addColumn<int>("packageType");
	QTest::addColumn<QString>("runtime");

	QTest::newRow("win qt4 x86")
		<< "PE-bear_0.7.2_qt4_x86_win_vs10.zip"
		<< int(PlatformWindows) << int(ArchX86) << 4 << int(PackageWindowsZip) << "vs10";
	QTest::newRow("linux qt5 tar.xz with full qt version")
		<< "PE-bear_0.7.2_qt5.15.13_x64_linux.tar.xz"
		<< int(PlatformLinux) << int(ArchX64) << 5 << int(PackageLinuxTarXz) << "";
	QTest::newRow("win qt5 x64")
		<< "PE-bear_0.7.2_qt5_x64_win_vs17.zip"
		<< int(PlatformWindows) << int(ArchX64) << 5 << int(PackageWindowsZip) << "vs17";
	QTest::newRow("macos app bundle")
		<< "PE-bear_0.7.2_qt6_x64_macos.app.zip"
		<< int(PlatformMacOS) << int(ArchX64) << 6 << int(PackageMacAppZip) << "";
	/* x86_64 contains the very separator the name is split on */
	QTest::newRow("appimage with x86_64 arch")
		<< "PE-bear_0.7.2_qt6_x86_64_linux.AppImage"
		<< int(PlatformLinux) << int(ArchX64) << 6 << int(PackageLinuxAppImage) << "";
}

void TestAssetSelector::parsesEveryPublishedAssetName()
{
	QFETCH(QString, name);
	QFETCH(int, platform);
	QFETCH(int, arch);
	QFETCH(int, qtMajor);
	QFETCH(int, packageType);
	QFETCH(QString, runtime);

	ReleaseAsset asset;
	QVERIFY2(AssetSelector::parseAssetName(name, asset), qPrintable(name));
	QCOMPARE(int(asset.platform), platform);
	QCOMPARE(int(asset.arch), arch);
	QCOMPARE(asset.qtMajor, qtMajor);
	QCOMPARE(int(asset.packageType), packageType);
	QCOMPARE(asset.runtime, runtime);
}

void TestAssetSelector::rejectsNamesOutsideTheGrammar_data()
{
	QTest::addColumn<QString>("name");

	QTest::newRow("empty") << "";
	QTest::newRow("unrelated project") << "some-other-tool_0.7.2_qt5_x64_linux.tar.xz";
	QTest::newRow("no extension") << "PE-bear_0.7.2_qt5_x64_linux";
	QTest::newRow("unknown extension") << "PE-bear_0.7.2_qt5_x64_linux.deb";
	QTest::newRow("no qt token") << "PE-bear_0.7.2_x64_linux.tar.xz";
	QTest::newRow("no arch") << "PE-bear_0.7.2_qt5_linux.tar.xz";
	QTest::newRow("unaccounted token") << "PE-bear_0.7.2_qt5_x64_linux_experimental.tar.xz";
	QTest::newRow("package type wrong for os") << "PE-bear_0.7.2_qt5_x64_linux.zip";
	QTest::newRow("two platforms") << "PE-bear_0.7.2_qt5_x64_linux_win.tar.xz";
}

void TestAssetSelector::rejectsNamesOutsideTheGrammar()
{
	QFETCH(QString, name);
	ReleaseAsset asset;
	QVERIFY2(!AssetSelector::parseAssetName(name, asset), qPrintable(QString("accepted: ") + name));
}

void TestAssetSelector::picksTheLinuxTarXzForAQt5Build()
{
	const AssetSelector selector(profileFor(PlatformLinux, ArchX64, 5, PackageLinuxTarXz));
	ReleaseAsset selected;
	QStringList reasons;
	QCOMPARE(selector.select(realRelease(), selected, &reasons), AssetSelector::Selected);
	QCOMPARE(selected.name, QString("PE-bear_0.7.2_qt5.15.13_x64_linux.tar.xz"));
}

void TestAssetSelector::picksTheAppImageForAnAppImageBuild()
{
	/* Same OS, arch and Qt major as the tar.xz: only the package type tells
	   them apart, which is exactly why it is part of the profile. */
	QSet<int> installable;
	installable.insert(int(PackageLinuxAppImage));
	const AssetSelector selector(profileFor(PlatformLinux, ArchX64, 6, PackageLinuxAppImage), installable);
	ReleaseAsset selected;
	QCOMPARE(selector.select(realRelease(), selected), AssetSelector::Selected);
	QCOMPARE(selected.name, QString("PE-bear_0.7.2_qt6_x86_64_linux.AppImage"));
}

void TestAssetSelector::anAppImageBuildIsToldButNotOffered()
{
	/* No AppImage installer exists: the matching asset is still found and
	   handed back, so the user can be pointed at it, but it is not Selected. */
	const AssetSelector selector(profileFor(PlatformLinux, ArchX64, 6, PackageLinuxAppImage));
	ReleaseAsset selected;
	QStringList reasons;
	QCOMPARE(selector.select(realRelease(), selected, &reasons), AssetSelector::NotInstallable);
	QCOMPARE(selected.name, QString("PE-bear_0.7.2_qt6_x86_64_linux.AppImage"));
	QVERIFY(reasons.last().contains(QLatin1String("no installer for linux-appimage")));
}

void TestAssetSelector::aMacBundleBuildIsToldButNotOffered()
{
	const AssetSelector selector(profileFor(PlatformMacOS, ArchX64, 6, PackageMacAppZip));
	ReleaseAsset selected;
	QCOMPARE(selector.select(realRelease(), selected), AssetSelector::NotInstallable);
	QCOMPARE(selected.name, QString("PE-bear_0.7.2_qt6_x64_macos.app.zip"));
}

void TestAssetSelector::installsByDefaultOnlyWhatTheDirectoryInstallerHandles()
{
	QCOMPARE(AssetSelector::defaultInstallablePackageTypes(
		profileFor(PlatformWindows, ArchX64, 6, PackageWindowsZip, QLatin1String("vs22"))),
		QSet<int>() << int(PackageWindowsZip));
	QCOMPARE(AssetSelector::defaultInstallablePackageTypes(
		profileFor(PlatformLinux, ArchX64, 6, PackageLinuxTarXz)),
		QSet<int>() << int(PackageLinuxTarXz));
	QVERIFY(AssetSelector::defaultInstallablePackageTypes(
		profileFor(PlatformLinux, ArchX64, 6, PackageLinuxAppImage)).isEmpty());
	QVERIFY(AssetSelector::defaultInstallablePackageTypes(
		profileFor(PlatformMacOS, ArchArm64, 6, PackageMacAppZip)).isEmpty());
	QVERIFY(AssetSelector::defaultInstallablePackageTypes(
		profileFor(PlatformLinux, ArchX64, 6, PackageUnknown)).isEmpty());
}

void TestAssetSelector::picksTheWindowsPackageMatchingTheRuntime()
{
	const AssetSelector selector(profileFor(PlatformWindows, ArchX64, 6,
		PackageWindowsZip, QLatin1String("vs22")));
	ReleaseAsset selected;
	QCOMPARE(selector.select(realRelease(), selected), AssetSelector::Selected);
	QCOMPARE(selected.name, QString("PE-bear_0.7.2_qt6_x64_win_vs22.zip"));
}

void TestAssetSelector::refusesToMigrateBetweenQtMajors()
{
	/* A Qt4 build must never be handed a Qt5 or Qt6 package. */
	const AssetSelector selector(profileFor(PlatformWindows, ArchX86, 4,
		PackageWindowsZip, QLatin1String("vs17")));
	ReleaseAsset selected;
	const ReleaseInfo release = makeRelease(QStringList()
		<< QLatin1String("PE-bear_0.7.2_qt5_x86_win_vs17.zip"));
	QCOMPARE(selector.select(release, selected), AssetSelector::NoCompatible);
}

void TestAssetSelector::refusesToMigrateBetweenArchitectures()
{
	const AssetSelector selector(profileFor(PlatformWindows, ArchX86, 6,
		PackageWindowsZip, QLatin1String("vs22")));
	ReleaseAsset selected;
	QCOMPARE(selector.select(realRelease(), selected), AssetSelector::NoCompatible);
}

void TestAssetSelector::refusesWhenTheRuntimeCannotBeProven()
{
	/* A build that cannot state its runtime (e.g. MinGW) must not be given a
	   package built with a specific MSVC toolchain. */
	const AssetSelector selector(profileFor(PlatformWindows, ArchX64, 6, PackageWindowsZip));
	ReleaseAsset selected;
	QCOMPARE(selector.select(realRelease(), selected), AssetSelector::NoCompatible);
}

void TestAssetSelector::reportsAmbiguityInsteadOfGuessing()
{
	/* A build that does not declare its package type matches both Linux Qt6
	   packages; taking either could move the user to a different variant. */
	BuildProfile profile = profileFor(PlatformLinux, ArchX64, 6, PackageUnknown);
	QSet<int> installable;
	installable.insert(int(PackageLinuxTarXz));
	installable.insert(int(PackageLinuxAppImage));

	const AssetSelector selector(profile, installable);
	ReleaseAsset selected;
	QStringList reasons;
	QCOMPARE(selector.select(realRelease(), selected, &reasons), AssetSelector::Ambiguous);
	QVERIFY(selected.name.isEmpty());
	QVERIFY(!reasons.isEmpty());
}

void TestAssetSelector::reportsMissingDigestDistinctlyFromNoAsset()
{
	ReleaseInfo release;
	release.tagName = QLatin1String("v0.7.3");
	release.version = Version::fromString(QLatin1String("0.7.3"));
	release.assets.append(makeAsset(
		QLatin1String("PE-bear_0.7.3_qt5_x64_linux.tar.xz"), QString()));

	const AssetSelector selector(profileFor(PlatformLinux, ArchX64, 5, PackageLinuxTarXz));
	ReleaseAsset selected;
	UpdateError digestIssue = ErrorNone;
	QCOMPARE(selector.select(release, selected, NULL, &digestIssue), AssetSelector::NoCompatible);
	QCOMPARE(digestIssue, ErrorMissingDigest);
}

void TestAssetSelector::reportsMalformedDigestDistinctly()
{
	ReleaseInfo release;
	release.tagName = QLatin1String("v0.7.3");
	release.version = Version::fromString(QLatin1String("0.7.3"));
	ReleaseAsset asset = makeAsset(QLatin1String("PE-bear_0.7.3_qt5_x64_linux.tar.xz"), QString());
	asset.digestMalformed = true;
	release.assets.append(asset);

	const AssetSelector selector(profileFor(PlatformLinux, ArchX64, 5, PackageLinuxTarXz));
	ReleaseAsset selected;
	UpdateError digestIssue = ErrorNone;
	QCOMPARE(selector.select(release, selected, NULL, &digestIssue), AssetSelector::NoCompatible);
	QCOMPARE(digestIssue, ErrorInvalidDigest);
}

void TestAssetSelector::refusesWhenThisBuildCannotDescribeItself()
{
	BuildProfile incomplete;
	const AssetSelector selector(incomplete);
	ReleaseAsset selected;
	QStringList reasons;
	QCOMPARE(selector.select(realRelease(), selected, &reasons), AssetSelector::NoCompatible);
	QVERIFY(!reasons.isEmpty());
}

void TestAssetSelector::refusesWhenNoInstallerHandlesThePackage()
{
	const AssetSelector selector(profileFor(PlatformLinux, ArchX64, 5, PackageLinuxTarXz),
		QSet<int>());
	ReleaseAsset selected;
	QCOMPARE(selector.select(realRelease(), selected), AssetSelector::NotInstallable);
	/* Distinct from "no package fits": the package is named. */
	QCOMPARE(selected.name, QString("PE-bear_0.7.2_qt5.15.13_x64_linux.tar.xz"));
}

QTEST_APPLESS_MAIN(TestAssetSelector)
#include "tst_assetselector.moc"

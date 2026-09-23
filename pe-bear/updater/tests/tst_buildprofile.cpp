/* Covers the build profile of PR #1: this build must be able to describe
   itself precisely enough for an asset to be matched against it. */
#include <QtTest>
#include "../BuildProfile.h"

using namespace pe_bear::updater;

class TestBuildProfile : public QObject
{
	Q_OBJECT

private slots:
	void currentBuildDescribesItself();
	void currentBuildReportsTheCompiledQtMajor();
	void packageTypesBelongToOnePlatform();
	void incompleteProfileIsDetected();
	void stringFormIsStableAndReadable();
};

void TestBuildProfile::currentBuildDescribesItself()
{
	const BuildProfile p = BuildProfile::current();
	QVERIFY(p.platform() != PlatformUnknown);
	QVERIFY(p.architecture() != ArchUnknown);
	QVERIFY(p.isComplete());
}

void TestBuildProfile::currentBuildReportsTheCompiledQtMajor()
{
	QCOMPARE(BuildProfile::current().qtMajor(), QT_VERSION_MAJOR);
}

void TestBuildProfile::packageTypesBelongToOnePlatform()
{
	QVERIFY(isPackageTypeOnPlatform(PackageWindowsZip, PlatformWindows));
	QVERIFY(!isPackageTypeOnPlatform(PackageWindowsZip, PlatformLinux));
	QVERIFY(isPackageTypeOnPlatform(PackageLinuxAppImage, PlatformLinux));
	QVERIFY(isPackageTypeOnPlatform(PackageLinuxTarXz, PlatformLinux));
	QVERIFY(!isPackageTypeOnPlatform(PackageLinuxTarXz, PlatformMacOS));
	QVERIFY(isPackageTypeOnPlatform(PackageMacAppZip, PlatformMacOS));
	QVERIFY(!isPackageTypeOnPlatform(PackageUnknown, PlatformLinux));
}

void TestBuildProfile::incompleteProfileIsDetected()
{
	BuildProfile p;
	QVERIFY(!p.isComplete());
	p.setPlatform(PlatformLinux);
	QVERIFY(!p.isComplete());
	p.setArchitecture(ArchX64);
	QVERIFY(!p.isComplete());
	p.setQtMajor(5);
	QVERIFY(p.isComplete());
}

void TestBuildProfile::stringFormIsStableAndReadable()
{
	BuildProfile p;
	p.setPlatform(PlatformWindows);
	p.setArchitecture(ArchX64);
	p.setQtMajor(6);
	p.setPackageType(PackageWindowsZip);
	p.setRuntime(QLatin1String("vs22"));

	QCOMPARE(p.toString(), QString("windows/x64/qt6/windows-zip/vs22"));
}

QTEST_APPLESS_MAIN(TestBuildProfile)
#include "tst_buildprofile.moc"

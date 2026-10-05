/*
 * Covers installation detection of PR #2.
 *
 * The rule being protected: a copy of PE-bear that the system owns, or that
 * the current user cannot write to, is reported and left alone. No elevation,
 * no package manager, ever.
 */
#include <QtTest>
#include "../InstallationDetector.h"

#ifdef Q_OS_UNIX
	#include <unistd.h>
#endif

using namespace pe_bear::updater;

namespace {

	QMap<QString, QString> env(const QString &key = QString(), const QString &value = QString())
	{
		QMap<QString, QString> e;
		if (!key.isEmpty()) e.insert(key, value);
		return e;
	}

}; // namespace

class TestInstallationDetector : public QObject
{
	Q_OBJECT

private slots:
	void aWritableDirectoryIsPortableAndUpdatable();
	void aFlatpakSandboxIsManaged();
	void aSnapSandboxIsManaged();
	void anAppImageIsPortableWhereverItSits();
	void anUnknownDirectoryIsNeverUpdatable();
	void managedInstallationsAreNeverUpdatable();
	void nonWritableInstallationsAreNeverUpdatable();
	void writabilityIsProbedNotAssumed();
#ifdef Q_OS_LINUX
	void systemDirectoriesAreRecognised_data();
	void systemDirectoriesAreRecognised();
	void aPersonalFolderItselfIsNeverUpdatable();
	void aSubdirectoryOfAPersonalFolderIsFine();
	void aFilesystemRootIsNeverUpdatable();
#endif
};

void TestInstallationDetector::aWritableDirectoryIsPortableAndUpdatable()
{
	QTemporaryDir dir;
	const InstallationInfo info = InstallationDetector::detectAt(
		dir.path(), QDir(dir.path()).absoluteFilePath("PE-bear"), env());

	QCOMPARE(info.kind, InstallPortable);
	QVERIFY(info.writable);
	QVERIFY(info.isUpdatable());
}

void TestInstallationDetector::aFlatpakSandboxIsManaged()
{
	QTemporaryDir dir;
	const InstallationInfo info = InstallationDetector::detectAt(
		dir.path(), QDir(dir.path()).absoluteFilePath("PE-bear"),
		env(QLatin1String("FLATPAK_ID"), QLatin1String("net.hasherezade.pe-bear")));

	QCOMPARE(info.kind, InstallManaged);
	QVERIFY2(!info.isUpdatable(), "a Flatpak payload must not be replaced in place");
}

void TestInstallationDetector::aSnapSandboxIsManaged()
{
	QTemporaryDir dir;
	const InstallationInfo info = InstallationDetector::detectAt(
		dir.path(), QDir(dir.path()).absoluteFilePath("PE-bear"),
		env(QLatin1String("SNAP"), QLatin1String("/snap/pe-bear/12")));

	QCOMPARE(info.kind, InstallManaged);
	QVERIFY(!info.isUpdatable());
}

void TestInstallationDetector::anAppImageIsPortableWhereverItSits()
{
	QTemporaryDir dir;
	const QString imagePath = QDir(dir.path()).absoluteFilePath("PE-bear.AppImage");

	const InstallationInfo info = InstallationDetector::detectAt(
		QLatin1String("/tmp/.mount_something"), QLatin1String("/tmp/.mount_something/AppRun"),
		env(QLatin1String("APPIMAGE"), imagePath));

	QCOMPARE(info.kind, InstallPortable);
	/* What matters is where the image file lives, not the mount point. */
	QCOMPARE(info.executablePath, imagePath);
	QCOMPARE(QDir(info.installDir).absolutePath(), QDir(dir.path()).absolutePath());
	QVERIFY(info.isUpdatable());
}

void TestInstallationDetector::anUnknownDirectoryIsNeverUpdatable()
{
	const InstallationInfo info = InstallationDetector::detectAt(QString(), QString(), env());
	QCOMPARE(info.kind, InstallUnknown);
	QVERIFY(!info.isUpdatable());
}

void TestInstallationDetector::managedInstallationsAreNeverUpdatable()
{
	InstallationInfo info;
	info.kind = InstallManaged;
	info.writable = true; /* even if it happens to be writable */
	QVERIFY(!info.isUpdatable());
}

void TestInstallationDetector::nonWritableInstallationsAreNeverUpdatable()
{
	InstallationInfo info;
	info.kind = InstallSystem;
	info.writable = false;
	QVERIFY(!info.isUpdatable());

	info.kind = InstallPortable;
	QVERIFY(!info.isUpdatable());
}

void TestInstallationDetector::writabilityIsProbedNotAssumed()
{
	QTemporaryDir dir;
	QVERIFY(InstallationDetector::isDirectoryWritable(dir.path()));
	QVERIFY(!InstallationDetector::isDirectoryWritable(
		QDir(dir.path()).absoluteFilePath("no-such-subdir")));

#ifdef Q_OS_UNIX
	if (geteuid() == 0) {
		QSKIP("running as root: every directory is writable");
	}
	const QString readOnly = QDir(dir.path()).absoluteFilePath("readonly");
	QVERIFY(QDir().mkpath(readOnly));
	QVERIFY(QFile::setPermissions(readOnly, QFile::ReadOwner | QFile::ExeOwner));
	QVERIFY2(!InstallationDetector::isDirectoryWritable(readOnly),
		"a read-only directory was reported as writable");
	QFile::setPermissions(readOnly, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
#endif
}

#ifdef Q_OS_LINUX
void TestInstallationDetector::systemDirectoriesAreRecognised_data()
{
	QTest::addColumn<QString>("path");
	QTest::addColumn<int>("kind");

	QTest::newRow("usr bin") << "/usr/bin" << int(InstallManaged);
	QTest::newRow("usr local bin") << "/usr/local/bin" << int(InstallManaged);
	QTest::newRow("opt") << "/opt/pe-bear" << int(InstallSystem);
	QTest::newRow("snap mount") << "/snap/pe-bear/current" << int(InstallManaged);
	QTest::newRow("home") << "/home/somebody/tools/pe-bear" << int(InstallPortable);
}

void TestInstallationDetector::systemDirectoriesAreRecognised()
{
	QFETCH(QString, path);
	QFETCH(int, kind);

	const InstallationInfo info = InstallationDetector::detectAt(
		path, path + QLatin1String("/PE-bear"), env());
	QCOMPARE(int(info.kind), kind);
}
#endif

void TestInstallationDetector::aPersonalFolderItselfIsNeverUpdatable()
{
	/* A portable build unzipped straight onto the Desktop: common, and the
	   one layout the directory-swapping installer must never touch, because
	   replacing the directory would replace the Desktop. The folder is
	   declared rather than looked up, so this does not depend on where the
	   test happens to run. */
	QTemporaryDir desktop;
	const InstallationInfo info = InstallationDetector::detectAt(
		desktop.path(), QDir(desktop.path()).absoluteFilePath("PE-bear"), env(),
		QStringList() << QFileInfo(desktop.path()).canonicalFilePath());

	QCOMPARE(info.kind, InstallUserFolder);
	QVERIFY2(!info.isUpdatable(), "a personal folder was offered for replacement");
	QVERIFY2(info.detail.contains(QLatin1String("personal folder")), qPrintable(info.detail));
	/* Writable, and still refused: writability is not the question here. */
	QVERIFY(info.writable);
}

void TestInstallationDetector::aSubdirectoryOfAPersonalFolderIsFine()
{
	/* Desktop/pe-bear is a perfectly good place; only the Desktop itself is
	   not. Refusing subdirectories would refuse nearly every portable install. */
	QTemporaryDir desktop;
	const QString inside = QDir(desktop.path()).absoluteFilePath("pe-bear");
	QVERIFY(QDir().mkpath(inside));

	const InstallationInfo info = InstallationDetector::detectAt(
		inside, inside + QLatin1String("/PE-bear"), env(),
		QStringList() << QFileInfo(desktop.path()).canonicalFilePath());

	QCOMPARE(info.kind, InstallPortable);
	QVERIFY(info.isUpdatable());
}

void TestInstallationDetector::aFilesystemRootIsNeverUpdatable()
{
	const QString root = QDir::rootPath();
	const InstallationInfo info = InstallationDetector::detectAt(
		root, root + QLatin1String("PE-bear"), env(), QStringList());

	QCOMPARE(info.kind, InstallUserFolder);
	QVERIFY(!info.isUpdatable());
	QVERIFY2(info.detail.contains(QLatin1String("root")), qPrintable(info.detail));
}

QTEST_MAIN(TestInstallationDetector)
#include "tst_installationdetector.moc"

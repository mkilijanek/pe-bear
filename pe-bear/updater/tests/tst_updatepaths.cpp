/*
 * Covers the private update directory of PR #3.
 *
 * Downloads, staging, transaction records and backups are kept apart, each run
 * gets a fresh random directory, and nothing is world-readable while it waits
 * to be installed.
 */
#include <QtTest>
#include "../UpdatePaths.h"
#include "../FileSystem.h"

using namespace pe_bear::updater;

class TestUpdatePaths : public QObject
{
	Q_OBJECT

private slots:
	void createsTheWholeTreeSeparately();
	void subdirectoriesAreDistinct();
	void restrictsPermissionsToTheOwner();
	void createsAFreshRandomDirectoryPerDownload();
	void prefersStagingOnTheInstallationVolume();
	void stagingSurvivesTheInstallationBeingMovedAside();
	void fallsBackWhenTheInstallationIsNotWritable();
	void defaultRootIsOutsideTheInstallation();
	void removesADirectoryTreeCompletely();
	void defaultRootDoesNotDependOnWhichExecutableAsks();
	void parentDirectoryIsAPureStringOperation();
	void parentDirectoryIsAPureStringOperation_data();
};

void TestUpdatePaths::createsTheWholeTreeSeparately()
{
	QTemporaryDir tmp;
	UpdatePaths paths(QDir(tmp.path()).absoluteFilePath("PE-bear-updates"), QString());

	QString error;
	QVERIFY2(paths.prepare(&error), qPrintable(error));
	QVERIFY(QDir(paths.root()).exists());
	QVERIFY(QDir(paths.downloadsDir()).exists());
	QVERIFY(QDir(paths.stagingDir()).exists());
	QVERIFY(QDir(paths.transactionsDir()).exists());
	QVERIFY(QDir(paths.backupsDir()).exists());
}

void TestUpdatePaths::subdirectoriesAreDistinct()
{
	QTemporaryDir tmp;
	UpdatePaths paths(QDir(tmp.path()).absoluteFilePath("u"), QString());

	QSet<QString> all;
	all << paths.downloadsDir() << paths.stagingDir()
		<< paths.transactionsDir() << paths.backupsDir() << paths.logFilePath();
	QCOMPARE(all.size(), 5);
}

void TestUpdatePaths::restrictsPermissionsToTheOwner()
{
	QTemporaryDir tmp;
	UpdatePaths paths(QDir(tmp.path()).absoluteFilePath("u"), QString());
	QVERIFY(paths.prepare());

#ifdef Q_OS_UNIX
	const QFile::Permissions perms = QFile::permissions(paths.downloadsDir());
	QVERIFY2((perms & QFile::ReadGroup) == 0, "the download directory is group-readable");
	QVERIFY2((perms & QFile::ReadOther) == 0, "the download directory is world-readable");
	QVERIFY((perms & QFile::ReadOwner) != 0);
	QVERIFY((perms & QFile::WriteOwner) != 0);
#else
	QSKIP("POSIX permission bits are only meaningful on Unix");
#endif
}

void TestUpdatePaths::createsAFreshRandomDirectoryPerDownload()
{
	QTemporaryDir tmp;
	UpdatePaths paths(QDir(tmp.path()).absoluteFilePath("u"), QString());
	QVERIFY(paths.prepare());

	const QString first = paths.createDownloadDir();
	const QString second = paths.createDownloadDir();

	QVERIFY(!first.isEmpty());
	QVERIFY(!second.isEmpty());
	QVERIFY2(first != second, "two downloads shared a directory");
	QVERIFY(QDir(first).exists());
	QVERIFY(QDir(second).exists());
	QVERIFY(first.startsWith(paths.downloadsDir()));
}

void TestUpdatePaths::prefersStagingOnTheInstallationVolume()
{
	/* Beside the installation, which is what lets the installer replace it by
	   rename instead of copying across a volume boundary. */
	QTemporaryDir parent;
	const QString installDir = parent.path() + QLatin1String("/pe-bear");
	QVERIFY(QDir().mkpath(installDir));
	QTemporaryDir fallback;

	const QString staging = UpdatePaths::preferredStagingRoot(installDir, fallback.path());

	QVERIFY(QDir(staging).exists());
	/* Same volume: it shares the installation's parent. */
	QCOMPARE(QFileInfo(staging).absolutePath(), QDir(parent.path()).absolutePath());
	/* And not the fallback, which would mean a cross-volume copy. */
	QVERIFY(!staging.startsWith(QDir(fallback.path()).absolutePath()));
}

void TestUpdatePaths::stagingSurvivesTheInstallationBeingMovedAside()
{
	/* The installer moves the installation aside before activating. Staging
	   inside it therefore destroys itself -- and the move that follows fails
	   with its source gone, which is not a cross-volume problem and must not
	   be mistaken for one.
	
	   This was found by running the helper against a real filesystem, not by
	   any test with a fake one: every other test passes the staging path in
	   explicitly, so none of them could notice where it was chosen. */
	QTemporaryDir parent;
	const QString installDir = parent.path() + QLatin1String("/pe-bear");
	QVERIFY(QDir().mkpath(installDir));
	QTemporaryDir fallback;

	const QString staging = UpdatePaths::preferredStagingRoot(installDir, fallback.path());
	const QString canonicalInstall = QDir(installDir).absolutePath();

	QVERIFY2(!staging.startsWith(canonicalInstall + QLatin1Char('/')),
		qPrintable(QLatin1String("staging is inside the installation: ") + staging));
	QVERIFY(staging != canonicalInstall);

	/* Demonstrated rather than asserted about: move the installation and the
	   staging directory is still there. */
	QVERIFY(QDir().rename(installDir, parent.path() + QLatin1String("/pe-bear.backup")));
	QVERIFY2(QDir(staging).exists(), "the staged tree went with the installation");
}

void TestUpdatePaths::fallsBackWhenTheInstallationIsNotWritable()
{
	QTemporaryDir fallback;
	const QString staging = UpdatePaths::preferredStagingRoot(
		QLatin1String("/this/path/does/not/exist"), fallback.path());

	QVERIFY(staging.startsWith(QDir(fallback.path()).absolutePath()));
}

void TestUpdatePaths::defaultRootIsOutsideTheInstallation()
{
	const QString root = UpdatePaths::defaultRoot();
	QVERIFY(!root.isEmpty());
	QVERIFY(root.endsWith(QLatin1String(UpdatePaths::DIR_NAME)));
}

void TestUpdatePaths::removesADirectoryTreeCompletely()
{
	QTemporaryDir tmp;
	const QString nested = QDir(tmp.path()).absoluteFilePath("a/b/c");
	QVERIFY(QDir().mkpath(nested));

	QFile f(QDir(nested).absoluteFilePath("file.bin"));
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write("x");
	f.close();

	QVERIFY(UpdatePaths::removeRecursively(QDir(tmp.path()).absoluteFilePath("a")));
	QVERIFY(!QDir(QDir(tmp.path()).absoluteFilePath("a")).exists());
	/* removing something that is already gone is not an error */
	QVERIFY(UpdatePaths::removeRecursively(QDir(tmp.path()).absoluteFilePath("a")));
}

void TestUpdatePaths::defaultRootDoesNotDependOnWhichExecutableAsks()
{
	/* PE-bear and pe-bear-updater are two executables that have to agree on
	   this one directory: PE-bear writes the package and the instructions into
	   it, and the helper refuses anything outside it.
	
	   Derived from QCoreApplication::applicationName() -- which is what
	   QStandardPaths::AppLocalDataLocation does -- they would get different
	   answers, and the helper would refuse every real handoff for being in the
	   wrong place. Nothing with a fake filesystem can catch that, because
	   every other test passes UpdatePaths in explicitly; only asking the two
	   names for the same path does. */
	const QString original = QCoreApplication::applicationName();

	QCoreApplication::setApplicationName(QLatin1String("PE-bear"));
	const QString asApplication = UpdatePaths::defaultRoot();

	QCoreApplication::setApplicationName(QLatin1String("pe-bear-updater"));
	const QString asHelper = UpdatePaths::defaultRoot();

	QCoreApplication::setApplicationName(QString());
	const QString asNothing = UpdatePaths::defaultRoot();

	QCoreApplication::setApplicationName(original);

	QVERIFY(!asApplication.isEmpty());
	QCOMPARE(asHelper, asApplication);
	QCOMPARE(asNothing, asApplication);

	/* And it still names PE-bear, so the location people already have does
	   not move. */
	QVERIFY2(asApplication.contains(QLatin1String(UpdatePaths::APPLICATION_DIR_NAME)),
		qPrintable(asApplication));
	QVERIFY2(asApplication.endsWith(QLatin1String(UpdatePaths::DIR_NAME)),
		qPrintable(asApplication));
}

void TestUpdatePaths::parentDirectoryIsAPureStringOperation_data()
{
	QTest::addColumn<QString>("path");
	QTest::addColumn<QString>("parent");

	QTest::newRow("posix")            << QString::fromLatin1("/opt/pe-bear")    << QString::fromLatin1("/opt");
	QTest::newRow("posix nested")     << QString::fromLatin1("/a/b/c")          << QString::fromLatin1("/a/b");
	QTest::newRow("child of root")    << QString::fromLatin1("/opt")            << QString::fromLatin1("/");
	QTest::newRow("trailing slash")   << QString::fromLatin1("/opt/pe-bear/")   << QString::fromLatin1("/opt");
	QTest::newRow("redundant parts")  << QString::fromLatin1("/opt/./pe-bear")  << QString::fromLatin1("/opt");
	QTest::newRow("drive letter")     << QString::fromLatin1("C:/Tools/pe-bear")<< QString::fromLatin1("C:/Tools");
	QTest::newRow("child of drive")   << QString::fromLatin1("C:/pe-bear")      << QString::fromLatin1("C:/");
	/* Native separators are not understood, and that is the promise: the
	   same input must give the same answer on every host, and converting
	   backslashes is a no-op on POSIX. */
	QTest::newRow("backslashes")      << QString::fromLatin1("C:\\Tools\\pe-bear") << QString();
	QTest::newRow("no parent")        << QString::fromLatin1("pe-bear")         << QString();
	QTest::newRow("empty")            << QString()                             << QString();
}

void TestUpdatePaths::parentDirectoryIsAPureStringOperation()
{
	/* This decides *which* directory's writability is checked before an
	   installation is replaced, so it has to mean the same thing everywhere.
	
	   QFileInfo::absolutePath() does not: it resolves against the process's
	   current directory and current drive, so on Windows it answers "C:\opt"
	   for "/opt/pe-bear". That was found by running the suite under MSVC,
	   where it made the installer refuse a target it had just accepted on
	   Linux -- the same expression, a different answer, decided by ambient
	   state. */
	QFETCH(QString, path);
	QFETCH(QString, parent);

	QCOMPARE(parentDirectoryOf(path), parent);

#if defined(Q_OS_WIN)
	/* Windows keeps a leading "//" through cleanPath, so a UNC share's child
	   has the share as its parent. POSIX normalises it to a single slash,
	   which is correct there, so this cannot be a shared expectation. */
	QCOMPARE(parentDirectoryOf(QString::fromLatin1("//srv/share/app")),
		QString::fromLatin1("//srv/share"));
#else
	QCOMPARE(parentDirectoryOf(QString::fromLatin1("//srv/share/app")),
		QString::fromLatin1("/srv/share"));
#endif

#if defined(Q_OS_WIN)
	/* Windows keeps a leading "//" through cleanPath, so a UNC share's child
	   has the share as its parent. POSIX normalises it to a single slash,
	   which is correct there -- so this cannot be a shared expectation, and
	   putting it in the data table would assert Windows semantics on Linux. */
	QCOMPARE(parentDirectoryOf(QString::fromLatin1("//srv/share/app")),
		QString::fromLatin1("//srv/share"));
#else
	QCOMPARE(parentDirectoryOf(QString::fromLatin1("//srv/share/app")),
		QString::fromLatin1("/srv/share"));
#endif

	/* And the same answer regardless of where the process happens to be. */
	const QString before = QDir::currentPath();
	QTemporaryDir elsewhere;
	if (elsewhere.isValid() && QDir::setCurrent(elsewhere.path())) {
		const QString again = parentDirectoryOf(path);
		QDir::setCurrent(before);
		QCOMPARE(again, parent);
	}
}

QTEST_MAIN(TestUpdatePaths)
#include "tst_updatepaths.moc"

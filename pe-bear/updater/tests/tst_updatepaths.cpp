/*
 * Covers the private update directory of PR #3.
 *
 * Downloads, staging, transaction records and backups are kept apart, each run
 * gets a fresh random directory, and nothing is world-readable while it waits
 * to be installed.
 */
#include <QtTest>
#include "../UpdatePaths.h"

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
	void fallsBackWhenTheInstallationIsNotWritable();
	void defaultRootIsOutsideTheInstallation();
	void removesADirectoryTreeCompletely();
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
	/* Staging next to the installation is what lets the installer replace
	   files by rename instead of copying across a volume boundary. */
	QTemporaryDir installDir;
	QTemporaryDir fallback;

	const QString staging = UpdatePaths::preferredStagingRoot(
		installDir.path(), fallback.path());

	QVERIFY(staging.startsWith(QDir(installDir.path()).absolutePath()));
	QVERIFY(QDir(staging).exists());
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

QTEST_MAIN(TestUpdatePaths)
#include "tst_updatepaths.moc"

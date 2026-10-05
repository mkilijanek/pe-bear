/*
 * The installer across a filesystem boundary, on the real filesystem.
 *
 * This suite exists because of a defect the fake-filesystem suites could not
 * have caught, and did not: FakeFileSystem::movePath always succeeds, so
 * nothing in it modelled EXDEV. The installer backed the installation up into
 * the updater's private directory under the user's data location, by rename --
 * and a rename cannot cross a filesystem. Every update of an installation on
 * any other volume therefore failed and rolled back. The staging path already
 * carried a copy fallback for exactly this reason; the backup path did not,
 * and the asymmetry was invisible to every test.
 *
 * So these use RealFileSystem, and the cross-volume case uses two genuinely
 * different filesystems, discovered rather than assumed. Where the host offers
 * only one writable filesystem the case reports a skip, because passing
 * vacuously would be worse than saying it did not run.
 */
#include <QtTest>
#include "../Installer.h"
#include "../DirectoryInstaller.h"
#include "../FileSystem.h"
#include "../TransactionJournal.h"
#include "../UpdatePaths.h"

#if defined(Q_OS_UNIX)
	#include <sys/stat.h>
	#include <unistd.h>
#endif

using namespace pe_bear::updater;

namespace {

	/** An archive described entirely in memory; the bytes land for real. */
	class FakeArchiveReader : public IArchiveReader
	{
	public:
		void addFile(const QString &path, const QByteArray &body)
		{
			ArchiveEntry e(path, ArchiveEntry::KindFile, body.size(), body.size());
			e.isExecutable = path.endsWith(DirectoryInstaller::applicationFileName());
			m_entries.append(e);
			m_bodies.insert(path, body);
		}
		virtual bool open(const QString &) { return true; }
		virtual void close() {}
		virtual QList<ArchiveEntry> entries() const { return m_entries; }
		virtual QByteArray readEntry(const QString &path) { return m_bodies.value(path); }
		virtual QString lastError() const { return QLatin1String("fake reader"); }

	private:
		QList<ArchiveEntry> m_entries;
		QMap<QString, QByteArray> m_bodies;
	};

	QString exeName() { return DirectoryInstaller::applicationFileName(); }

	VerifiedUpdate anUpdate()
	{
		ReleaseAsset asset;
		asset.name = QLatin1String("pkg.tar.xz");
		asset.downloadUrl = QUrl(QLatin1String("https://example.invalid/pkg.tar.xz"));
		asset.size = 4096;
		asset.sha256 = QString(64, QLatin1Char('a'));

		VerifiedUpdate u;
		u.candidate.release.tagName = QLatin1String("v0.7.3");
		u.candidate.release.version = Version::fromString(QLatin1String("0.7.3"));
		u.candidate.asset = asset;
		u.packagePath = QLatin1String("/nonexistent/pkg.tar.xz");
		u.size = asset.size;
		u.sha256 = asset.sha256;
		return u;
	}

	InstallationInfo installationAt(const QString &dir)
	{
		InstallationInfo info;
		info.kind = InstallPortable;
		info.installDir = dir;
		info.executablePath = dir + QLatin1Char('/') + exeName();
		info.writable = true;
		return info;
	}

	/** Device id of @p path, or -1 where the platform cannot say. */
	qint64 deviceOf(const QString &path)
	{
#if defined(Q_OS_UNIX)
		struct stat st;
		if (::stat(QFile::encodeName(path).constData(), &st) != 0) return -1;
		return static_cast<qint64>(st.st_dev);
#else
		Q_UNUSED(path);
		return -1;
#endif
	}

	/**
	 * A writable directory on a different filesystem from @p reference, or an
	 * empty string when the host has none to offer.
	 */
	QString onAnotherVolume(const QString &reference)
	{
		const qint64 here = deviceOf(reference);
		if (here < 0) return QString();

		QStringList candidates;
		candidates << QLatin1String("/dev/shm")
			<< QLatin1String("/run/user/") + QString::number(::geteuid())
			<< QLatin1String("/var/tmp")
			<< QDir::tempPath()
			<< QDir::homePath();

		for (int i = 0; i < candidates.size(); i++) {
			const QString &c = candidates.at(i);
			QFileInfo info(c);
			if (!info.isDir() || !info.isWritable()) continue;
			const qint64 there = deviceOf(c);
			if (there >= 0 && there != here) return c;
		}
		return QString();
	}

}; // namespace

class TestCrossVolumeInstall : public QObject
{
	Q_OBJECT

private slots:
	void theBackupIsKeptBesideTheTarget();
	void installsAnInstallationOnAnotherVolume();
	void committingReclaimsTheStagingTree();
	void theStagingRootGoesWhenItIsEmpty();

private:
	/**
	 * Runs a whole install against the real filesystem.
	 *
	 * @param installRoot  where the installation lives
	 * @param updaterRoot  the updater's private directory
	 */
	Installer::Outcome install(const QString &installRoot, const QString &updaterRoot,
		QString *backupDir, QString *stagingDir, QString *error,
		const QString &forcedStagingRoot = QString());

	RealFileSystem m_fs;
};

Installer::Outcome TestCrossVolumeInstall::install(const QString &installRoot,
		const QString &updaterRoot, QString *backupDir, QString *stagingDir, QString *error,
		const QString &forcedStagingRoot)
{
	const QString installDir = installRoot + QLatin1String("/pe-bear");
	if (!QDir().mkpath(installDir)) {
		if (error) *error = QLatin1String("could not create ") + installDir;
		return Installer::RefusedUntouched;
	}
	/* The build that is about to be replaced, plus a neighbour that proves the
	   whole directory moved rather than parts of it. */
	QFile old(installDir + QLatin1Char('/') + exeName());
	old.open(QIODevice::WriteOnly);
	old.write("the old build");
	old.close();
	QFile tag(installDir + QLatin1String("/notes.tag"));
	tag.open(QIODevice::WriteOnly);
	tag.write("keep me");
	tag.close();

	FakeArchiveReader reader;
	reader.addFile(exeName(), QByteArray("the new build"));
	reader.addFile(QLatin1String("readme.txt"), QByteArray("0.7.3"));

	const UpdatePaths paths(updaterRoot, forcedStagingRoot.isEmpty()
		? UpdatePaths::preferredStagingRoot(installDir, updaterRoot)
		: forcedStagingRoot);
	TransactionJournal journal(&m_fs, paths.transactionsDir());
	if (!journal.prepare()) {
		if (error) *error = journal.lastError();
		return Installer::RefusedUntouched;
	}

	DirectoryInstaller platform(&m_fs, &reader);
	Installer installer(&m_fs, &platform, &journal, paths);
	installer.setInstallation(installationAt(installDir));

	const Installer::Outcome outcome = installer.prepareAndActivate(anUpdate());
	if (backupDir) *backupDir = installer.transaction().record().backupDir;
	if (stagingDir) *stagingDir = installer.transaction().record().stagingDir;
	if (error) *error = installer.lastError();

	if (outcome == Installer::AwaitingValidation) {
		installer.confirmValidated();
	}
	return outcome;
}

void TestCrossVolumeInstall::theBackupIsKeptBesideTheTarget()
{
	/* The property that makes the cross-volume case work at all, asserted
	   directly so it holds on every host and not only where a second
	   filesystem happens to be available. */
	QTemporaryDir installRoot;
	QTemporaryDir updaterRoot;
	QVERIFY(installRoot.isValid() && updaterRoot.isValid());

	QString backupDir, stagingDir, error;
	const Installer::Outcome outcome =
		install(installRoot.path(), updaterRoot.path(), &backupDir, &stagingDir, &error);
	QVERIFY2(outcome == Installer::AwaitingValidation, qPrintable(error));

	QVERIFY(!backupDir.isEmpty());
	const QString target = QDir(installRoot.path()).absoluteFilePath(QLatin1String("pe-bear"));
	QCOMPARE(parentDirectoryOf(backupDir), parentDirectoryOf(target));

	/* And emphatically not under the updater's own directory, which is where
	   it used to go and is what made the rename cross a filesystem. */
	QVERIFY2(!backupDir.startsWith(QDir(updaterRoot.path()).absolutePath()),
		qPrintable(backupDir));

	/* The new build is in place and the old one's neighbour went with it. */
	QFile installed(target + QLatin1Char('/') + exeName());
	QVERIFY(installed.open(QIODevice::ReadOnly));
	QCOMPARE(installed.readAll(), QByteArray("the new build"));
	QVERIFY(!QFile::exists(target + QLatin1String("/notes.tag")));
}

void TestCrossVolumeInstall::installsAnInstallationOnAnotherVolume()
{
	QTemporaryDir updaterRoot;
	QVERIFY(updaterRoot.isValid());

	const QString other = onAnotherVolume(updaterRoot.path());
	if (other.isEmpty()) {
		QSKIP("this host offers no second writable filesystem to test across");
	}

	/* The installation goes on the other filesystem; the updater's private
	   directory stays where it is. Before the fix this combination failed
	   every time, because backing up meant renaming across the boundary. */
	QTemporaryDir installRoot(other + QLatin1String("/pe-bear-xvol-XXXXXX"));
	QVERIFY2(installRoot.isValid(), qPrintable(other));
	QVERIFY(deviceOf(installRoot.path()) != deviceOf(updaterRoot.path()));

	QString backupDir, stagingDir, error;
	const Installer::Outcome outcome =
		install(installRoot.path(), updaterRoot.path(), &backupDir, &stagingDir, &error);

	QVERIFY2(outcome == Installer::AwaitingValidation,
		qPrintable(Installer::outcomeToString(outcome) + QLatin1String(": ") + error));

	const QString target = QDir(installRoot.path()).absoluteFilePath(QLatin1String("pe-bear"));
	QFile installed(target + QLatin1Char('/') + exeName());
	QVERIFY(installed.open(QIODevice::ReadOnly));
	QCOMPARE(installed.readAll(), QByteArray("the new build"));

	/* Both working directories sat on the installation's volume, so nothing
	   had to be copied across. */
	QCOMPARE(deviceOf(parentDirectoryOf(backupDir)), deviceOf(installRoot.path()));
}

void TestCrossVolumeInstall::committingReclaimsTheStagingTree()
{
	/* The leftover this is about only exists when activation had to *copy*
	   rather than move, and that only happens when staging and target sit on
	   different filesystems. With staging beside the target -- the normal case
	   -- activation is a rename and the staging tree is gone either way, so
	   asserting on it there proves nothing. The first version of this test did
	   exactly that and passed with the fix reverted.
	
	   So the staging root is forced onto the updater's volume while the
	   installation lives on another one, which is the shape UpdatePaths falls
	   back to when an installation has no usable parent. */
	QTemporaryDir updaterRoot;
	QVERIFY(updaterRoot.isValid());

	const QString other = onAnotherVolume(updaterRoot.path());
	if (other.isEmpty()) {
		QSKIP("this host offers no second writable filesystem to test across");
	}

	QTemporaryDir installRoot(other + QLatin1String("/pe-bear-stage-XXXXXX"));
	QVERIFY2(installRoot.isValid(), qPrintable(other));

	const QString forcedStaging = QDir(updaterRoot.path())
		.absoluteFilePath(QLatin1String("staging"));

	QString backupDir, stagingDir, error;
	const Installer::Outcome outcome = install(installRoot.path(), updaterRoot.path(),
		&backupDir, &stagingDir, &error, forcedStaging);
	QVERIFY2(outcome == Installer::AwaitingValidation,
		qPrintable(Installer::outcomeToString(outcome) + QLatin1String(": ") + error));

	/* Confirm the copy path really was taken, or this test is back to proving
	   nothing: the staged tree is on the other volume from the target. */
	QVERIFY(deviceOf(parentDirectoryOf(stagingDir)) != deviceOf(installRoot.path()));

	/* install() commits on success. Neither working directory may survive it:
	   the backup is a full copy of the previous build, and after a copy
	   activation the staging tree is a full copy of the new one. */
	QVERIFY2(!QDir(backupDir).exists(), qPrintable(backupDir));
	QVERIFY2(!QDir(stagingDir).exists(), qPrintable(stagingDir));

	/* The installation is still the new build. */
	const QString target = QDir(installRoot.path()).absoluteFilePath(QLatin1String("pe-bear"));
	QFile installed(target + QLatin1Char('/') + exeName());
	QVERIFY(installed.open(QIODevice::ReadOnly));
	QCOMPARE(installed.readAll(), QByteArray("the new build"));
}

void TestCrossVolumeInstall::theStagingRootGoesWhenItIsEmpty()
{
	/* It sits next to the installation -- somebody else's directory -- so an
	   empty one left behind after every update is litter in a shared place. */
	QTemporaryDir installRoot;
	QTemporaryDir updaterRoot;
	QVERIFY(installRoot.isValid() && updaterRoot.isValid());

	QString backupDir, stagingDir, error;
	const Installer::Outcome outcome =
		install(installRoot.path(), updaterRoot.path(), &backupDir, &stagingDir, &error);
	QVERIFY2(outcome == Installer::AwaitingValidation, qPrintable(error));

	const QString stagingRoot = parentDirectoryOf(stagingDir);
	QVERIFY2(!QDir(stagingRoot).exists(), qPrintable(stagingRoot));
}

QTEST_GUILESS_MAIN(TestCrossVolumeInstall)
#include "tst_crossvolumeinstall.moc"

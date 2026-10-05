/*
 * Covers the only concrete PlatformInstaller: the one that replaces an
 * installation which is a single self-contained directory.
 *
 * The executable's name is taken from the class rather than written out, so
 * these run unchanged on all three platforms -- on macOS the bundle layout is
 * a second candidate and a build with the binary at the root still matches.
 *
 * What is deliberately *not* covered here is the Windows-specific failure this
 * class cannot avoid: another process holding a handle inside the target and
 * making the move fail. The nearest thing a fake can express -- a move that is
 * refused -- is covered, and the real behaviour belongs to a machine that can
 * produce a sharing violation.
 */
#include <QtTest>
#include "../DirectoryInstaller.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	/** An archive described entirely in memory. */
	class FakeArchiveReader : public IArchiveReader
	{
	public:
		FakeArchiveReader() : m_openOk(true) {}

		void addFile(const QString &path, const QByteArray &body)
		{
			m_entries.append(ArchiveEntry(path, ArchiveEntry::KindFile, body.size(), body.size()));
			m_bodies.insert(path, body);
		}
		void addDir(const QString &path)
		{
			m_entries.append(ArchiveEntry(path, ArchiveEntry::KindDir));
		}
		void setOpenFails() { m_openOk = false; }

		virtual bool open(const QString &) { return m_openOk; }
		virtual void close() {}
		virtual QList<ArchiveEntry> entries() const { return m_entries; }
		virtual QByteArray readEntry(const QString &path) { return m_bodies.value(path); }
		virtual QString lastError() const { return QLatin1String("fake reader"); }

	private:
		QList<ArchiveEntry> m_entries;
		QMap<QString, QByteArray> m_bodies;
		bool m_openOk;
	};

	const char* PKG     = "/u/.pe-bear/updates/downloads/r/pkg.zip";
	const char* STAGING = "/u/.pe-bear/updates/staging/tx-1";
	const char* TARGET  = "/opt/pe-bear";

	QString exeName() { return DirectoryInstaller::applicationFileName(); }

	VerifiedUpdate anUpdate()
	{
		ReleaseAsset asset;
		asset.name = QLatin1String("pkg.zip");
		asset.downloadUrl = QUrl(QLatin1String("https://example.invalid/pkg.zip"));
		asset.size = 2048;
		asset.sha256 = QString(64, QLatin1Char('c'));

		ReleaseInfo release;
		release.tagName = QLatin1String("v0.7.3");
		release.version = Version::fromString(QLatin1String("0.7.3"));

		VerifiedUpdate u;
		u.candidate.release = release;
		u.candidate.asset = asset;
		u.packagePath = QLatin1String(PKG);
		u.size = asset.size;
		u.sha256 = asset.sha256;
		return u;
	}

	InstallationInfo aPortableInstall()
	{
		InstallationInfo info;
		info.kind = InstallPortable;
		info.installDir = QLatin1String(TARGET);
		info.executablePath = QLatin1String(TARGET) + QLatin1Char('/') + exeName();
		info.writable = true;
		return info;
	}

}; // namespace

class TestDirectoryInstaller : public QObject
{
	Q_OBJECT

private slots:
	void init();

	void acceptsAWritablePortableInstall();
	void refusesWhatTheDetectorSaysIsNotUpdatable();
	void refusesWhatTheDetectorSaysIsNotUpdatable_data();
	void refusesAnInstallWithNoExecutable();
	void refusesAFilesystemRoot();
	void refusesAnInstallDirectoryThatIsNotADirectory();
	void refusesAWritableDirectoryInsideAReadOnlyParent();

	void stagesAnArchiveWhoseFilesAreAtTheRoot();
	void unwrapsASingleTopLevelDirectory();
	void refusesTwoTopLevelDirectories();
	void refusesAnArchiveWithoutTheExecutable();
	void refusesAWrapperWithoutTheExecutable();
	void keepsTheStepsOfAFailedExtraction();
	void refusesToStageWithoutADecoder();

	void verifyRefusesBeforeAnythingIsStaged();
	void verifyRefusesAnEmptyExecutable();
	void verifyAcceptsAStagedBuild();

	void activateMovesTheStagedBuildIntoPlace();
	void activateRefusesWhenTheTargetIsStillThere();
	void activateFallsBackToCopyingWhenTheMoveIsRefused();
	void activateReportsBothFailuresWhenTheFallbackAlsoFails();
	void activateRefusesWhenTheStagedBuildHasVanished();

	void namesThePrimaryCandidateWhenNothingIsThere();

private:
	FakeFileSystem m_fs;
	FakeArchiveReader m_reader;
};

void TestDirectoryInstaller::init()
{
	m_fs = FakeFileSystem();
	m_reader = FakeArchiveReader();
	m_fs.setWritableDefault(true);
	m_fs.addFile(QLatin1String(PKG), QByteArray("package bytes"));
}

void TestDirectoryInstaller::acceptsAWritablePortableInstall()
{
	m_fs.addFile(QLatin1String(TARGET) + QLatin1Char('/') + exeName(), QByteArray("elf"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QString why;
	QVERIFY2(installer.canInstall(aPortableInstall(), &why), qPrintable(why));
	QCOMPARE(installer.name(), QLatin1String("directory"));
}

void TestDirectoryInstaller::refusesWhatTheDetectorSaysIsNotUpdatable_data()
{
	QTest::addColumn<int>("kind");
	QTest::addColumn<bool>("writable");

	QTest::newRow("managed, writable")   << int(InstallManaged)  << true;
	QTest::newRow("unknown, writable")   << int(InstallUnknown)  << true;
	QTest::newRow("portable, read-only") << int(InstallPortable) << false;
	QTest::newRow("system, read-only")   << int(InstallSystem)   << false;
	/* Writable and still refused: the directory is somebody's Desktop. */
	QTest::newRow("user folder, writable") << int(InstallUserFolder) << true;
}

void TestDirectoryInstaller::refusesWhatTheDetectorSaysIsNotUpdatable()
{
	QFETCH(int, kind);
	QFETCH(bool, writable);

	m_fs.addFile(QLatin1String(TARGET) + QLatin1Char('/') + exeName(), QByteArray("elf"));

	InstallationInfo info = aPortableInstall();
	info.kind = static_cast<InstallationKind>(kind);
	info.writable = writable;

	DirectoryInstaller installer(&m_fs, &m_reader);
	QString why;
	QVERIFY(!installer.canInstall(info, &why));
	QVERIFY(!why.isEmpty());
}

void TestDirectoryInstaller::refusesAFilesystemRoot()
{
	/* The detector refuses these too; this is the belt to its braces, since
	   the two are reached by different callers and a root must be refused by
	   whichever one asks. A root has no parent to move it within. */
	const QString root = QLatin1String("/");
	m_fs.addDir(root);
	m_fs.addFile(root + exeName(), QByteArray("elf"));

	InstallationInfo info = aPortableInstall();
	info.installDir = root;
	info.executablePath = root + exeName();

	DirectoryInstaller installer(&m_fs, &m_reader);
	QString why;
	QVERIFY(!installer.canInstall(info, &why));
	QVERIFY2(why.contains(QLatin1String("filesystem root")), qPrintable(why));
}

void TestDirectoryInstaller::refusesAnInstallWithNoExecutable()
{
	/* The directory is there and writable; it is simply not PE-bear. */
	m_fs.addDir(QLatin1String(TARGET));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QString why;
	QVERIFY(!installer.canInstall(aPortableInstall(), &why));
	QVERIFY(why.contains(exeName()));
}

void TestDirectoryInstaller::refusesAnInstallDirectoryThatIsNotADirectory()
{
	m_fs.addFile(QLatin1String(TARGET), QByteArray("a file, not a directory"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QString why;
	QVERIFY(!installer.canInstall(aPortableInstall(), &why));
}

void TestDirectoryInstaller::refusesAWritableDirectoryInsideAReadOnlyParent()
{
	/* Activation replaces the directory by moving it, which is a write to the
	   parent. A check that only looked at the directory itself would pass here
	   and then fail at the one step that has already moved the old build
	   aside. */
	m_fs.addFile(QLatin1String(TARGET) + QLatin1Char('/') + exeName(), QByteArray("elf"));
	m_fs.setWritable(QLatin1String("/opt"), false);

	DirectoryInstaller installer(&m_fs, &m_reader);
	QString why;
	QVERIFY(!installer.canInstall(aPortableInstall(), &why));
	QVERIFY(why.contains(QLatin1String("parent")));
}

void TestDirectoryInstaller::stagesAnArchiveWhoseFilesAreAtTheRoot()
{
	m_reader.addFile(exeName(), QByteArray("elf bytes"));
	m_reader.addFile(QLatin1String("readme.txt"), QByteArray("hello"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY2(installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops),
		qPrintable(installer.lastError()));

	QCOMPARE(installer.stagedRoot(), QLatin1String(STAGING));
	QVERIFY(!ops.isEmpty());
	QVERIFY(m_fs.hasFile(QLatin1String(STAGING) + QLatin1Char('/') + exeName()));
}

void TestDirectoryInstaller::unwrapsASingleTopLevelDirectory()
{
	/* Both shapes are published, so both have to work. */
	m_reader.addDir(QLatin1String("PE-bear-0.7.3"));
	m_reader.addFile(QLatin1String("PE-bear-0.7.3/") + exeName(), QByteArray("elf bytes"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY2(installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops),
		qPrintable(installer.lastError()));

	QCOMPARE(installer.stagedRoot(),
		QLatin1String(STAGING) + QLatin1String("/PE-bear-0.7.3"));
}

void TestDirectoryInstaller::refusesTwoTopLevelDirectories()
{
	/* Which of the two is the build? Guessing installs the wrong one. */
	m_reader.addFile(QLatin1String("a/") + exeName(), QByteArray("elf"));
	m_reader.addFile(QLatin1String("b/") + exeName(), QByteArray("elf"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(!installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));
	QVERIFY(installer.stagedRoot().isEmpty());
}

void TestDirectoryInstaller::refusesAnArchiveWithoutTheExecutable()
{
	/* Verified by digest and still not PE-bear. */
	m_reader.addFile(QLatin1String("notes.txt"), QByteArray("not a build"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(!installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));
	QVERIFY(installer.lastError().contains(exeName()));
	/* The files were written, so they must be undoable. */
	QVERIFY(!ops.isEmpty());
}

void TestDirectoryInstaller::refusesAWrapperWithoutTheExecutable()
{
	m_reader.addFile(QLatin1String("wrapper/notes.txt"), QByteArray("not a build"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(!installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));
}

void TestDirectoryInstaller::keepsTheStepsOfAFailedExtraction()
{
	m_reader.addFile(exeName(), QByteArray("elf bytes"));
	m_reader.addFile(QLatin1String("data.bin"), QByteArray("more"));
	m_fs.failOnCall(QLatin1String("writeFile"), 2);

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(!installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));
	/* Whatever landed has to be recorded, or rollback leaves it behind. */
	QVERIFY(!ops.isEmpty());
}

void TestDirectoryInstaller::refusesToStageWithoutADecoder()
{
	/* A build without libarchive. Refusing clearly beats crashing in the
	   middle of staging. */
	DirectoryInstaller installer(&m_fs, NULL);
	QList<TransactionOp> ops;
	QVERIFY(!installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));
	QVERIFY(installer.lastError().contains(QLatin1String("libarchive")));
	QVERIFY(ops.isEmpty());
}

void TestDirectoryInstaller::verifyRefusesBeforeAnythingIsStaged()
{
	DirectoryInstaller installer(&m_fs, &m_reader);
	QString why;
	QVERIFY(!installer.verifyStagedLayout(QLatin1String(STAGING), &why));
	QVERIFY(!why.isEmpty());
}

void TestDirectoryInstaller::verifyRefusesAnEmptyExecutable()
{
	m_reader.addFile(exeName(), QByteArray());

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));

	QString why;
	QVERIFY(!installer.verifyStagedLayout(QLatin1String(STAGING), &why));
	QVERIFY(why.contains(QLatin1String("empty")));
}

void TestDirectoryInstaller::verifyAcceptsAStagedBuild()
{
	m_reader.addFile(exeName(), QByteArray("elf bytes"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));

	QString why;
	QVERIFY2(installer.verifyStagedLayout(QLatin1String(STAGING), &why), qPrintable(why));
}

void TestDirectoryInstaller::activateMovesTheStagedBuildIntoPlace()
{
	m_reader.addFile(exeName(), QByteArray("elf bytes"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));

	QList<TransactionOp> activationOps;
	QVERIFY2(installer.activate(QLatin1String(STAGING), QLatin1String(TARGET), &activationOps),
		qPrintable(installer.lastError()));

	QCOMPARE(activationOps.size(), 1);
	QCOMPARE(activationOps.first().kind, TransactionOp::OpMoved);
	QCOMPARE(activationOps.first().to, QLatin1String(TARGET));
	QVERIFY(m_fs.hasFile(QLatin1String(TARGET) + QLatin1Char('/') + exeName()));
}

void TestDirectoryInstaller::activateRefusesWhenTheTargetIsStillThere()
{
	/* The transaction moves the old build aside first. Something still there
	   means the record and the disk disagree, and writing into it would merge
	   two builds into one directory. */
	m_reader.addFile(exeName(), QByteArray("elf bytes"));
	m_fs.addFile(QLatin1String(TARGET) + QLatin1Char('/') + QLatin1String("old"), QByteArray("x"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));

	QList<TransactionOp> activationOps;
	QVERIFY(!installer.activate(QLatin1String(STAGING), QLatin1String(TARGET), &activationOps));
	QVERIFY(installer.lastError().contains(QLatin1String("still exists")));
	QVERIFY(activationOps.isEmpty());
}

void TestDirectoryInstaller::activateFallsBackToCopyingWhenTheMoveIsRefused()
{
	/* Staging can land on another filesystem when the installation directory
	   is not writable and UpdatePaths falls back to the private root. Without
	   this path the update would simply never work there. */
	m_reader.addFile(exeName(), QByteArray("elf bytes"));
	m_reader.addFile(QLatin1String("lib/extra.so"), QByteArray("so"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));

	m_fs.failAlways(QLatin1String("movePath"));

	QList<TransactionOp> activationOps;
	QVERIFY2(installer.activate(QLatin1String(STAGING), QLatin1String(TARGET), &activationOps),
		qPrintable(installer.lastError()));

	QCOMPARE(activationOps.size(), 1);
	QCOMPARE(activationOps.first().kind, TransactionOp::OpCreatedDir);
	QCOMPARE(activationOps.first().from, QLatin1String(TARGET));
	QVERIFY(m_fs.hasFile(QLatin1String(TARGET) + QLatin1Char('/') + exeName()));
	QVERIFY(m_fs.hasFile(QLatin1String(TARGET) + QLatin1String("/lib/extra.so")));
}

void TestDirectoryInstaller::activateReportsBothFailuresWhenTheFallbackAlsoFails()
{
	m_reader.addFile(exeName(), QByteArray("elf bytes"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));

	m_fs.failAlways(QLatin1String("movePath"));
	m_fs.failAlways(QLatin1String("copyFile"));

	QList<TransactionOp> activationOps;
	QVERIFY(!installer.activate(QLatin1String(STAGING), QLatin1String(TARGET), &activationOps));
	/* Both are named: the move told us why the cheap path was unavailable,
	   and without it the message would only say the copy failed. */
	QVERIFY(installer.lastError().contains(QLatin1String("move failed")));
	QVERIFY(installer.lastError().contains(QLatin1String("copy fallback")));
	/* The partially created directory is still recorded. */
	QCOMPARE(activationOps.size(), 1);
	QCOMPARE(activationOps.first().kind, TransactionOp::OpCreatedDir);
}

void TestDirectoryInstaller::activateRefusesWhenTheStagedBuildHasVanished()
{
	/* The copy fallback used to "succeed" here: it created the destination,
	   found nothing to iterate, and reported an empty directory as the new
	   build. That is the one failure shape no later step can tell from a
	   finished install, and it is exactly what happened when the staged tree
	   was destroyed by the backup step. */
	m_reader.addFile(exeName(), QByteArray("elf bytes"));

	DirectoryInstaller installer(&m_fs, &m_reader);
	QList<TransactionOp> ops;
	QVERIFY(installer.prepareStaging(anUpdate(), QLatin1String(STAGING), &ops));

	/* The staged tree disappears between staging and activation. */
	QVERIFY(m_fs.removeDirRecursively(installer.stagedRoot()));

	QList<TransactionOp> activationOps;
	QVERIFY2(!installer.activate(QLatin1String(STAGING), QLatin1String(TARGET), &activationOps),
		"an empty directory was reported as a successful activation");
	QVERIFY2(installer.lastError().contains(QLatin1String("no longer")),
		qPrintable(installer.lastError()));
	/* And nothing was left at the target pretending to be a build. */
	QVERIFY(!m_fs.hasFile(QLatin1String(TARGET) + QLatin1Char('/') + exeName()));
}

void TestDirectoryInstaller::namesThePrimaryCandidateWhenNothingIsThere()
{
	DirectoryInstaller installer(&m_fs, &m_reader);
	const QString named = installer.executablePathIn(QLatin1String("/nowhere"));
	QVERIFY(!named.isEmpty());
	QVERIFY(named.startsWith(QLatin1String("/nowhere/")));
	QVERIFY(named.endsWith(exeName()));
}

QTEST_MAIN(TestDirectoryInstaller)
#include "tst_directoryinstaller.moc"

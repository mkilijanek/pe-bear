/*
 * Covers the installation ordering of #20.
 *
 * The platform step is a fake, so every failure point can be exercised on any
 * host. What is being tested is not Windows behaviour but the order of the
 * steps and what survives each failure -- which is where the damage lives if
 * it is wrong, and which no amount of testing on a real Windows box would
 * cover more thoroughly than this.
 */
#include <QtTest>
#include "../Installer.h"
#include "../DirectoryInstaller.h"
#include "../Recovery.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	const char* DIGEST = "c9aeff95175eed76d47a8dad7d20d531fd1f318ccd202220841813a55e21e4fe";
	const char* TARGET = "/opt/pe-bear";
	const char* ROOT   = "/var/pe-bear-updates";

	/** Records what it was asked to do, and can refuse any of it. */
	class FakePlatform : public PlatformInstaller
	{
	public:
		FakePlatform(FakeFileSystem *fs)
			: m_fs(fs), m_canInstall(true), m_stageOk(true), m_layoutOk(true),
			m_activateOk(true) {}

		void refuseCanInstall(const QString &why) { m_canInstall = false; m_why = why; }
		void failStaging() { m_stageOk = false; }
		void failLayout() { m_layoutOk = false; }
		void failActivate() { m_activateOk = false; }
		/** Copies whatever is in @p dir when activate() is entered. */
		void snapshotOnActivate(const QString &dir) { m_snapshotDir = dir; }
		QMap<QString, QByteArray> snapshot() const { return m_snapshot; }

		QStringList calls() const { return m_calls; }

		virtual QString name() const { return QLatin1String("fake"); }

		virtual bool canInstall(const InstallationInfo &, QString *reason) const
		{
			m_calls << QLatin1String("canInstall");
			if (!m_canInstall) { if (reason) *reason = m_why; return false; }
			return true;
		}

		virtual bool prepareStaging(const VerifiedUpdate &, const QString &stagingDir,
			QList<TransactionOp> *ops)
		{
			m_calls << QLatin1String("prepareStaging");
			if (!m_stageOk) { m_error = QLatin1String("staging refused"); return false; }
			/* a plausible staged tree, so later steps have something real */
			m_fs->addDir(stagingDir);
			m_fs->addFile(stagingDir + QLatin1String("/PE-bear"), QByteArray("new-binary"));
			if (ops) {
				ops->append(TransactionOp(TransactionOp::OpCreatedDir, stagingDir));
				ops->append(TransactionOp(TransactionOp::OpCreated,
					stagingDir + QLatin1String("/PE-bear")));
			}
			return true;
		}

		virtual bool verifyStagedLayout(const QString &, QString *reason) const
		{
			m_calls << QLatin1String("verifyStagedLayout");
			if (!m_layoutOk) { if (reason) *reason = QLatin1String("no executable found"); return false; }
			return true;
		}

		virtual bool activate(const QString &stagingDir, const QString &targetDir,
			QList<TransactionOp> *ops)
		{
			m_calls << QLatin1String("activate");
			if (!m_snapshotDir.isEmpty()) {
				foreach (const QString &name, m_fs->listDir(m_snapshotDir)) {
					const QString path = m_snapshotDir + QLatin1Char('/') + name;
					m_snapshot.insert(path, m_fs->readFile(path));
				}
			}
			if (!m_activateOk) { m_error = QLatin1String("activate refused"); return false; }
			if (!m_fs->movePath(stagingDir, targetDir)) {
				m_error = QLatin1String("move failed");
				return false;
			}
			if (ops) ops->append(TransactionOp(TransactionOp::OpMoved, stagingDir, targetDir));
			return true;
		}

		virtual QString executablePathIn(const QString &dir) const
		{
			return dir + QLatin1String("/PE-bear");
		}

		virtual QString lastError() const { return m_error; }

	private:
		FakeFileSystem *m_fs;
		bool m_canInstall, m_stageOk, m_layoutOk, m_activateOk;
		QString m_why;
		QString m_snapshotDir;
		QMap<QString, QByteArray> m_snapshot;
		mutable QString m_error;
		mutable QStringList m_calls;
	};

	/** An archive described entirely in memory. */
	class FakeArchiveReader : public IArchiveReader
	{
	public:
		void addFile(const QString &path, const QByteArray &body)
		{
			m_entries.append(ArchiveEntry(path, ArchiveEntry::KindFile, body.size(), body.size()));
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

	VerifiedUpdate update()
	{
		VerifiedUpdate u;
		u.candidate.release.tagName = QLatin1String("v0.7.3");
		u.candidate.release.version = Version::fromString(QLatin1String("0.7.3"));
		u.candidate.asset.name = QLatin1String("PE-bear_0.7.3_qt6_x64_win_vs22.zip");
		u.candidate.asset.downloadUrl =
			QUrl(QLatin1String("https://github.com/hasherezade/pe-bear/releases/download/v0.7.3/x.zip"));
		u.candidate.asset.size = 2048;
		u.candidate.asset.sha256 = QLatin1String(DIGEST);
		u.packagePath = QLatin1String("/var/pe-bear-updates/downloads/x.zip");
		u.size = 2048;
		u.sha256 = QLatin1String(DIGEST);
		return u;
	}

	InstallationInfo portableAt(const QString &dir)
	{
		InstallationInfo i;
		i.kind = InstallPortable;
		i.installDir = dir;
		i.writable = true;
		return i;
	}

	void installTarget(FakeFileSystem &fs)
	{
		fs.addDir(QLatin1String(TARGET));
		fs.addFile(QLatin1String("/opt/pe-bear/PE-bear"), QByteArray("old-binary"));
		fs.addFile(QLatin1String("/opt/pe-bear/SIG.txt"), QByteArray("old-sigs"));
	}

	UpdatePaths paths()
	{
		return UpdatePaths(QLatin1String(ROOT), QLatin1String("/var/pe-bear-updates/staging"));
	}

}; // namespace

class TestInstaller : public QObject
{
	Q_OBJECT

private slots:
	void theHappyPathActivatesAndAwaitsValidation();
	void stepsRunInTheDocumentedOrder();
	void stagingHappensBeforeTheInstallationIsTouched();
	void layoutIsCheckedBeforeTheSwap();

	void refusesAManagedInstallationWithoutTouchingAnything();
	void refusesAnUnverifiedUpdate();
	void refusesWhenTheInstallationDirectoryIsUnknown();

	void aFailedStagingLeavesTheInstallationIntact();
	void aFailedLayoutCheckLeavesTheInstallationIntact();
	void aFailedActivationRestoresTheInstallation();
	void anIncompleteRollbackIsReportedDistinctly();
	void aCopyFallbackThatFailsHalfwayRestoresTheInstallation();
	void activationIsRecordedBeforeItHappens();

	void theRecordIsWrittenBeforeTheStagingRootExists();
	void aRootThatCannotBeCreatedClosesTheRecord();
	void aRefusedJournalLeavesNoStagingRoot();
	void aStagingRootThatIsNotPrivateIsNotUsed();

	void confirmValidatedCommitsAndDropsTheBackup();
	void confirmValidatedRefusedBeforeActivation();
	void rollBackAfterActivationRestoresTheOldBuild();
	void installedExecutablePathPointsAtTheTarget();
};

void TestInstaller::theHappyPathActivatesAndAwaitsValidation()
{
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QCOMPARE(inst.prepareAndActivate(update()), Installer::AwaitingValidation);
	QCOMPARE(inst.transaction().state(), TxActivated);
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("new-binary"));
	/* the record carries the root, so any path that ends this run can
	   reclaim it without re-deriving it by naming convention */
	QCOMPARE(inst.transaction().record().stagingRoot,
		QString(QLatin1String("/var/pe-bear-updates/staging")));
}

void TestInstaller::stepsRunInTheDocumentedOrder()
{
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QCOMPARE(inst.prepareAndActivate(update()), Installer::AwaitingValidation);
	QCOMPARE(platform.calls(), QStringList()
		<< "canInstall" << "prepareStaging" << "verifyStagedLayout" << "activate");
}

void TestInstaller::stagingHappensBeforeTheInstallationIsTouched()
{
	/* A package that will not unpack must cost nothing. */
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	platform.failStaging();
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QCOMPARE(inst.prepareAndActivate(update()), Installer::FailedRolledBack);
	/* activate was never reached, and the old build is untouched */
	QVERIFY(!platform.calls().contains(QLatin1String("activate")));
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
}

void TestInstaller::layoutIsCheckedBeforeTheSwap()
{
	/* A digest proves the bytes, not that they are the right program. Finding
	   that out after the installation has moved would be strictly worse. */
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	platform.failLayout();
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QCOMPARE(inst.prepareAndActivate(update()), Installer::FailedRolledBack);
	QVERIFY(!platform.calls().contains(QLatin1String("activate")));
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
	QVERIFY(fs.hasFile(QLatin1String("/opt/pe-bear/SIG.txt")));
}

void TestInstaller::refusesAManagedInstallationWithoutTouchingAnything()
{
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	platform.refuseCanInstall(QLatin1String("installed under /usr"));
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());

	InstallationInfo managed;
	managed.kind = InstallManaged;
	managed.installDir = QLatin1String("/usr/bin");
	inst.setInstallation(managed);

	QCOMPARE(inst.prepareAndActivate(update()), Installer::RefusedUntouched);
	/* no transaction was even opened */
	QVERIFY(journal.listIds().isEmpty());
	QVERIFY(fs.hasFile(QLatin1String("/opt/pe-bear/PE-bear")));
	QVERIFY(inst.lastError().contains(QLatin1String("/usr")));
}

void TestInstaller::refusesAnUnverifiedUpdate()
{
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	VerifiedUpdate broken = update();
	broken.sha256.clear();
	QCOMPARE(inst.prepareAndActivate(broken), Installer::RefusedUntouched);
	QVERIFY(journal.listIds().isEmpty());
	/* canInstall was never even asked */
	QVERIFY(platform.calls().isEmpty());
}

void TestInstaller::refusesWhenTheInstallationDirectoryIsUnknown()
{
	FakeFileSystem fs;
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());

	InstallationInfo nowhere;
	nowhere.kind = InstallPortable;
	nowhere.writable = true; /* but installDir is empty */
	inst.setInstallation(nowhere);

	QCOMPARE(inst.prepareAndActivate(update()), Installer::RefusedUntouched);
	QVERIFY(journal.listIds().isEmpty());
}

void TestInstaller::aFailedStagingLeavesTheInstallationIntact()
{
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	platform.failStaging();
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QCOMPARE(inst.prepareAndActivate(update()), Installer::FailedRolledBack);
	QCOMPARE(inst.transaction().state(), TxRolledBack);
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
}

void TestInstaller::aFailedLayoutCheckLeavesTheInstallationIntact()
{
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	platform.failLayout();
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QCOMPARE(inst.prepareAndActivate(update()), Installer::FailedRolledBack);
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
	/* and the staging tree it created is gone too, root and all: the root
	   is reclaimed when empty, which after undoing the only staged tree it
	   always is here */
	QVERIFY2(!fs.hasDir(QLatin1String("/var/pe-bear-updates/staging")),
		"the staged tree outlived the rollback");
}

void TestInstaller::aFailedActivationRestoresTheInstallation()
{
	/* The hard case: the installation has already moved aside. */
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	platform.failActivate();
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QCOMPARE(inst.prepareAndActivate(update()), Installer::FailedRolledBack);
	QCOMPARE(inst.transaction().state(), TxRolledBack);
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/SIG.txt")), QByteArray("old-sigs"));
}

void TestInstaller::anIncompleteRollbackIsReportedDistinctly()
{
	/* "Restored" and "a person needs to look" must never be collapsed. */
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	platform.failActivate();
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	/* The backup must succeed and only the restore fail, otherwise the
	   backup move never happens and there is nothing left to fail at.
	   movePath call 1 is the backup; call 2 is the restore during rollback --
	   activation is refused before it moves anything. */
	fs.failOnCall(QLatin1String("movePath"), 2);
	QCOMPARE(inst.prepareAndActivate(update()), Installer::FailedNeedsAttention);
	QCOMPARE(inst.transaction().state(), TxFailed);
	QVERIFY(!inst.lastError().isEmpty());
}

void TestInstaller::confirmValidatedCommitsAndDropsTheBackup()
{
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QCOMPARE(inst.prepareAndActivate(update()), Installer::AwaitingValidation);
	QVERIFY2(inst.confirmValidated(), qPrintable(inst.lastError()));
	QCOMPARE(inst.transaction().state(), TxCommitted);
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("new-binary"));

	const QString backup = inst.transaction().record().backupDir;
	QVERIFY(!backup.isEmpty());
	QVERIFY2(!fs.hasDir(backup), "the backup outlived the commit");
	/* the staging root went too: it sits beside the installation, and an
	   empty one is litter after a finished update */
	QVERIFY2(!fs.hasDir(QLatin1String("/var/pe-bear-updates/staging")),
		"an empty staging root outlived the commit");
}

void TestInstaller::confirmValidatedRefusedBeforeActivation()
{
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QVERIFY2(!inst.confirmValidated(), "committed without ever activating");
}

void TestInstaller::rollBackAfterActivationRestoresTheOldBuild()
{
	/* What happens when the new build starts but fails its handshake. */
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QCOMPARE(inst.prepareAndActivate(update()), Installer::AwaitingValidation);
	QCOMPARE(inst.rollBack(QLatin1String("the new build did not confirm itself")),
		Installer::FailedRolledBack);
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/SIG.txt")), QByteArray("old-sigs"));
}

void TestInstaller::aCopyFallbackThatFailsHalfwayRestoresTheInstallation()
{
	/* The real installer, on the path a cross-volume update takes: the move
	   of the staged build into place is refused, the copy fallback creates
	   the target and dies on its second file. What it created is the only
	   thing standing between the backup and its old place, so unless the
	   transaction knows about it the restore cannot happen. */
	FakeFileSystem fs;
	installTarget(fs);
	/* DirectoryInstaller looks for the platform's own name: PE-bear.exe on
	   Windows, which installTarget() does not lay down. */
	const QString oldExe = QLatin1String(TARGET) + QLatin1Char('/')
		+ DirectoryInstaller::applicationFileName();
	fs.addFile(oldExe, QByteArray("old-binary"));
	fs.setWritableDefault(true);
	fs.addFile(QLatin1String("/var/pe-bear-updates/downloads/x.zip"), QByteArray("package"));

	FakeArchiveReader reader;
	reader.addFile(DirectoryInstaller::applicationFileName(), QByteArray("new-binary"));
	reader.addFile(QLatin1String("SIG.txt"), QByteArray("new-sigs"));
	DirectoryInstaller platform(&fs, &reader);

	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	/* movePath #1 is the backup, #2 the activation; copyFile #2 is the
	   second file of the fallback. */
	fs.failOnCall(QLatin1String("movePath"), 2);
	fs.failOnCall(QLatin1String("copyFile"), 2);

	QCOMPARE(inst.prepareAndActivate(update()), Installer::FailedRolledBack);
	QCOMPARE(inst.transaction().state(), TxRolledBack);
	QCOMPARE(fs.contentOf(oldExe), QByteArray("old-binary"));
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/SIG.txt")), QByteArray("old-sigs"));
	QVERIFY(!fs.exists(inst.transaction().record().backupDir));
}

void TestInstaller::activationIsRecordedBeforeItHappens()
{
	/* A helper that dies after the new build is in place but before the
	   activation is recorded leaves the journal exactly as it was when
	   activate() was entered. Recovery then has to be able to put the old
	   build back from that record alone. */
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	platform.snapshotOnActivate(paths().transactionsDir());
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QCOMPARE(inst.prepareAndActivate(update()), Installer::AwaitingValidation);
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("new-binary"));

	/* the crash: the journal goes back to what it said during activate() */
	const QMap<QString, QByteArray> before = platform.snapshot();
	QVERIFY(!before.isEmpty());
	QMap<QString, QByteArray>::const_iterator itr;
	for (itr = before.begin(); itr != before.end(); ++itr) {
		QVERIFY(fs.writeFile(itr.key(), itr.value()));
	}

	Recovery recovery(&fs, &journal);
	recovery.setNow(QDateTime::currentDateTimeUtc().addDays(1));
	const QList<Recovery::Outcome> outcomes = recovery.run();
	QCOMPARE(outcomes.size(), 1);
	QVERIFY2(outcomes.first().disposition == Recovery::RolledBack,
		qPrintable(outcomes.first().note));
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/SIG.txt")), QByteArray("old-sigs"));
}

void TestInstaller::installedExecutablePathPointsAtTheTarget()
{
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	QVERIFY(inst.installedExecutablePath().isEmpty()); /* nothing activated yet */
	QCOMPARE(inst.prepareAndActivate(update()), Installer::AwaitingValidation);
	QCOMPARE(inst.installedExecutablePath(), QString("/opt/pe-bear/PE-bear"));
}

void TestInstaller::theRecordIsWrittenBeforeTheStagingRootExists()
{
	/* The whole point of moving root creation after begin(): makeDir call 1
	   is the journal's own directory, call 2 is the staging root, so failing
	   the second lands after the record. Under the old order -- root first,
	   journal second -- the very same failure left an unjournaled directory
	   beside somebody's installation. */
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	fs.failOnCall(QLatin1String("makeDir"), 2);
	QCOMPARE(inst.prepareAndActivate(update()), Installer::FailedRolledBack);
	QCOMPARE(inst.transaction().state(), TxRolledBack);
	/* the record exists and is closed, not orphaned in Prepared */
	QCOMPARE(journal.listIds().size(), 1);
	QVERIFY(fs.hasFile(paths().transactionsDir() + QLatin1Char('/')
		+ journal.listIds().at(0) + QLatin1String(".json")));
	/* and nothing was left beside the installation */
	QVERIFY2(!fs.hasDir(QLatin1String("/var/pe-bear-updates/staging")),
		"a staging root outlived a run that never staged anything");
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
}

void TestInstaller::aRootThatCannotBeCreatedClosesTheRecord()
{
	/* The failure itself is the same one as above; what this pins is the
	   outcome's honesty. "Refused, untouched" would claim no record exists,
	   and one does. */
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	fs.failOnCall(QLatin1String("makeDir"), 2);
	const Installer::Outcome outcome = inst.prepareAndActivate(update());
	QVERIFY2(outcome != Installer::RefusedUntouched,
		"a run that opened a journal claimed to have touched nothing");
	QVERIFY(outcome != Installer::FailedNeedsAttention);
	QVERIFY(!inst.lastError().isEmpty());
}

void TestInstaller::aRefusedJournalLeavesNoStagingRoot()
{
	/* The mirror of the first test: when the journal cannot be written there
	   is no record, so there must not be a root either -- a directory that
	   exists only while the record naming it does not is unowned. */
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	fs.failAlways(QLatin1String("writeFile"));
	QCOMPARE(inst.prepareAndActivate(update()), Installer::RefusedUntouched);
	QVERIFY(journal.listIds().isEmpty());
	QVERIFY2(!fs.hasDir(QLatin1String("/var/pe-bear-updates/staging")),
		"a refused journal still left a staging root behind");
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
}

void TestInstaller::aStagingRootThatIsNotPrivateIsNotUsed()
{
	/* The root is beside the installation, in a directory that may be shared.
	   One that somebody else created first -- a link, or their own directory --
	   would hold the staged build where they can change it between unpacking
	   and activation, so nothing may be unpacked into it. */
	FakeFileSystem fs;
	installTarget(fs);
	FakePlatform platform(&fs);
	TransactionJournal journal(&fs, paths().transactionsDir());
	Installer inst(&fs, &platform, &journal, paths());
	inst.setInstallation(portableAt(QLatin1String(TARGET)));

	fs.markNotPrivate(paths().stagingDir());
	const Installer::Outcome outcome = inst.prepareAndActivate(update());

	/* a record exists by then, so "untouched" would be untrue; it is closed */
	QCOMPARE(outcome, Installer::FailedRolledBack);
	QCOMPARE(inst.transaction().state(), TxRolledBack);
	QVERIFY2(inst.lastError().contains(QLatin1String("not private")), qPrintable(inst.lastError()));
	QVERIFY2(!platform.calls().contains(QLatin1String("prepareStaging")),
		"the package was unpacked into a directory somebody else controls");
	QCOMPARE(fs.contentOf(QLatin1String("/opt/pe-bear/PE-bear")), QByteArray("old-binary"));
}

QTEST_GUILESS_MAIN(TestInstaller)
#include "tst_installer.moc"

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
		mutable QString m_error;
		mutable QStringList m_calls;
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
	/* and the staging tree it created is gone too */
	QVERIFY2(!fs.hasDir(QLatin1String("/var/pe-bear-updates/staging"))
		|| fs.listDir(QLatin1String("/var/pe-bear-updates/staging")).isEmpty(),
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

QTEST_GUILESS_MAIN(TestInstaller)
#include "tst_installer.moc"

/*
 * Covers the update state machine of PR #4 (state transitions, UT-10,
 * AC-01..AC-07, AC-10).
 *
 * Driven entirely through stub network pieces, so every branch -- including
 * the ones that only happen when a server misbehaves -- is reachable and
 * deterministic.
 */
#include <QtTest>
#include "../UpdateManager.h"
#include "../HelperHandoff.h"
#include "../ProcessControl.h"

using namespace pe_bear::updater;

namespace {

	const char* CURRENT_TAG = "v0.7.2";
	const char* NEWER_TAG = "v0.7.3";

	QString sha256Of(const QByteArray &content)
	{
		return QString::fromLatin1(
			QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex()).toLower();
	}

}; // namespace

	/** Replays a canned release, or a canned failure, on the next event loop pass. */
	class StubReleaseSource : public IReleaseSource
	{
		Q_OBJECT

	public:
		StubReleaseSource() : m_error(ErrorNone), m_busy(false), m_fetchCount(0) {}

		void setRelease(const ReleaseInfo &release) { m_release = release; m_error = ErrorNone; }
		void setFailure(UpdateError error) { m_error = error; }
		int fetchCount() const { return m_fetchCount; }

		virtual void fetchLatest()
		{
			m_fetchCount++;
			m_busy = true;
			QTimer::singleShot(0, this, SLOT(deliver()));
		}
		virtual void cancel()
		{
			m_busy = false;
			emit failed(int(ErrorCancelled), QString());
		}
		virtual bool isBusy() const { return m_busy; }

	private slots:
		void deliver()
		{
			if (!m_busy) return;
			m_busy = false;
			if (m_error != ErrorNone) {
				emit failed(int(m_error), QLatin1String("stub failure"));
				return;
			}
			emit releaseReady(m_release);
		}

	private:
		ReleaseInfo m_release;
		UpdateError m_error;
		bool m_busy;
		int m_fetchCount;
	};

	/** Writes canned bytes where a real download would have put them. */
	class StubDownloader : public IPackageDownloader
	{
		Q_OBJECT

	public:
		StubDownloader() : m_error(ErrorNone), m_busy(false), m_startCount(0) {}

		void setContent(const QByteArray &content) { m_content = content; m_error = ErrorNone; }
		void setFailure(UpdateError error) { m_error = error; }
		int startCount() const { return m_startCount; }
		QString lastPath() const { return m_lastPath; }

		virtual void start(const ReleaseAsset &asset, const QString &targetDir)
		{
			m_startCount++;
			m_busy = true;
			m_lastPath = QDir(targetDir).absoluteFilePath(QFileInfo(asset.name).fileName());
			QTimer::singleShot(0, this, SLOT(deliver()));
		}
		virtual void cancel()
		{
			if (!m_busy) return;
			m_busy = false;
			emit failed(int(ErrorCancelled), QString());
		}
		virtual bool isBusy() const { return m_busy; }

	private slots:
		void deliver()
		{
			if (!m_busy) return;
			m_busy = false;
			if (m_error != ErrorNone) {
				emit failed(int(m_error), QLatin1String("stub failure"));
				return;
			}
			QFile f(m_lastPath);
			if (!f.open(QIODevice::WriteOnly)) {
				emit failed(int(ErrorStorage), QLatin1String("stub could not write"));
				return;
			}
			f.write(m_content);
			f.close();
			emit finished(m_lastPath);
		}

	private:
		QByteArray m_content;
		QString m_lastPath;
		UpdateError m_error;
		bool m_busy;
		int m_startCount;
	};

namespace {

	/** Records what the manager tried to start; never starts anything. */
	class StubLauncher : public pe_bear::updater::IProcessLauncher
	{
	public:
		StubLauncher() : m_refuse(false), m_detached(0) {}
		void setRefuses() { m_refuse = true; }
		int detachedStarts() const { return m_detached; }
		QString exe() const { return m_exe; }
		QStringList args() const { return m_args; }
		QString workingDir() const { return m_workingDir; }

		virtual Result runAndWait(const QString &, const QStringList &, const QString &, int)
		{
			return Result();
		}
		virtual bool startDetached(const QString &exe, const QStringList &args,
			const QString &workingDir)
		{
			m_detached++;
			m_exe = exe; m_args = args; m_workingDir = workingDir;
			return !m_refuse;
		}
		virtual QString lastError() const { return QLatin1String("stub launcher refused"); }

	private:
		bool m_refuse;
		int m_detached;
		QString m_exe, m_workingDir;
		QStringList m_args;
	};

}; // namespace

class TestUpdateManager : public QObject
{
	Q_OBJECT

public:
	TestUpdateManager() : m_source(NULL), m_downloader(NULL), m_manager(NULL), m_tmp(NULL) {}

private slots:
	void init();
	void cleanup();

	void reportsUpToDateForTheSameVersion();
	void reportsUpToDateForAnOlderRelease();
	void offersANewerVersionWithoutDownloadingIt();
	void downloadsAutomaticallyOnlyWhenOptedIn();
	void reachesReadyToInstallAfterVerification();
	void deletesAPackageThatFailsVerification();
	void reportsAManagedInstallationAndStopsThere();
	void reportsWhenNoPackageFitsThisBuild();
	void reportsAmbiguityRatherThanGuessing();
	void survivesANetworkFailureWithoutBlockingAnything();
	void cancellingADownloadKeepsTheOffer();
	void installRequiresReadyToInstall();
	void installWritesTheHandoffAndStartsTheHelper();
	void aMissingHelperIsReportedBeforeAnythingIsWritten();
	void aHelperThatCannotStartLeavesThePackageAndRemovesTheHandoff();
	void skippingAVersionSilencesOnlyAutomaticChecks();
	void automaticCheckRespectsTheInterval();
	void aSecondCheckIsIgnoredWhileOneIsRunning();

private:
	ReleaseInfo makeRelease(const QString &tag, const QByteArray &content,
		const QString &assetName = QLatin1String("PE-bear_0.7.3_qt5_x64_linux.tar.xz"),
		bool withDigest = true);
	void makeUpdatable();

	StubReleaseSource *m_source;
	StubDownloader *m_downloader;
	UpdateManager *m_manager;
	UpdateSettings m_settings;
	QTemporaryDir *m_tmp;
};

void TestUpdateManager::init()
{
	m_tmp = new QTemporaryDir();
	m_settings = UpdateSettings();
	m_source = new StubReleaseSource();
	m_downloader = new StubDownloader();
	m_manager = new UpdateManager(m_source, m_downloader, &m_settings);

	BuildProfile profile;
	profile.setPlatform(PlatformLinux);
	profile.setArchitecture(ArchX64);
	profile.setQtMajor(5);
	profile.setPackageType(PackageLinuxTarXz);
	m_manager->setBuildProfile(profile);

	QSet<int> installable;
	installable.insert(int(PackageLinuxTarXz));
	m_manager->setInstallablePackageTypes(installable);

	m_manager->setPaths(UpdatePaths(QDir(m_tmp->path()).absoluteFilePath("updates"), QString()));
	makeUpdatable();
}

void TestUpdateManager::cleanup()
{
	delete m_manager;
	m_manager = NULL;
	m_source = NULL;
	m_downloader = NULL;
	delete m_tmp;
	m_tmp = NULL;
}

void TestUpdateManager::makeUpdatable()
{
	InstallationInfo info;
	info.kind = InstallPortable;
	info.installDir = m_tmp->path();
	info.writable = true;
	m_manager->setInstallation(info);
}

ReleaseInfo TestUpdateManager::makeRelease(const QString &tag, const QByteArray &content,
	const QString &assetName, bool withDigest)
{
	ReleaseInfo release;
	release.tagName = tag;
	release.version = Version::fromString(tag);

	ReleaseAsset asset;
	asset.name = assetName;
	asset.downloadUrl = QUrl(QLatin1String(
		"https://github.com/hasherezade/pe-bear/releases/download/") + tag + QLatin1Char('/') + assetName);
	asset.size = content.size();
	if (withDigest) {
		asset.sha256 = sha256Of(content);
	}
	release.assets.append(asset);
	return release;
}

void TestUpdateManager::reportsUpToDateForTheSameVersion()
{
	m_source->setRelease(makeRelease(QLatin1String(CURRENT_TAG), QByteArray("x")));
	QSignalSpy upToDate(m_manager, SIGNAL(upToDate()));

	m_manager->checkForUpdates(true);
	QVERIFY(upToDate.wait(2000));
	QCOMPARE(m_manager->state(), StateUpToDate);
	QCOMPARE(m_downloader->startCount(), 0);
}

void TestUpdateManager::reportsUpToDateForAnOlderRelease()
{
	/* A downgrade is never offered, whatever the API says is "latest". */
	m_source->setRelease(makeRelease(QLatin1String("v0.7.1"), QByteArray("x")));
	QSignalSpy upToDate(m_manager, SIGNAL(upToDate()));

	m_manager->checkForUpdates(true);
	QVERIFY(upToDate.wait(2000));
	QCOMPARE(m_manager->state(), StateUpToDate);
}

void TestUpdateManager::offersANewerVersionWithoutDownloadingIt()
{
	const QByteArray content("package bytes");
	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), content));
	QSignalSpy available(m_manager, SIGNAL(updateAvailable(pe_bear::updater::UpdateCandidate)));

	m_manager->checkForUpdates(true);
	QVERIFY(available.wait(2000));

	QCOMPARE(m_manager->state(), StateUpdateAvailable);
	QCOMPARE(m_manager->candidate().release.version.toString(), QString("0.7.3"));
	QVERIFY2(m_downloader->startCount() == 0, "a download started without consent");
	QVERIFY(m_manager->canDownload());
	QVERIFY(!m_manager->canInstall());
}

void TestUpdateManager::downloadsAutomaticallyOnlyWhenOptedIn()
{
	const QByteArray content("package bytes");
	m_settings.setAutoDownloadEnabled(true);
	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), content));
	m_downloader->setContent(content);

	QSignalSpy ready(m_manager, SIGNAL(readyToInstall(pe_bear::updater::VerifiedUpdate)));
	m_manager->checkForUpdates(false);
	QVERIFY(ready.wait(5000));

	QCOMPARE(m_downloader->startCount(), 1);
	QCOMPARE(m_manager->state(), StateReadyToInstall);
}

void TestUpdateManager::reachesReadyToInstallAfterVerification()
{
	const QByteArray content("verified package contents");
	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), content));
	m_downloader->setContent(content);

	QSignalSpy available(m_manager, SIGNAL(updateAvailable(pe_bear::updater::UpdateCandidate)));
	QSignalSpy ready(m_manager, SIGNAL(readyToInstall(pe_bear::updater::VerifiedUpdate)));
	QSignalSpy states(m_manager, SIGNAL(stateChanged(int)));

	m_manager->checkForUpdates(true);
	QVERIFY(available.wait(2000));
	m_manager->startDownload();
	QVERIFY(ready.wait(5000));

	const VerifiedUpdate verified = m_manager->verifiedUpdate();
	QVERIFY(verified.isValid());
	QCOMPARE(verified.sha256, sha256Of(content));
	QCOMPARE(verified.size, qint64(content.size()));
	QVERIFY(QFile::exists(verified.packagePath));
	QVERIFY(m_manager->canInstall());

	/* The documented order of states, with nothing skipped. */
	QList<int> seen;
	for (int i = 0; i < states.count(); i++) {
		seen << states.at(i).first().toInt();
	}
	QVERIFY(seen.indexOf(int(StateChecking)) >= 0);
	QVERIFY(seen.indexOf(int(StateUpdateAvailable)) > seen.indexOf(int(StateChecking)));
	QVERIFY(seen.indexOf(int(StateDownloading)) > seen.indexOf(int(StateUpdateAvailable)));
	QVERIFY(seen.indexOf(int(StateVerifying)) > seen.indexOf(int(StateDownloading)));
	QVERIFY(seen.indexOf(int(StateReadyToInstall)) > seen.indexOf(int(StateVerifying)));
}

void TestUpdateManager::deletesAPackageThatFailsVerification()
{
	const QByteArray announced("what the release says");
	const QByteArray delivered("what the server actually sent");

	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), announced));
	m_downloader->setContent(delivered);

	QSignalSpy available(m_manager, SIGNAL(updateAvailable(pe_bear::updater::UpdateCandidate)));
	QSignalSpy errors(m_manager, SIGNAL(errorOccurred(int, QString)));

	m_manager->checkForUpdates(true);
	QVERIFY(available.wait(2000));
	const QString expectedPath = m_downloader->lastPath();
	m_manager->startDownload();
	QVERIFY(errors.wait(5000));

	QCOMPARE(m_manager->state(), StateFailed);
	const int error = m_manager->lastError();
	QVERIFY2(error == int(ErrorDigestMismatch) || error == int(ErrorSizeMismatch),
		"a package that did not match was not rejected");
	QVERIFY(!m_manager->verifiedUpdate().isValid());
	if (!expectedPath.isEmpty()) {
		QVERIFY2(!QFile::exists(expectedPath), "the rejected package was left on disk");
	}
}

void TestUpdateManager::reportsAManagedInstallationAndStopsThere()
{
	InstallationInfo managed;
	managed.kind = InstallManaged;
	managed.installDir = QLatin1String("/usr/bin");
	managed.writable = false;
	managed.detail = QLatin1String("installed under /usr");
	m_manager->setInstallation(managed);

	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), QByteArray("x")));
	QSignalSpy errors(m_manager, SIGNAL(errorOccurred(int, QString)));

	m_manager->checkForUpdates(true);
	QVERIFY(errors.wait(2000));

	QCOMPARE(m_manager->state(), StateManagedInstallation);
	QCOMPARE(m_manager->lastError(), ErrorManagedInstallation);
	QVERIFY2(m_downloader->startCount() == 0, "a managed installation started a download");
	/* The user is still told which version exists. */
	QCOMPARE(m_manager->candidate().release.version.toString(), QString("0.7.3"));
}

void TestUpdateManager::reportsWhenNoPackageFitsThisBuild()
{
	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), QByteArray("x"),
		QLatin1String("PE-bear_0.7.3_qt6_x64_win_vs22.zip")));
	QSignalSpy errors(m_manager, SIGNAL(errorOccurred(int, QString)));

	m_manager->checkForUpdates(true);
	QVERIFY(errors.wait(2000));

	QCOMPARE(m_manager->state(), StateNoCompatibleAsset);
	QCOMPARE(m_manager->lastError(), ErrorNoCompatibleAsset);
	QCOMPARE(m_downloader->startCount(), 0);
}

void TestUpdateManager::reportsAmbiguityRatherThanGuessing()
{
	const QByteArray content("x");
	ReleaseInfo release = makeRelease(QLatin1String(NEWER_TAG), content);
	/* A second, equally well-matching package. */
	ReleaseAsset twin = release.assets.first();
	twin.name = QLatin1String("PE-bear_0.7.3_qt5.15.13_x64_linux.tar.xz");
	release.assets.append(twin);

	m_source->setRelease(release);
	QSignalSpy errors(m_manager, SIGNAL(errorOccurred(int, QString)));

	m_manager->checkForUpdates(true);
	QVERIFY(errors.wait(2000));

	QCOMPARE(m_manager->state(), StateNoCompatibleAsset);
	QCOMPARE(m_manager->lastError(), ErrorAmbiguousAsset);
	QCOMPARE(m_downloader->startCount(), 0);
}

void TestUpdateManager::survivesANetworkFailureWithoutBlockingAnything()
{
	m_source->setFailure(ErrorNetwork);
	QSignalSpy errors(m_manager, SIGNAL(errorOccurred(int, QString)));

	m_manager->checkForUpdates(false);
	QVERIFY(errors.wait(2000));

	QCOMPARE(m_manager->state(), StateFailed);
	QCOMPARE(m_manager->lastError(), ErrorNetwork);
	/* And a later check still works: nothing is wedged. */
	QVERIFY(m_manager->canCheck());
	m_source->setRelease(makeRelease(QLatin1String(CURRENT_TAG), QByteArray("x")));
	QSignalSpy upToDate(m_manager, SIGNAL(upToDate()));
	m_manager->checkForUpdates(true);
	QVERIFY(upToDate.wait(2000));
}

void TestUpdateManager::cancellingADownloadKeepsTheOffer()
{
	const QByteArray content("package bytes");
	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), content));
	m_downloader->setContent(content);

	QSignalSpy available(m_manager, SIGNAL(updateAvailable(pe_bear::updater::UpdateCandidate)));
	m_manager->checkForUpdates(true);
	QVERIFY(available.wait(2000));

	m_manager->startDownload();
	QCOMPARE(m_manager->state(), StateDownloading);
	m_manager->cancel();

	QCOMPARE(m_manager->state(), StateUpdateAvailable);
	QCOMPARE(m_manager->lastError(), ErrorCancelled);
	QVERIFY(m_manager->canDownload());
}

void TestUpdateManager::installRequiresReadyToInstall()
{
	QSignalSpy errors(m_manager, SIGNAL(errorOccurred(int, QString)));

	/* Idle */
	m_manager->requestInstall();
	QCOMPARE(errors.count(), 0);

	const QByteArray content("package bytes");
	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), content));
	QSignalSpy available(m_manager, SIGNAL(updateAvailable(pe_bear::updater::UpdateCandidate)));
	m_manager->checkForUpdates(true);
	QVERIFY(available.wait(2000));

	/* UpdateAvailable, but nothing has been verified yet */
	QVERIFY(!m_manager->canInstall());
	m_manager->requestInstall();
	QCOMPARE(errors.count(), 0);
}

void TestUpdateManager::installWritesTheHandoffAndStartsTheHelper()
{
	/* The whole hand-off, observed from the outside: a file the helper can
	   parse, a process start with the right arguments, and this process
	   being told to get out of the way. The file is real, not faked -- the
	   manager writes through RealFileSystem by default, and the point of
	   parsing it back with HelperHandoff::fromJson is that the helper will. */
	const QByteArray content("package bytes");
	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), content));
	m_downloader->setContent(content);

	QSignalSpy ready(m_manager, SIGNAL(readyToInstall(pe_bear::updater::VerifiedUpdate)));
	m_settings.setAutoDownloadEnabled(true);
	m_manager->checkForUpdates(true);
	QVERIFY(ready.wait(5000));

	/* A helper that exists, beside nothing in particular. */
	const QString helper = QDir(m_tmp->path()).absoluteFilePath("pe-bear-updater");
	QFile h(helper); QVERIFY(h.open(QIODevice::WriteOnly)); h.write("stub"); h.close();
	m_manager->setHelperPath(helper);
	StubLauncher launcher;
	m_manager->setProcessLauncher(&launcher);

	QSignalSpy started(m_manager, SIGNAL(installStarted()));
	QSignalSpy errors(m_manager, SIGNAL(errorOccurred(int, QString)));
	m_manager->requestInstall();

	QCOMPARE(errors.count(), 0);
	QCOMPARE(started.count(), 1);
	QCOMPARE(m_manager->state(), StateInstalling);

	QCOMPARE(launcher.detachedStarts(), 1);
	QCOMPARE(launcher.exe(), helper);
	QCOMPARE(launcher.args().size(), 2);
	QCOMPARE(launcher.args().at(0), QLatin1String("--handoff"));

	/* The instruction the helper will read: inside the private root, and
	   describing exactly the verified package and this installation. */
	const QString handoffPath = launcher.args().at(1);
	QVERIFY(handoffPath.startsWith(QDir(m_tmp->path()).absoluteFilePath("updates")));
	QFile f(handoffPath);
	QVERIFY2(f.open(QIODevice::ReadOnly), "the handoff was not written where the helper was told to look");
	bool ok = false;
	const HelperHandoff handoff = HelperHandoff::fromJson(f.readAll(), &ok);
	QVERIFY2(ok, "the handoff does not parse as the helper would parse it");
	QCOMPARE(handoff.packagePath, m_manager->verifiedUpdate().packagePath);
	QCOMPARE(handoff.packageSha256, m_manager->verifiedUpdate().sha256);
	QCOMPARE(handoff.packageSize, m_manager->verifiedUpdate().size);
	QCOMPARE(handoff.targetDir, m_manager->installation().installDir);
	QCOMPARE(handoff.parentPid, static_cast<qint64>(QCoreApplication::applicationPid()));
	QVERIFY(handoff.relaunch);
	QVERIFY(handoff.expected().isValid());
}

void TestUpdateManager::aMissingHelperIsReportedBeforeAnythingIsWritten()
{
	const QByteArray content("package bytes");
	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), content));
	m_downloader->setContent(content);
	QSignalSpy ready(m_manager, SIGNAL(readyToInstall(pe_bear::updater::VerifiedUpdate)));
	m_settings.setAutoDownloadEnabled(true);
	m_manager->checkForUpdates(true);
	QVERIFY(ready.wait(5000));

	m_manager->setHelperPath(QDir(m_tmp->path()).absoluteFilePath("no-such-helper"));
	StubLauncher launcher;
	m_manager->setProcessLauncher(&launcher);

	QSignalSpy errors(m_manager, SIGNAL(errorOccurred(int, QString)));
	m_manager->requestInstall();

	QCOMPARE(errors.count(), 1);
	QCOMPARE(m_manager->lastError(), ErrorHelperMissing);
	/* Nothing started, nothing written, and the user can try again. */
	QCOMPARE(launcher.detachedStarts(), 0);
	QVERIFY(!QFile::exists(QDir(m_tmp->path()).absoluteFilePath("updates/" + HelperHandoff::fileName())));
	QCOMPARE(m_manager->state(), StateReadyToInstall);
	QVERIFY(QFile::exists(m_manager->verifiedUpdate().packagePath));
}

void TestUpdateManager::aHelperThatCannotStartLeavesThePackageAndRemovesTheHandoff()
{
	const QByteArray content("package bytes");
	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), content));
	m_downloader->setContent(content);
	QSignalSpy ready(m_manager, SIGNAL(readyToInstall(pe_bear::updater::VerifiedUpdate)));
	m_settings.setAutoDownloadEnabled(true);
	m_manager->checkForUpdates(true);
	QVERIFY(ready.wait(5000));

	const QString helper = QDir(m_tmp->path()).absoluteFilePath("pe-bear-updater");
	QFile h(helper); QVERIFY(h.open(QIODevice::WriteOnly)); h.write("stub"); h.close();
	m_manager->setHelperPath(helper);
	StubLauncher launcher;
	launcher.setRefuses();
	m_manager->setProcessLauncher(&launcher);

	QSignalSpy started(m_manager, SIGNAL(installStarted()));
	QSignalSpy errors(m_manager, SIGNAL(errorOccurred(int, QString)));
	m_manager->requestInstall();

	QCOMPARE(started.count(), 0);
	QCOMPARE(errors.count(), 1);
	QCOMPARE(m_manager->lastError(), ErrorHelperStartFailed);
	QCOMPARE(m_manager->state(), StateReadyToInstall);
	QVERIFY(QFile::exists(m_manager->verifiedUpdate().packagePath));
	/* The instruction is taken back: a helper that never started must not
	   find it at some later, unrelated start. */
	QVERIFY(!QFile::exists(launcher.args().at(1)));
}

void TestUpdateManager::skippingAVersionSilencesOnlyAutomaticChecks()
{
	const QByteArray content("package bytes");
	m_source->setRelease(makeRelease(QLatin1String(NEWER_TAG), content));

	QSignalSpy available(m_manager, SIGNAL(updateAvailable(pe_bear::updater::UpdateCandidate)));
	m_manager->checkForUpdates(true);
	QVERIFY(available.wait(2000));
	m_manager->skipCurrentVersion();

	QCOMPARE(m_settings.skippedVersion(), QString("0.7.3"));
	QCOMPARE(m_manager->state(), StateIdle);

	/* automatic: silent */
	QSignalSpy upToDate(m_manager, SIGNAL(upToDate()));
	m_manager->checkForUpdates(false);
	QVERIFY(upToDate.wait(2000));
	QCOMPARE(m_manager->state(), StateUpToDate);

	/* manual: the user asked, so they get an answer */
	QSignalSpy availableAgain(m_manager, SIGNAL(updateAvailable(pe_bear::updater::UpdateCandidate)));
	m_manager->checkForUpdates(true);
	QVERIFY(availableAgain.wait(2000));
	QCOMPARE(m_manager->state(), StateUpdateAvailable);
}

void TestUpdateManager::automaticCheckRespectsTheInterval()
{
	m_source->setRelease(makeRelease(QLatin1String(CURRENT_TAG), QByteArray("x")));

	m_settings.setLastCheck(QDateTime::currentDateTime().addSecs(-3600));
	m_manager->checkForUpdatesIfDue();
	QCOMPARE(m_source->fetchCount(), 0);

	m_settings.setLastCheck(QDateTime::currentDateTime().addSecs(-25 * 3600));
	QSignalSpy upToDate(m_manager, SIGNAL(upToDate()));
	m_manager->checkForUpdatesIfDue();
	QVERIFY(upToDate.wait(2000));
	QCOMPARE(m_source->fetchCount(), 1);

	/* the run just now updated the timestamp, so the next one is not due */
	m_manager->checkForUpdatesIfDue();
	QCOMPARE(m_source->fetchCount(), 1);
}

void TestUpdateManager::aSecondCheckIsIgnoredWhileOneIsRunning()
{
	m_source->setRelease(makeRelease(QLatin1String(CURRENT_TAG), QByteArray("x")));

	m_manager->checkForUpdates(true);
	QCOMPARE(m_manager->state(), StateChecking);
	m_manager->checkForUpdates(true);
	QCOMPARE(m_source->fetchCount(), 1);

	QSignalSpy upToDate(m_manager, SIGNAL(upToDate()));
	QVERIFY(upToDate.wait(2000));
}

QTEST_MAIN(TestUpdateManager)
#include "tst_updatemanager.moc"

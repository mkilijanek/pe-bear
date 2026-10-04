#include "UpdateManager.h"

using namespace pe_bear::updater;

UpdateManager::UpdateManager(IReleaseSource *source, IPackageDownloader *downloader,
	UpdateSettings *settings, QObject *parent)
	: QObject(parent), m_source(source), m_downloader(downloader),
	m_verifier(this), m_settings(settings),
	m_profile(BuildProfile::current()), m_installableOverridden(false),
	m_state(StateIdle), m_lastError(ErrorNone),
	m_userInitiated(false), m_installConsentGiven(false)
{
	qRegisterMetaType<pe_bear::updater::ReleaseInfo>("pe_bear::updater::ReleaseInfo");
	qRegisterMetaType<pe_bear::updater::UpdateCandidate>("pe_bear::updater::UpdateCandidate");
	qRegisterMetaType<pe_bear::updater::VerifiedUpdate>("pe_bear::updater::VerifiedUpdate");
	qRegisterMetaType<pe_bear::updater::PackageVerifier::Result>("pe_bear::updater::PackageVerifier::Result");

	if (m_source) {
		m_source->setParent(this);
		connect(m_source, SIGNAL(releaseReady(pe_bear::updater::ReleaseInfo)),
			this, SLOT(onReleaseReady(pe_bear::updater::ReleaseInfo)));
		connect(m_source, SIGNAL(failed(int, QString)), this, SLOT(onSourceFailed(int, QString)));
	}
	if (m_downloader) {
		m_downloader->setParent(this);
		connect(m_downloader, SIGNAL(progress(qint64, qint64)),
			this, SLOT(onDownloadProgress(qint64, qint64)));
		connect(m_downloader, SIGNAL(finished(QString)), this, SLOT(onDownloadFinished(QString)));
		connect(m_downloader, SIGNAL(failed(int, QString)), this, SLOT(onDownloadFailed(int, QString)));
	}
	connect(&m_verifier, SIGNAL(progress(qint64, qint64)),
		this, SLOT(onVerifyProgress(qint64, qint64)));
	connect(&m_verifier, SIGNAL(finished(pe_bear::updater::PackageVerifier::Result)),
		this, SLOT(onVerifyFinished(pe_bear::updater::PackageVerifier::Result)));

	m_installation = InstallationDetector::detect();
}

UpdateManager::~UpdateManager()
{
}

bool UpdateManager::isBusy() const
{
	return (m_state == StateChecking || m_state == StateDownloading || m_state == StateVerifying);
}

bool UpdateManager::canCheck() const
{
	return !isBusy();
}

bool UpdateManager::canDownload() const
{
	return (m_state == StateUpdateAvailable) && m_candidate.isValid();
}

void UpdateManager::setState(UpdateState state)
{
	if (m_state == state) return;
	m_state = state;
	emit stateChanged(static_cast<int>(state));
}

void UpdateManager::failWith(UpdateError error, const QString &detail, UpdateState state)
{
	m_lastError = error;
	m_lastErrorDetail = detail;
	setState(state);
	emit errorOccurred(static_cast<int>(error), detail);
}

void UpdateManager::checkForUpdatesIfDue()
{
	if (!m_settings) return;
	if (!m_settings->isAutomaticCheckDue(QDateTime::currentDateTime())) return;
	checkForUpdates(false);
}

void UpdateManager::checkForUpdates(bool userInitiated)
{
	if (!canCheck()) return;
	if (!m_source) {
		failWith(ErrorNetwork, QLatin1String("no release source is configured"));
		return;
	}
	m_userInitiated = userInitiated;
	m_lastError = ErrorNone;
	m_lastErrorDetail.clear();
	m_selectionReasons.clear();
	m_candidate = UpdateCandidate();
	m_verified = VerifiedUpdate();
	m_installConsentGiven = false;

	setState(StateChecking);
	m_source->fetchLatest();
}

void UpdateManager::onSourceFailed(int error, const QString &detail)
{
	const UpdateError err = static_cast<UpdateError>(error);
	/* A failed check must never get in the way of using PE-bear: it records
	   the reason, returns to rest, and that is all. */
	failWith(err, detail, (err == ErrorCancelled) ? StateIdle : StateFailed);
}

void UpdateManager::onReleaseReady(const pe_bear::updater::ReleaseInfo &release)
{
	if (m_state != StateChecking) return;

	if (m_settings) {
		m_settings->setLastCheck(QDateTime::currentDateTime());
	}
	if (!release.isValid()) {
		failWith(ErrorInvalidResponse, QLatin1String("the release metadata is incomplete"));
		return;
	}

	const Version current = Version::current();
	if (!(release.version > current)) {
		setState(StateUpToDate);
		emit upToDate();
		return;
	}
	/* A skipped version stays silent for automatic checks, but a user who
	   asked explicitly always gets an answer. */
	if (!m_userInitiated && m_settings && m_settings->isVersionSkipped(release.version)) {
		setState(StateUpToDate);
		emit upToDate();
		return;
	}

	const QSet<int> installable = m_installableOverridden
		? m_installable
		: AssetSelector::defaultInstallablePackageTypes(m_profile);
	const AssetSelector selector(m_profile, installable);

	ReleaseAsset asset;
	UpdateError digestIssue = ErrorNone;
	m_selectionReasons.clear();
	const AssetSelector::Outcome outcome =
		selector.select(release, asset, &m_selectionReasons, &digestIssue);

	if (outcome != AssetSelector::Selected) {
		UpdateError error = (outcome == AssetSelector::Ambiguous)
			? ErrorAmbiguousAsset : ErrorNoCompatibleAsset;
		/* An unverifiable package is a different story from no package: say so. */
		if (outcome == AssetSelector::NoCompatible && digestIssue != ErrorNone) {
			error = digestIssue;
		}
		failWith(error, release.tagName, StateNoCompatibleAsset);
		return;
	}

	m_candidate.release = release;
	m_candidate.asset = asset;

	/* The release exists and fits, but this copy must not be replaced. Report
	   it and stop: no download, no package manager, no elevation. */
	if (!m_installation.isUpdatable()) {
		failWith(ErrorManagedInstallation, m_installation.detail, StateManagedInstallation);
		return;
	}

	setState(StateUpdateAvailable);
	emit updateAvailable(m_candidate);

	if (m_settings && m_settings->isAutoDownloadEnabled()) {
		startDownload();
	}
}

void UpdateManager::startDownload()
{
	if (!canDownload()) return;
	if (!m_downloader) {
		failWith(ErrorDownloadFailed, QLatin1String("no downloader is configured"));
		return;
	}
	QString error;
	if (!m_paths.prepare(&error)) {
		failWith(ErrorStorage, error);
		return;
	}
	cleanupDownloadDir();
	m_downloadDir = m_paths.createDownloadDir(&error);
	if (m_downloadDir.isEmpty()) {
		failWith(ErrorStorage, error);
		return;
	}
	setState(StateDownloading);
	m_downloader->start(m_candidate.asset, m_downloadDir);
}

void UpdateManager::onDownloadProgress(qint64 received, qint64 total)
{
	if (m_state != StateDownloading) return;
	emit progress(received, total);
}

void UpdateManager::onDownloadFailed(int error, const QString &detail)
{
	const UpdateError err = static_cast<UpdateError>(error);
	cleanupDownloadDir();
	/* Cancelling a download returns to the offer, not to a failure: the update
	   is still there if the user changes their mind. */
	const UpdateState next = (err == ErrorCancelled)
		? (m_candidate.isValid() ? StateUpdateAvailable : StateIdle)
		: StateFailed;
	failWith(err, detail, next);
}

void UpdateManager::onDownloadFinished(const QString &path)
{
	if (m_state != StateDownloading) return;

	setState(StateVerifying);
	/* Nothing is executed, extracted or inspected before this passes. */
	m_verifier.start(path, m_candidate.asset.size, m_candidate.asset.sha256, true);
}

void UpdateManager::onVerifyProgress(qint64 hashed, qint64 total)
{
	if (m_state != StateVerifying) return;
	emit progress(hashed, total);
}

void UpdateManager::onVerifyFinished(const pe_bear::updater::PackageVerifier::Result &result)
{
	if (m_state != StateVerifying) return;

	if (!result.ok) {
		/* The verifier has already deleted the package. */
		cleanupDownloadDir();
		const UpdateState next = (result.error == ErrorCancelled) ? StateUpdateAvailable : StateFailed;
		failWith(result.error, QString(), next);
		return;
	}

	VerifiedUpdate verified;
	verified.candidate = m_candidate;
	verified.packagePath = QDir(m_downloadDir).absoluteFilePath(
		QFileInfo(m_candidate.asset.name).fileName());
	verified.size = result.size;
	verified.sha256 = result.sha256;

	if (!verified.isValid()) {
		cleanupDownloadDir();
		failWith(ErrorDigestMismatch, QLatin1String("the verified package did not match the release metadata"));
		return;
	}
	m_verified = verified;
	setState(StateReadyToInstall);
	emit readyToInstall(m_verified);
}

void UpdateManager::requestInstall()
{
	if (!canInstall()) return;
	/* Consent is recorded here and nowhere else; it is never inferred from a
	   setting, and it is asked for again on every update. */
	m_installConsentGiven = true;
	failWith(ErrorInstallerUnavailable,
		QLatin1String("the transactional installer is not part of this build"),
		StateReadyToInstall);
}

void UpdateManager::skipCurrentVersion()
{
	if (m_settings && m_candidate.release.version.isValid()) {
		m_settings->setSkippedVersion(m_candidate.release.version);
	}
	cancel();
	/* Skipping means the user does not want this version at all, so a package
	   already fetched for it goes too. */
	cleanupDownloadDir();
	m_verified = VerifiedUpdate();
	reset();
}

void UpdateManager::cancel()
{
	if (m_source && m_source->isBusy()) {
		m_source->cancel();
	}
	if (m_downloader && m_downloader->isBusy()) {
		m_downloader->cancel();
	}
	if (m_verifier.isBusy()) {
		m_verifier.cancel();
	}
}

void UpdateManager::cleanupDownloadDir()
{
	if (m_downloadDir.isEmpty()) return;
	UpdatePaths::removeRecursively(m_downloadDir);
	m_downloadDir.clear();
}

void UpdateManager::reset()
{
	cancel();
	/* A package that has been verified is kept: the user may still choose to
	   install it. Anything else is cleared away. */
	if (m_state != StateReadyToInstall) {
		cleanupDownloadDir();
		m_verified = VerifiedUpdate();
	}
	m_candidate = UpdateCandidate();
	m_lastError = ErrorNone;
	m_lastErrorDetail.clear();
	m_selectionReasons.clear();
	m_installConsentGiven = false;
	setState(StateIdle);
}

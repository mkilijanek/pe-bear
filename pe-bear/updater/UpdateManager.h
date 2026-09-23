#pragma once

#include <QtCore>
#include "UpdateTypes.h"
#include "ReleaseClient.h"
#include "DownloadManager.h"
#include "PackageVerifier.h"
#include "InstallationDetector.h"
#include "UpdatePaths.h"
#include "UpdateSettings.h"
#include "AssetSelector.h"

namespace pe_bear {
namespace updater {

/**
 * The update state machine.
 *
 * Owns the whole flow from "is there anything newer?" to a package sitting
 * verified on disk, and stops there. It knows nothing about widgets, so it can
 * be driven entirely from a test with stub network pieces.
 *
 * Three rules hold at every transition:
 *  - nothing is downloaded unless the user opted in or asked;
 *  - nothing is installed without a separate, explicit act of consent;
 *  - a failure anywhere leaves no half-finished package behind.
 *
 * v1 ends at ReadyToInstall: performing the installation belongs to the
 * transactional helper, which is out of this milestone's scope.
 */
class UpdateManager : public QObject
{
	Q_OBJECT

public:
	/**
	 * Takes ownership of @p source and @p downloader.
	 * @param settings borrowed, must outlive this object
	 */
	UpdateManager(IReleaseSource *source, IPackageDownloader *downloader,
		UpdateSettings *settings, QObject *parent = NULL);
	virtual ~UpdateManager();

	void setBuildProfile(const BuildProfile &profile) { m_profile = profile; }
	BuildProfile buildProfile() const { return m_profile; }

	void setInstallation(const InstallationInfo &info) { m_installation = info; }
	InstallationInfo installation() const { return m_installation; }

	void setPaths(const UpdatePaths &paths) { m_paths = paths; }

	/** Overrides the installable package types; used by tests. */
	void setInstallablePackageTypes(const QSet<int> &types)
	{
		m_installable = types;
		m_installableOverridden = true;
	}

	UpdateState state() const { return m_state; }
	UpdateError lastError() const { return m_lastError; }
	QString lastErrorDetail() const { return m_lastErrorDetail; }
	/** Diagnostic lines about rejected assets; safe to log. */
	QStringList lastSelectionReasons() const { return m_selectionReasons; }

	UpdateCandidate candidate() const { return m_candidate; }
	VerifiedUpdate verifiedUpdate() const { return m_verified; }

	bool isBusy() const;
	/** True when a check, download or verification can be started right now. */
	bool canCheck() const;
	bool canDownload() const;
	bool canInstall() const { return m_state == StateReadyToInstall; }

public slots:
	/**
	 * @param userInitiated a manual check ignores the 24 h interval and the
	 *                      skipped-version setting, and reports "up to date"
	 *                      visibly instead of silently
	 */
	void checkForUpdates(bool userInitiated);
	/** Runs a check only if the interval has elapsed and auto-check is on. */
	void checkForUpdatesIfDue();
	void startDownload();
	void cancel();
	/** Marks the currently offered version as skipped and returns to Idle. */
	void skipCurrentVersion();
	/** Clears any finished or failed run, keeping settings untouched. */
	void reset();

	/**
	 * Records the user's explicit consent to install. In this milestone the
	 * platform activation step does not exist yet, so this reports
	 * InstallerUnavailable rather than pretending to install.
	 */
	void requestInstall();

signals:
	void stateChanged(int state);
	void progress(qint64 done, qint64 total);
	void updateAvailable(const pe_bear::updater::UpdateCandidate &candidate);
	void readyToInstall(const pe_bear::updater::VerifiedUpdate &verified);
	void upToDate();
	void errorOccurred(int error, const QString &detail);

private slots:
	void onReleaseReady(const pe_bear::updater::ReleaseInfo &release);
	void onSourceFailed(int error, const QString &detail);
	void onDownloadProgress(qint64 received, qint64 total);
	void onDownloadFinished(const QString &path);
	void onDownloadFailed(int error, const QString &detail);
	void onVerifyProgress(qint64 hashed, qint64 total);
	void onVerifyFinished(const pe_bear::updater::PackageVerifier::Result &result);

private:
	void setState(UpdateState state);
	void failWith(UpdateError error, const QString &detail, UpdateState state = StateFailed);
	void cleanupDownloadDir();

	IReleaseSource *m_source;
	IPackageDownloader *m_downloader;
	AsyncPackageVerifier m_verifier;
	UpdateSettings *m_settings;

	BuildProfile m_profile;
	InstallationInfo m_installation;
	UpdatePaths m_paths;
	QSet<int> m_installable;
	bool m_installableOverridden;

	UpdateState m_state;
	UpdateError m_lastError;
	QString m_lastErrorDetail;
	QStringList m_selectionReasons;

	UpdateCandidate m_candidate;
	VerifiedUpdate m_verified;
	QString m_downloadDir;
	bool m_userInitiated;
	bool m_installConsentGiven;
};

}; // namespace updater
}; // namespace pe_bear

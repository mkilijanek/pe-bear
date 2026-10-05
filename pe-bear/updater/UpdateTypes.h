#pragma once

#include <QtCore>
#include "Version.h"
#include "BuildProfile.h"

namespace pe_bear {
namespace updater {

/**
 * States of the update flow. Installing hands off to pe-bear-updater and ends
 * this process; the outcome arrives at the next start, through HelperResult.
 */
enum UpdateState {
	StateIdle = 0,
	StateChecking,
	StateUpToDate,
	StateUpdateAvailable,
	StateDownloading,
	StateVerifying,
	StateReadyToInstall,
	/** The helper has been started; this process is about to close. */
	StateInstalling,
	StateNoCompatibleAsset,
	StateManagedInstallation,
	StateFailed,
	UPDATE_STATES_COUNT
};

enum UpdateError {
	ErrorNone = 0,
	/* discovery */
	ErrorNetwork,
	ErrorTimeout,
	ErrorRateLimited,
	ErrorTls,
	ErrorForbiddenRedirect,
	ErrorInvalidResponse,
	ErrorResponseTooLarge,
	ErrorNoStableRelease,
	ErrorInvalidVersion,
	ErrorMissingDigest,
	ErrorInvalidDigest,
	/* asset selection */
	ErrorNoCompatibleAsset,
	ErrorAmbiguousAsset,
	ErrorManagedInstallation,
	/* download and verification */
	ErrorDownloadFailed,
	ErrorSizeMismatch,
	ErrorDigestMismatch,
	ErrorStorage,
	ErrorCancelled,
	/* install preparation */
	ErrorInstallerUnavailable,
	/* the installation sits where replacing the directory would destroy
	   other things: a personal folder or a filesystem root */
	ErrorProtectedLocation,
	/* install hand-off: nothing below changes the installation; it is the
	   helper that does, and these are the ways of failing to reach it */
	ErrorHelperMissing,
	ErrorHandoffWriteFailed,
	ErrorHelperStartFailed,
	UPDATE_ERRORS_COUNT
};

QString updateStateToString(UpdateState s);
QString updateErrorToString(UpdateError e);
/** Human-readable, translated message for the given error. */
QString updateErrorMessage(UpdateError e);

//----------------------------------------------------------------------

/** One downloadable file of a release, as reported by the GitHub API. */
struct ReleaseAsset
{
	ReleaseAsset() : size(0), digestMalformed(false), qtMajor(0),
		platform(PlatformUnknown), arch(ArchUnknown), packageType(PackageUnknown) {}

	QString name;
	QUrl downloadUrl;
	qint64 size;
	/** Lowercase 64-hex SHA-256, without the "sha256:" prefix. */
	QString sha256;
	/** True when a digest was published but could not be parsed. */
	bool digestMalformed;

	/* parsed out of the asset name by AssetSelector */
	int qtMajor;
	Platform platform;
	Architecture arch;
	PackageType packageType;
	QString runtime;

	bool hasDigest() const { return sha256.length() == 64; }
	bool isValid() const { return !name.isEmpty() && downloadUrl.isValid() && size > 0; }
};

/** A validated stable release. */
struct ReleaseInfo
{
	ReleaseInfo() {}

	QString tagName;
	Version version;
	QUrl htmlUrl;
	QString publishedAt;
	QList<ReleaseAsset> assets;

	bool isValid() const { return version.isValid() && !tagName.isEmpty(); }
};

/** A release plus the single asset that matches the running build. */
struct UpdateCandidate
{
	UpdateCandidate() {}

	ReleaseInfo release;
	ReleaseAsset asset;

	bool isValid() const { return release.isValid() && asset.isValid() && asset.hasDigest(); }
};

/**
 * A downloaded package whose size and SHA-256 have been confirmed in this
 * process. The helper re-verifies independently; this is not a trust anchor
 * for the installer.
 */
struct VerifiedUpdate
{
	VerifiedUpdate() : size(0) {}

	UpdateCandidate candidate;
	/** Absolute path of the verified package inside the private update dir. */
	QString packagePath;
	qint64 size;
	/** Digest as recomputed locally; equals candidate.asset.sha256. */
	QString sha256;

	bool isValid() const
	{
		return candidate.isValid() && !packagePath.isEmpty()
			&& size == candidate.asset.size
			&& sha256.length() == 64
			&& sha256 == candidate.asset.sha256;
	}
};

}; // namespace updater
}; // namespace pe_bear

Q_DECLARE_METATYPE(pe_bear::updater::ReleaseAsset)
Q_DECLARE_METATYPE(pe_bear::updater::ReleaseInfo)
Q_DECLARE_METATYPE(pe_bear::updater::UpdateCandidate)
Q_DECLARE_METATYPE(pe_bear::updater::VerifiedUpdate)

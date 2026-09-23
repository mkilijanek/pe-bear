#include "UpdateTypes.h"

using namespace pe_bear::updater;

QString pe_bear::updater::updateStateToString(UpdateState s)
{
	switch (s) {
		case StateIdle: return QLatin1String("Idle");
		case StateChecking: return QLatin1String("Checking");
		case StateUpToDate: return QLatin1String("UpToDate");
		case StateUpdateAvailable: return QLatin1String("UpdateAvailable");
		case StateDownloading: return QLatin1String("Downloading");
		case StateVerifying: return QLatin1String("Verifying");
		case StateReadyToInstall: return QLatin1String("ReadyToInstall");
		case StateNoCompatibleAsset: return QLatin1String("NoCompatibleAsset");
		case StateManagedInstallation: return QLatin1String("ManagedInstallation");
		case StateFailed: return QLatin1String("Failed");
		default: return QLatin1String("Invalid");
	}
}

QString pe_bear::updater::updateErrorToString(UpdateError e)
{
	switch (e) {
		case ErrorNone: return QLatin1String("None");
		case ErrorNetwork: return QLatin1String("Network");
		case ErrorTimeout: return QLatin1String("Timeout");
		case ErrorRateLimited: return QLatin1String("RateLimited");
		case ErrorTls: return QLatin1String("Tls");
		case ErrorForbiddenRedirect: return QLatin1String("ForbiddenRedirect");
		case ErrorInvalidResponse: return QLatin1String("InvalidResponse");
		case ErrorResponseTooLarge: return QLatin1String("ResponseTooLarge");
		case ErrorNoStableRelease: return QLatin1String("NoStableRelease");
		case ErrorInvalidVersion: return QLatin1String("InvalidVersion");
		case ErrorMissingDigest: return QLatin1String("MissingDigest");
		case ErrorInvalidDigest: return QLatin1String("InvalidDigest");
		case ErrorNoCompatibleAsset: return QLatin1String("NoCompatibleAsset");
		case ErrorAmbiguousAsset: return QLatin1String("Ambiguous");
		case ErrorManagedInstallation: return QLatin1String("ManagedInstallation");
		case ErrorDownloadFailed: return QLatin1String("DownloadFailed");
		case ErrorSizeMismatch: return QLatin1String("SizeMismatch");
		case ErrorDigestMismatch: return QLatin1String("DigestMismatch");
		case ErrorStorage: return QLatin1String("Storage");
		case ErrorCancelled: return QLatin1String("Cancelled");
		case ErrorInstallerUnavailable: return QLatin1String("InstallerUnavailable");
		default: return QLatin1String("Invalid");
	}
}

QString pe_bear::updater::updateErrorMessage(UpdateError e)
{
	switch (e) {
		case ErrorNone:
			return QString();
		case ErrorNetwork:
			return QCoreApplication::translate("Updater", "Could not reach the update server.");
		case ErrorTimeout:
			return QCoreApplication::translate("Updater", "The update server did not respond in time.");
		case ErrorRateLimited:
			return QCoreApplication::translate("Updater", "The update server is rate-limiting requests. Try again later.");
		case ErrorTls:
			return QCoreApplication::translate("Updater", "The secure connection to the update server could not be established.");
		case ErrorForbiddenRedirect:
			return QCoreApplication::translate("Updater", "The update server redirected to an unexpected host.");
		case ErrorInvalidResponse:
			return QCoreApplication::translate("Updater", "The update server returned an unexpected response.");
		case ErrorResponseTooLarge:
			return QCoreApplication::translate("Updater", "The response from the update server was too large.");
		case ErrorNoStableRelease:
			return QCoreApplication::translate("Updater", "No stable release was found.");
		case ErrorInvalidVersion:
			return QCoreApplication::translate("Updater", "The release version could not be interpreted.");
		case ErrorMissingDigest:
			return QCoreApplication::translate("Updater", "The release does not publish a SHA-256 digest, so it cannot be verified.");
		case ErrorInvalidDigest:
			return QCoreApplication::translate("Updater", "The published SHA-256 digest is malformed.");
		case ErrorNoCompatibleAsset:
			return QCoreApplication::translate("Updater", "The new release has no package matching this build.");
		case ErrorAmbiguousAsset:
			return QCoreApplication::translate("Updater", "Several packages of the new release match this build, so none was chosen.");
		case ErrorManagedInstallation:
			return QCoreApplication::translate("Updater", "This copy of PE-bear is managed by the system, so it will not be replaced.");
		case ErrorDownloadFailed:
			return QCoreApplication::translate("Updater", "The download did not complete.");
		case ErrorSizeMismatch:
			return QCoreApplication::translate("Updater", "The downloaded package has an unexpected size.");
		case ErrorDigestMismatch:
			return QCoreApplication::translate("Updater", "The downloaded package failed SHA-256 verification and was deleted.");
		case ErrorStorage:
			return QCoreApplication::translate("Updater", "The update directory could not be prepared.");
		case ErrorCancelled:
			return QCoreApplication::translate("Updater", "Cancelled.");
		case ErrorInstallerUnavailable:
			return QCoreApplication::translate("Updater", "Installation is not available in this build.");
		default:
			return QCoreApplication::translate("Updater", "Unknown error.");
	}
}

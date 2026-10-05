#include "UpdateHelper.h"

#include <QCoreApplication>
#include "PackageVerifier.h"
#include "TransactionJournal.h"
#include "InstallationDetector.h"

namespace pe_bear {
namespace updater {

namespace {

	struct ResultName
	{
		UpdateHelper::Result r;
		const char *name;
		int exitCode;
	};

	/* Exit codes are part of the interface between the helper and PE-bear, so
	   they are written out here rather than derived from the enum's order --
	   inserting a value must not renumber the ones already in use. */
	const ResultName RESULTS[] = {
		{ UpdateHelper::Succeeded,                 "Succeeded",                 0  },
		{ UpdateHelper::RefusedInvalidRequest,     "RefusedInvalidRequest",     10 },
		{ UpdateHelper::RefusedStaleRequest,       "RefusedStaleRequest",       11 },
		{ UpdateHelper::RefusedPackageMismatch,    "RefusedPackageMismatch",    12 },
		{ UpdateHelper::RefusedTarget,             "RefusedTarget",             13 },
		{ UpdateHelper::RefusedParentStillRunning, "RefusedParentStillRunning", 14 },
		{ UpdateHelper::RolledBack,                "RolledBack",                20 },
		{ UpdateHelper::NeedsAttention,            "NeedsAttention",            30 },
		{ UpdateHelper::InternalError,             "InternalError",             40 }
	};
	const size_t RESULT_COUNT = sizeof(RESULTS) / sizeof(RESULTS[0]);

	/** Tolerance for a clock that is slightly behind the writer's. */
	const qint64 CLOCK_SKEW_SECONDS = 120;

}; // namespace

QString UpdateHelper::resultToString(Result r)
{
	for (size_t i = 0; i < RESULT_COUNT; i++) {
		if (RESULTS[i].r == r) return QLatin1String(RESULTS[i].name);
	}
	return QLatin1String("Invalid");
}

int UpdateHelper::resultToExitCode(Result r)
{
	for (size_t i = 0; i < RESULT_COUNT; i++) {
		if (RESULTS[i].r == r) return RESULTS[i].exitCode;
	}
	return RESULTS[RESULT_COUNT - 1].exitCode;
}

bool UpdateHelper::leftUntouched(Result r)
{
	switch (r) {
		case RefusedInvalidRequest:
		case RefusedStaleRequest:
		case RefusedPackageMismatch:
		case RefusedTarget:
		case RefusedParentStillRunning:
			return true;
		default:
			/* Succeeded changed things on purpose; RolledBack tried to put
			   them back but the installation is not bit-for-bit what it was;
			   NeedsAttention and InternalError make no promise at all. */
			return false;
	}
}

QString UpdateHelper::resultMessage(Result r)
{
	switch (r) {
		case Succeeded:
			return QCoreApplication::translate("Updater",
				"The update was installed.");
		case RefusedInvalidRequest:
			return QCoreApplication::translate("Updater",
				"The update instructions were not usable. Nothing was changed.");
		case RefusedStaleRequest:
			return QCoreApplication::translate("Updater",
				"The update instructions were too old to act on. Nothing was changed.");
		case RefusedPackageMismatch:
			return QCoreApplication::translate("Updater",
				"The update package did not match what was expected, so it was not installed. Nothing was changed.");
		case RefusedTarget:
			return QCoreApplication::translate("Updater",
				"This installation cannot be updated automatically. Nothing was changed.");
		case RefusedParentStillRunning:
			return QCoreApplication::translate("Updater",
				"PE-bear was still running, so the update was not applied. Nothing was changed.");
		case RolledBack:
			return QCoreApplication::translate("Updater",
				"The update failed and the previous version was restored.");
		case NeedsAttention:
			return QCoreApplication::translate("Updater",
				"The update failed and could not be fully undone. Please reinstall PE-bear.");
		default:
			return QCoreApplication::translate("Updater",
				"The updater stopped because of an internal error.");
	}
}

//----------------------------------------------------------------------

UpdateHelper::UpdateHelper(IFileSystem *fs, PlatformInstaller *platform, IProcessProbe *probe,
		IProcessLauncher *launcher, const UpdatePaths &paths, const Limits &limits)
	: m_fs(fs), m_platform(platform), m_probe(probe), m_launcher(launcher),
	m_paths(paths), m_limits(limits)
{
}

void UpdateHelper::note(const QString &what)
{
	m_journal << what;
}

UpdateHelper::Result UpdateHelper::refuse(Result r, const QString &why)
{
	m_lastError = why;
	note(resultToString(r) + QLatin1String(": ") + why);
	return r;
}

QString UpdateHelper::computeDigest(const QString &path) const
{
	return PackageVerifier::computeSha256(path);
}

VerifiedUpdate UpdateHelper::describeUpdate(const HelperHandoff &handoff)
{
	ReleaseAsset asset;
	asset.name = handoff.assetName;
	asset.downloadUrl = QUrl(handoff.assetUrl);
	asset.size = handoff.packageSize;
	asset.sha256 = handoff.packageSha256;

	ReleaseInfo release;
	release.tagName = handoff.releaseTag;
	release.version = handoff.expected();

	UpdateCandidate candidate;
	candidate.release = release;
	candidate.asset = asset;

	VerifiedUpdate update;
	update.candidate = candidate;
	update.packagePath = handoff.packagePath;
	update.size = handoff.packageSize;
	update.sha256 = handoff.packageSha256;
	return update;
}

UpdateHelper::Result UpdateHelper::checkRequest(const HelperHandoff &handoff)
{
	if (!m_fs || !m_platform || !m_probe || !m_launcher) {
		return refuse(InternalError, QLatin1String("the helper is not wired up"));
	}
	if (!handoff.isValid()) {
		return refuse(RefusedInvalidRequest,
			QLatin1String("the instructions are not self-consistent"));
	}

	if (!handoff.hasUsableTimestamp()) {
		return refuse(RefusedInvalidRequest,
			QLatin1String("the instructions carry no usable timestamp"));
	}

	const QDateTime now = m_now.isValid() ? m_now : QDateTime::currentDateTimeUtc();
	const qint64 age = handoff.ageSeconds(now);
	/* A minute or two of skew between the process that wrote this and the one
	   reading it is ordinary; refusing it would make the updater fail on
	   machines whose clocks are merely imperfect. Further ahead than that is
	   not skew. */
	if (age < -CLOCK_SKEW_SECONDS) {
		return refuse(RefusedStaleRequest, QLatin1String("the instructions are dated ")
			+ QString::number(-age) + QLatin1String(" seconds ahead of now"));
	}
	if (age > HelperHandoff::MAX_AGE_SECONDS) {
		return refuse(RefusedStaleRequest, QLatin1String("the instructions are ")
			+ QString::number(age) + QLatin1String(" seconds old"));
	}

	note(QLatin1String("instructions accepted: run ") + handoff.runId
		+ QLatin1String(", ") + handoff.releaseTag
		+ QLatin1String(" from ") + handoff.assetUrl);
	return Succeeded;
}

UpdateHelper::Result UpdateHelper::checkPackage(const HelperHandoff &handoff)
{
	const QString canonicalPackage = m_fs->canonicalPath(handoff.packagePath);
	if (canonicalPackage.isEmpty() || !m_fs->exists(canonicalPackage)) {
		return refuse(RefusedPackageMismatch, QLatin1String("the package is not there: ")
			+ QDir::toNativeSeparators(handoff.packagePath));
	}
	if (m_fs->isDir(canonicalPackage)) {
		return refuse(RefusedPackageMismatch, QLatin1String("the package is a directory"));
	}

	/* Confined to the updater's own directory. A package anywhere else was
	   not put there by the download step, and a helper that unpacked whatever
	   path it was handed would be a general-purpose archive extractor running
	   with the user's rights -- a far more useful thing to misuse than an
	   updater. */
	const QString root = m_fs->canonicalPath(m_paths.root());
	if (root.isEmpty()
		|| !(canonicalPackage == root
			|| canonicalPackage.startsWith(root + QLatin1Char('/'))))
	{
		return refuse(RefusedPackageMismatch,
			QLatin1String("the package is outside the updater's directory: ")
			+ QDir::toNativeSeparators(canonicalPackage));
	}

	const qint64 size = m_fs->fileSize(canonicalPackage);
	if (size != handoff.packageSize) {
		return refuse(RefusedPackageMismatch, QLatin1String("the package is ")
			+ QString::number(size) + QLatin1String(" bytes, expected ")
			+ QString::number(handoff.packageSize));
	}

	/* Recomputed here, in this process, from the file as it is now. PE-bear
	   hashed it when it arrived; that was a statement about the past. */
	const QString digest = computeDigest(canonicalPackage);
	if (digest.isEmpty()) {
		return refuse(RefusedPackageMismatch, QLatin1String("the package could not be read"));
	}
	if (digest != handoff.packageSha256) {
		return refuse(RefusedPackageMismatch,
			QLatin1String("the package digest does not match: got ") + digest);
	}

	note(QLatin1String("package re-verified in this process: ") + digest);
	return Succeeded;
}

UpdateHelper::Result UpdateHelper::checkTarget(const HelperHandoff &handoff, InstallationInfo *info)
{
	const QString canonicalTarget = m_fs->canonicalPath(handoff.targetDir);
	if (canonicalTarget.isEmpty() || !m_fs->isDir(canonicalTarget)) {
		return refuse(RefusedTarget, QLatin1String("the target is not a directory: ")
			+ QDir::toNativeSeparators(handoff.targetDir));
	}

	/* The target must not be inside the updater's own working area: the
	   staging tree and the backups live there, and replacing one of those
	   with a build would make the transaction's record describe something
	   that no longer exists. */
	const QString root = m_fs->canonicalPath(m_paths.root());
	if (!root.isEmpty()
		&& (canonicalTarget == root || canonicalTarget.startsWith(root + QLatin1Char('/'))))
	{
		return refuse(RefusedTarget,
			QLatin1String("the target is inside the updater's own directory"));
	}

	const QString exe = m_platform->executablePathIn(canonicalTarget);
	if (!m_fs->exists(exe) || m_fs->isDir(exe)) {
		return refuse(RefusedTarget, QLatin1String("no PE-bear executable in the target: ")
			+ QDir::toNativeSeparators(exe));
	}

	/* The *kind* is decided by the same code the GUI used, rather than by a
	   second opinion written here. Two places deciding what counts as a
	   managed installation is two places to drift apart, and the consequence
	   of drift is the updater touching files a package manager owns. */
	InstallationInfo detected = InstallationDetector::detectAt(canonicalTarget, exe,
		InstallationDetector::currentEnvironment());
	detected.installDir = canonicalTarget;
	detected.executablePath = exe;

	/* Writability, though, is asked of IFileSystem and not taken from the
	   detector. The detector answers it by touching the real filesystem --
	   which is right for the running process it was written to describe, and
	   wrong here: this is the one question in the helper whose answer decides
	   whether files get moved, and routing it around the interface the rest of
	   the design rests on would make it the only step that cannot be driven
	   through its failure case. */
	detected.writable = m_fs->isWritableDir(canonicalTarget);

	QString why;
	if (!m_platform->canInstall(detected, &why)) {
		return refuse(RefusedTarget, why);
	}

	if (info) *info = detected;
	note(QLatin1String("target accepted: ") + QDir::toNativeSeparators(canonicalTarget)
		+ QLatin1String(" (") + installationKindToString(detected.kind) + QLatin1String(")"));
	return Succeeded;
}

UpdateHelper::Result UpdateHelper::awaitParentExit(const HelperHandoff &handoff)
{
	const ProcessIdentity parent = m_probe->identify(handoff.parentPid);
	if (!parent.isValid()) {
		note(QLatin1String("PE-bear had already exited"));
		return Succeeded;
	}

	const qint64 deadline = m_probe->elapsedMs() + m_limits.parentExitTimeoutMs;
	while (m_probe->isRunning(parent)) {
		if (m_probe->elapsedMs() >= deadline) {
			/* Refused, not forced. Terminating PE-bear here would discard
			   whatever it was holding, which is the user's to lose. */
			return refuse(RefusedParentStillRunning,
				QLatin1String("PE-bear (pid ") + QString::number(handoff.parentPid)
				+ QLatin1String(") did not close within ")
				+ QString::number(m_limits.parentExitTimeoutMs / 1000)
				+ QLatin1String(" seconds"));
		}
		m_probe->sleep(m_limits.parentPollIntervalMs);
	}

	note(QLatin1String("PE-bear exited; the installation is now idle"));
	return Succeeded;
}

UpdateHelper::Result UpdateHelper::validateInstalled(Installer &installer,
		const HelperHandoff &handoff)
{
	const QString exe = installer.installedExecutablePath();
	if (exe.isEmpty() || !m_fs->exists(exe)) {
		installer.rollBack(QLatin1String("the installed build has no executable"));
		return refuse(RolledBack, QLatin1String("the installed build has no executable"));
	}

	/* Outside the directory that was just replaced, deliberately: a request
	   written inside it would be destroyed by the next rollback, and the
	   resulting absence would be indistinguishable from the new build having
	   crashed. */
	const QString requestPath = QDir::cleanPath(m_paths.root() + QDir::separator()
		+ QLatin1String("handshake-") + handoff.runId + QLatin1String("-request.json"));
	const QString responsePath = QDir::cleanPath(m_paths.root() + QDir::separator()
		+ QLatin1String("handshake-") + handoff.runId + QLatin1String("-response.json"));

	StartupHandshake handshake(m_fs);
	StartupHandshake::Request request;
	if (!handshake.createRequest(requestPath, responsePath, handoff.expected(), &request)) {
		const QString why = QLatin1String("could not set up the startup check: ")
			+ handshake.lastError();
		const Installer::Outcome o = installer.rollBack(why);
		return refuse(o == Installer::FailedRolledBack ? RolledBack : NeedsAttention, why);
	}

	QStringList args;
	args << QLatin1String("--update-handshake") << requestPath;

	const IProcessLauncher::Result run = m_launcher->runAndWait(exe, args,
		parentDirectoryOf(exe), m_limits.handshakeTimeoutMs);

	/* The verdict comes from the response file, not from the exit code. A
	   build can exit 0 having done nothing, and the file is what says it got
	   far enough to know its own version. */
	const StartupHandshake::Verdict verdict = handshake.verifyResponse(request);
	handshake.cleanUp(requestPath, request);

	if (verdict == StartupHandshake::Accepted) {
		if (!installer.confirmValidated()) {
			/* The new build is in place and works; only the bookkeeping
			   failed. Rolling back a working installation over a journal
			   write would be the worse of the two outcomes, so this is
			   reported rather than undone. */
			note(QLatin1String("installed and validated, but the record could not be closed: ")
				+ installer.lastError());
			m_lastError = installer.lastError();
			return NeedsAttention;
		}
		note(QLatin1String("the new build answered the startup check"));
		return Succeeded;
	}

	QString why = StartupHandshake::verdictMessage(verdict);
	if (!run.started) {
		why = QLatin1String("the new build would not start: ") + m_launcher->lastError();
	} else if (!run.exited) {
		why = QLatin1String("the new build did not finish the startup check in time");
	} else if (run.crashed) {
		why = QLatin1String("the new build crashed during the startup check");
	}

	const Installer::Outcome o = installer.rollBack(why);
	if (o == Installer::FailedRolledBack) {
		return refuse(RolledBack, why);
	}
	return refuse(NeedsAttention, why + QLatin1String("; the restore did not finish: ")
		+ installer.lastError());
}

UpdateHelper::Result UpdateHelper::run(const HelperHandoff &handoff)
{
	m_journal.clear();
	m_lastError.clear();
	m_transactionId.clear();

	/* Nothing below this point changes the installation. */
	Result r = checkRequest(handoff);
	if (r != Succeeded) return r;
	r = checkPackage(handoff);
	if (r != Succeeded) return r;

	InstallationInfo installation;
	r = checkTarget(handoff, &installation);
	if (r != Succeeded) return r;

	r = awaitParentExit(handoff);
	if (r != Succeeded) return r;

	/* From here on something can change. */
	TransactionJournal journal(m_fs, m_paths.transactionsDir());
	if (!journal.prepare()) {
		return refuse(InternalError, QLatin1String("could not prepare the transaction record: ")
			+ journal.lastError());
	}

	Installer installer(m_fs, m_platform, &journal, m_paths);
	installer.setInstallation(installation);

	const Installer::Outcome outcome = installer.prepareAndActivate(describeUpdate(handoff));
	m_transactionId = installer.transaction().record().id;

	switch (outcome) {
		case Installer::RefusedUntouched:
			return refuse(RefusedTarget, installer.lastError());
		case Installer::FailedRolledBack:
			return refuse(RolledBack, installer.lastError());
		case Installer::FailedNeedsAttention:
			return refuse(NeedsAttention, installer.lastError());
		case Installer::AwaitingValidation:
			break;
		default:
			return refuse(InternalError, QLatin1String("unknown installer outcome"));
	}

	note(QLatin1String("the new build is in place, awaiting its own confirmation"));

	r = validateInstalled(installer, handoff);
	if (r != Succeeded) return r;

	if (handoff.relaunch) {
		const QString exe = installer.installedExecutablePath();
		if (!m_launcher->startDetached(exe, QStringList(), parentDirectoryOf(exe))) {
			/* The update is done and committed; not restarting is an
			   inconvenience, not a failure, and reporting it as one would
			   invite someone to "fix" it by rolling back a good install. */
			note(QLatin1String("could not start PE-bear again: ") + m_launcher->lastError());
		} else {
			note(QLatin1String("PE-bear restarted"));
		}
	}

	return Succeeded;
}

}; // namespace updater
}; // namespace pe_bear

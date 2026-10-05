#include "UpdateCoordinator.h"
#include "../ReleaseClient.h"
#include "../DownloadManager.h"
#include "../InstallationDetector.h"
#include "../FileSystem.h"
#include "../TransactionJournal.h"
#include "../Recovery.h"
#include "../UpdatePaths.h"
#include "../../REbear.h"

using namespace pe_bear::updater;

const int UpdateCoordinator::AUTO_CHECK_DELAY_MS;

UpdateCoordinator::UpdateCoordinator(UpdateSettings *settings,
	IUnsavedWorkProbe *probe, QWidget *parentWindow)
	: QObject(parentWindow), m_settings(settings), m_probe(probe),
	m_parentWindow(parentWindow), m_manager(NULL), m_dialog(NULL), m_started(false)
{
	m_manager = new UpdateManager(new ReleaseClient(), new DownloadManager(), m_settings, this);

	const InstallationInfo installation = InstallationDetector::detect();
	m_manager->setInstallation(installation);
	/* The default paths, deliberately. Wiring the beside-installation staging
	   root here would mean UpdatePaths::prepare() creates it during a mere
	   download -- before anything has been validated, and in the phase that is
	   documented to change nothing outside the private root. The helper
	   chooses the real staging root when an install is actually about to
	   happen, and the installer creates it inside the transaction. */
	m_manager->setPaths(UpdatePaths());

	m_dialog = new UpdateDialog(m_manager, parentWindow);
	connect(m_dialog, SIGNAL(installRequested()), this, SLOT(onInstallRequested()));
	connect(m_manager, SIGNAL(stateChanged(int)), this, SLOT(onStateChanged(int)));
}

bool UpdateCoordinator::hasPendingInstall() const
{
	return m_manager && (m_manager->state() == StateReadyToInstall);
}

void UpdateCoordinator::onApplicationReady()
{
	if (m_started) return;
	m_started = true;

	/* Before anything to do with checking for updates, and regardless of
	   whether checking is enabled: this is about an update that already
	   happened and did not finish. After a helper crashes, the user does not
	   relaunch the helper -- they relaunch PE-bear, and this is where that
	   lands. Reached only on a real start, never from the handshake path,
	   which returns before the window is shown. */
	recoverInterruptedUpdates();

	if (!m_settings || !m_settings->isAutoCheckEnabled()) return;

	/* Deliberately late and asynchronous: no network work happens on the path
	   between launching PE-bear and having a usable window, and a machine with
	   no connectivity notices nothing at all. */
	QTimer::singleShot(AUTO_CHECK_DELAY_MS, this, SLOT(onAutoCheckTimeout()));
}

void UpdateCoordinator::onAutoCheckTimeout()
{
	if (!m_manager) return;
	m_manager->checkForUpdatesIfDue();
}

void UpdateCoordinator::recoverInterruptedUpdates()
{
	RealFileSystem fs;
	const UpdatePaths paths;
	/* Nothing to recover if there is no journal yet, and creating one at
	   every start just to find it empty would be a write for no reason. */
	if (!fs.isDir(paths.transactionsDir())) return;

	TransactionJournal journal(&fs, paths.transactionsDir());
	Recovery recovery(&fs, &journal);

	/* This process is a working PE-bear running from its installation
	   directory, which is the one fact the helper never has: an old
	   activated record for *this* directory is therefore kept, not undone. */
	const QString runningFrom = m_manager ? m_manager->installation().installDir : QString();
	recovery.run(runningFrom);

	const QStringList lines = recovery.journal();
	for (int i = 0; i < lines.size(); i++) {
		qWarning("%s", qPrintable(lines.at(i)));
	}
}

void UpdateCoordinator::checkManually()
{
	if (!m_manager) return;
	m_dialog->present();
	/* A manual check ignores both the 24 h interval and a skipped version. */
	m_manager->checkForUpdates(true);
}

void UpdateCoordinator::onStateChanged(int state)
{
	const UpdateState s = static_cast<UpdateState>(state);
	/* An automatic check stays out of the way until it has something worth
	   interrupting for. Failures and "up to date" are silent unless the user
	   asked, in which case the dialog is already open. */
	if (s == StateUpdateAvailable || s == StateReadyToInstall
		|| s == StateManagedInstallation)
	{
		if (m_dialog && !m_dialog->isVisible()) {
			m_dialog->present();
		}
	}
}

void UpdateCoordinator::onInstallRequested()
{
	if (!m_manager || !m_manager->canInstall()) return;

	const int unsaved = m_probe ? m_probe->countUnsavedItems() : 0;
	QString question = tr("PE-bear will close and restart to finish the update.");
	if (unsaved > 0) {
		question += QLatin1String("\n\n")
			+ tr("%n loaded file(s) have unsaved changes. They will be lost.", "", unsaved);
	}
	question += QLatin1String("\n\n") + tr("Continue?");

	const QMessageBox::StandardButton answer = QMessageBox::question(m_parentWindow,
		tr("Install the update"), question,
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (answer != QMessageBox::Yes) {
		/* Declining leaves the verified package exactly where it was, so the
		   user can come back to it without downloading again. */
		return;
	}
	m_manager->requestInstall();
}

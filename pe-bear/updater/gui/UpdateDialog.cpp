#include "UpdateDialog.h"
#include "../../REbear.h"

using namespace pe_bear::updater;

UpdateDialog::UpdateDialog(UpdateManager *manager, QWidget *parent)
	: QDialog(parent), m_manager(manager)
{
	setWindowTitle(tr("Updates"));
	setMinimumWidth(420);
	setLayout(&m_topLayout);

	QFont headlineFont = m_headline.font();
	headlineFont.setBold(true);
	m_headline.setFont(headlineFont);

	m_versions.setTextFormat(Qt::PlainText);
	m_status.setWordWrap(true);
	m_status.setTextFormat(Qt::PlainText);

	m_notesLink.setTextFormat(Qt::RichText);
	m_notesLink.setTextInteractionFlags(Qt::TextBrowserInteraction);
	m_notesLink.setOpenExternalLinks(true);
	m_notesLink.hide();

	m_progress.setRange(0, 100);
	m_progress.hide();

	m_downloadButton.setText(tr("Download"));
	m_installButton.setText(tr("Install and Restart"));
	m_cancelButton.setText(tr("Cancel"));
	m_laterButton.setText(tr("Later"));
	m_skipButton.setText(tr("Skip this version"));

	connect(&m_downloadButton, SIGNAL(clicked()), this, SLOT(onDownloadClicked()));
	connect(&m_installButton, SIGNAL(clicked()), this, SLOT(onInstallClicked()));
	connect(&m_cancelButton, SIGNAL(clicked()), this, SLOT(onCancelClicked()));
	connect(&m_laterButton, SIGNAL(clicked()), this, SLOT(onLaterClicked()));
	connect(&m_skipButton, SIGNAL(clicked()), this, SLOT(onSkipClicked()));

	QHBoxLayout *buttons = new QHBoxLayout();
	buttons->addWidget(&m_skipButton);
	buttons->addStretch();
	buttons->addWidget(&m_cancelButton);
	buttons->addWidget(&m_laterButton);
	buttons->addWidget(&m_downloadButton);
	buttons->addWidget(&m_installButton);

	m_topLayout.addWidget(&m_headline);
	m_topLayout.addWidget(&m_versions);
	m_topLayout.addWidget(&m_notesLink);
	m_topLayout.addWidget(&m_progress);
	m_topLayout.addWidget(&m_status);
	m_topLayout.addStretch();
	m_topLayout.addLayout(buttons);

	if (m_manager) {
		connect(m_manager, SIGNAL(stateChanged(int)), this, SLOT(onStateChanged(int)));
		connect(m_manager, SIGNAL(progress(qint64, qint64)), this, SLOT(onProgress(qint64, qint64)));
		connect(m_manager, SIGNAL(errorOccurred(int, QString)),
			this, SLOT(onErrorOccurred(int, QString)));
	}
	refresh();
}

void UpdateDialog::present()
{
	refresh();
	show();
	raise();
	activateWindow();
}

QString UpdateDialog::formatBytes(qint64 bytes)
{
	if (bytes < 1024) {
		return QString::number(bytes) + QLatin1String(" B");
	}
	const double kib = bytes / 1024.0;
	if (kib < 1024.0) {
		return QString::number(kib, 'f', 1) + QLatin1String(" KiB");
	}
	const double mib = kib / 1024.0;
	return QString::number(mib, 'f', 1) + QLatin1String(" MiB");
}

void UpdateDialog::setStatus(const QString &text, bool isError)
{
	m_status.setText(text);
	m_status.setStyleSheet(isError ? QLatin1String("color: " ERR_COLOR ";") : QString());
}

void UpdateDialog::onStateChanged(int)
{
	refresh();
}

void UpdateDialog::onProgress(qint64 done, qint64 total)
{
	if (total <= 0) {
		m_progress.setRange(0, 0); /* busy indicator */
		return;
	}
	m_progress.setRange(0, 100);
	m_progress.setValue(static_cast<int>((done * 100) / total));

	const QString what = (m_manager && m_manager->state() == StateVerifying)
		? tr("Verifying") : tr("Downloading");
	setStatus(what + QLatin1String(": ") + formatBytes(done)
		+ QLatin1String(" / ") + formatBytes(total));
}

void UpdateDialog::onErrorOccurred(int error, const QString &detail)
{
	QString text = updateErrorMessage(static_cast<UpdateError>(error));
	if (!detail.isEmpty()) {
		text += QLatin1String("\n") + detail;
	}
	setStatus(text, true);
}

void UpdateDialog::refresh()
{
	if (!m_manager) return;

	const UpdateState state = m_manager->state();
	const UpdateCandidate candidate = m_manager->candidate();
	const Version current = Version::current();

	m_versions.setText(tr("Installed version: ") + current.toString());
	m_notesLink.hide();

	if (candidate.release.isValid()) {
		m_versions.setText(tr("Installed version: ") + current.toString()
			+ QLatin1String("\n") + tr("Available version: ")
			+ candidate.release.version.toString()
			+ QLatin1String("\n") + tr("Package: ") + candidate.asset.name);
		if (candidate.release.htmlUrl.isValid()) {
			m_notesLink.setText(QLatin1String("<a href=\"")
				+ candidate.release.htmlUrl.toString() + QLatin1String("\">")
				+ tr("Release notes") + QLatin1String("</a>"));
			m_notesLink.show();
		}
	}

	const bool busy = m_manager->isBusy();
	m_progress.setVisible(state == StateDownloading || state == StateVerifying);
	m_cancelButton.setVisible(busy);
	m_downloadButton.setEnabled(m_manager->canDownload());
	m_downloadButton.setVisible(state == StateUpdateAvailable);
	/* The one state in which an installation may be started, ever. */
	m_installButton.setEnabled(m_manager->canInstall());
	m_installButton.setVisible(state == StateReadyToInstall);
	m_skipButton.setVisible(candidate.release.isValid()
		&& (state == StateUpdateAvailable || state == StateReadyToInstall));
	m_laterButton.setText(busy ? tr("Hide") : tr("Later"));

	switch (state) {
		case StateIdle:
			m_headline.setText(tr("Updates"));
			setStatus(QString());
			break;
		case StateChecking:
			m_headline.setText(tr("Checking for updates..."));
			setStatus(QString());
			break;
		case StateUpToDate:
			m_headline.setText(tr("PE-bear is up to date."));
			setStatus(QString());
			break;
		case StateUpdateAvailable:
			m_headline.setText(tr("A new version of PE-bear is available."));
			setStatus(tr("Nothing is downloaded until you choose to.")
				+ QLatin1String(" ")
				+ tr("Download size: ") + formatBytes(candidate.asset.size));
			break;
		case StateDownloading:
			m_headline.setText(tr("Downloading the update..."));
			break;
		case StateVerifying:
			m_headline.setText(tr("Verifying the download..."));
			break;
		case StateReadyToInstall:
			m_headline.setText(tr("The update is ready to install."));
			setStatus(tr("SHA-256 verified against the release metadata.")
				+ QLatin1String("\n")
				+ tr("Unsaved changes are never discarded: you will be asked first."));
			break;
		case StateNoCompatibleAsset:
			m_headline.setText(tr("A new version exists, but not for this build."));
			setStatus(updateErrorMessage(m_manager->lastError())
				+ QLatin1String("\n")
				+ tr("You can download it manually from the project page."), true);
			break;
		case StateManagedInstallation:
			m_headline.setText(tr("A new version of PE-bear is available."));
			/* The manager chose the error; it knows whether this is a package
			   manager's copy or one sitting on the Desktop, and the two must not
			   be explained with the same sentence. */
			setStatus(updateErrorMessage(m_manager->lastError())
				+ QLatin1String("\n") + m_manager->installation().detail
				+ QLatin1String("\n")
				+ tr("Update it the same way you installed it."), true);
			break;
		case StateFailed:
			m_headline.setText(tr("The update could not be completed."));
			onErrorOccurred(static_cast<int>(m_manager->lastError()), m_manager->lastErrorDetail());
			break;
		default:
			break;
	}
}

void UpdateDialog::onDownloadClicked()
{
	if (!m_manager) return;
	m_manager->startDownload();
}

void UpdateDialog::onInstallClicked()
{
	if (!m_manager || !m_manager->canInstall()) return;
	/* Consent is asked for here and confirmed again by the coordinator once it
	   has checked what would be lost. */
	emit installRequested();
}

void UpdateDialog::onCancelClicked()
{
	if (!m_manager) return;
	m_manager->cancel();
}

void UpdateDialog::onLaterClicked()
{
	/* "Later" hides the window and keeps whatever progress was made; it is not
	   a refusal, and it never throws away a verified package. */
	hide();
}

void UpdateDialog::onSkipClicked()
{
	if (!m_manager) return;
	const QMessageBox::StandardButton answer = QMessageBox::question(this,
		tr("Skip this version"),
		tr("PE-bear will stop offering this version.") + QLatin1String("\n")
		+ tr("You will still be told about later ones."),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (answer != QMessageBox::Yes) return;

	m_manager->skipCurrentVersion();
	hide();
}

#pragma once

#include "../../QtCompat.h"
#include "../UpdateManager.h"

/**
 * The single place where the user sees and steers an update.
 *
 * Every button maps to one explicit intent -- download, install, postpone,
 * skip this version, cancel -- and the dialog only ever reflects the state
 * machine; it never decides anything itself. "Install and Restart" is enabled
 * in exactly one state, ReadyToInstall, so there is no window in which a click
 * could start an installation from an unverified package.
 */
class UpdateDialog : public QDialog
{
	Q_OBJECT

public:
	UpdateDialog(pe_bear::updater::UpdateManager *manager, QWidget *parent = 0);

	/** Shows the dialog and brings it forward. */
	void present();

signals:
	/** The user consented to installing; the coordinator does the guarding. */
	void installRequested();

protected slots:
	void onStateChanged(int state);
	void onProgress(qint64 done, qint64 total);
	void onErrorOccurred(int error, const QString &detail);

private slots:
	void onDownloadClicked();
	void onInstallClicked();
	void onCancelClicked();
	void onLaterClicked();
	void onSkipClicked();

private:
	void refresh();
	void setStatus(const QString &text, bool isError = false);
	static QString formatBytes(qint64 bytes);

	pe_bear::updater::UpdateManager *m_manager;

	QVBoxLayout m_topLayout;
	QLabel m_headline;
	QLabel m_versions;
	QLabel m_status;
	QLabel m_notesLink;
	QProgressBar m_progress;

	QPushButton m_downloadButton;
	QPushButton m_installButton;
	QPushButton m_cancelButton;
	QPushButton m_laterButton;
	QPushButton m_skipButton;
};

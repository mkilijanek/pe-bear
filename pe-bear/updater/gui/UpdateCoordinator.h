#pragma once

#include "../../QtCompat.h"
#include "../UpdateManager.h"
#include "UpdateDialog.h"

/**
 * Something that knows whether the user would lose work right now.
 *
 * Implemented by the main window. Kept as an interface so the updater never
 * has to include the window, and so the rule "an update must not silently
 * discard an analysis" can be tested on its own.
 */
class IUnsavedWorkProbe
{
public:
	virtual ~IUnsavedWorkProbe() {}
	/** Number of loaded files with modifications that are not saved. */
	virtual int countUnsavedItems() const = 0;
};

/**
 * Connects the update state machine to the application.
 *
 * Owns the manager and the dialog, runs the once-a-day background check after
 * the main window is up, and is the only place that may turn a click on
 * "Install and Restart" into an actual installation -- after checking what
 * would be lost and asking.
 */
class UpdateCoordinator : public QObject
{
	Q_OBJECT

public:
	/** Delay before the automatic check, so startup stays unaffected. */
	static const int AUTO_CHECK_DELAY_MS = 5000;

	/**
	 * @param settings borrowed updater settings, owned by MainSettings
	 * @param probe    borrowed, may be NULL
	 */
	UpdateCoordinator(pe_bear::updater::UpdateSettings *settings,
		IUnsavedWorkProbe *probe, QWidget *parentWindow);

	pe_bear::updater::UpdateManager* manager() { return m_manager; }

	/** True while a verified package is waiting for the user's decision. */
	bool hasPendingInstall() const;

public slots:
	/** Called once the main window is visible. Never blocks startup. */
	void onApplicationReady();
	/** Help -> Check for Updates. */
	void checkManually();

private slots:
	void onAutoCheckTimeout();
	void onInstallRequested();
	void onStateChanged(int state);

private:
	pe_bear::updater::UpdateSettings *m_settings;
	IUnsavedWorkProbe *m_probe;
	QWidget *m_parentWindow;
	pe_bear::updater::UpdateManager *m_manager;
	UpdateDialog *m_dialog;
	bool m_started;
};

#pragma once

#include <QtCore>
#include "Version.h"

namespace pe_bear {
namespace updater {

/**
 * User-facing updater preferences.
 *
 * The defaults encode the v1 consent model: checking is on, because knowing a
 * security tool is out of date is itself useful; downloading is off, because
 * spending someone's bandwidth needs their say-so; and installing is not a
 * setting at all -- it is always an explicit action, every single time.
 *
 * Stored under the "Updates" group of the application's existing QSettings,
 * so it travels with the rest of the configuration.
 */
class UpdateSettings
{
public:
	static const char* SETTINGS_GROUP;
	static const int DEFAULT_CHECK_INTERVAL_HOURS = 24;

	UpdateSettings();

	bool isAutoCheckEnabled() const { return m_autoCheck; }
	void setAutoCheckEnabled(bool enabled) { m_autoCheck = enabled; }

	bool isAutoDownloadEnabled() const { return m_autoDownload; }
	void setAutoDownloadEnabled(bool enabled) { m_autoDownload = enabled; }

	int checkIntervalHours() const { return m_checkIntervalHours; }
	void setCheckIntervalHours(int hours);

	QDateTime lastCheck() const { return m_lastCheck; }
	void setLastCheck(const QDateTime &when) { m_lastCheck = when; }

	/** Version the user asked not to be reminded about; empty when none. */
	QString skippedVersion() const { return m_skippedVersion; }

	/**
	 * GitHub repository to take releases from, "owner/name"; empty means the
	 * build's default. Whoever can write this file can point the updater at
	 * their own releases -- which is no more than they could already do to a
	 * portable installation they can write to, and the only kind this
	 * updater replaces. Everything else (TLS, host allowlist, digest, the
	 * helper's own checks) applies to that repository exactly as to the
	 * default.
	 */
	QString repository() const { return m_repository; }
	/** Refuses anything that is not a plausible "owner/name"; empty clears. */
	bool setRepository(const QString &repository);
	/** The one to use: the override, or @p buildDefault when none is set. */
	QString effectiveRepository(const QString &buildDefault) const;
	/**
	 * "owner/name" as GitHub spells them: owner of letters, digits and
	 * hyphens; name of letters, digits, hyphens, dots and underscores; no
	 * dot-only names, no second slash, nothing a URL would re-interpret.
	 */
	static bool isValidRepository(const QString &repository);
	void setSkippedVersion(const Version &version);
	void clearSkippedVersion() { m_skippedVersion.clear(); }
	bool isVersionSkipped(const Version &version) const;

	/**
	 * True when an automatic check may run now: enabled, and the interval has
	 * elapsed since the last one. A clock that has moved backwards (or a
	 * corrupt timestamp) counts as due rather than blocking checks forever.
	 */
	bool isAutomaticCheckDue(const QDateTime &now) const;

	void read(QSettings &settings);
	void write(QSettings &settings) const;

private:
	bool m_autoCheck;
	bool m_autoDownload;
	int m_checkIntervalHours;
	QDateTime m_lastCheck;
	QString m_skippedVersion;
	QString m_repository;
};

}; // namespace updater
}; // namespace pe_bear

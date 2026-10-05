#include "UpdateSettings.h"

using namespace pe_bear::updater;

const char* UpdateSettings::SETTINGS_GROUP = "Updates";
const int UpdateSettings::DEFAULT_CHECK_INTERVAL_HOURS;

namespace {
	const char* KEY_AUTO_CHECK = "AutoCheck";
	const char* KEY_AUTO_DOWNLOAD = "AutoDownload";
	const char* KEY_INTERVAL_HOURS = "CheckIntervalHours";
	const char* KEY_LAST_CHECK = "LastCheck";
	const char* KEY_SKIPPED_VERSION = "SkippedVersion";
	const char* KEY_REPOSITORY = "Repository";
	const int MAX_OWNER_LENGTH = 39;
	const int MAX_NAME_LENGTH = 100;

	bool isOwnerChar(QChar c)
	{
		return (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
			|| (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
			|| (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
			|| c == QLatin1Char('-');
	}
	bool isNameChar(QChar c)
	{
		return isOwnerChar(c) || c == QLatin1Char('.') || c == QLatin1Char('_');
	}

	const int MIN_INTERVAL_HOURS = 1;
	const int MAX_INTERVAL_HOURS = 24 * 30;
}; // namespace

UpdateSettings::UpdateSettings()
	: m_autoCheck(true), m_autoDownload(false),
	m_checkIntervalHours(DEFAULT_CHECK_INTERVAL_HOURS)
{
}

void UpdateSettings::setCheckIntervalHours(int hours)
{
	if (hours < MIN_INTERVAL_HOURS) hours = MIN_INTERVAL_HOURS;
	if (hours > MAX_INTERVAL_HOURS) hours = MAX_INTERVAL_HOURS;
	m_checkIntervalHours = hours;
}

void UpdateSettings::setSkippedVersion(const Version &version)
{
	m_skippedVersion = version.isValid() ? version.toString() : QString();
}

bool UpdateSettings::isValidRepository(const QString &repository)
{
	const int slash = repository.indexOf(QLatin1Char('/'));
	if (slash <= 0) return false;
	const QString owner = repository.left(slash);
	const QString name = repository.mid(slash + 1);
	if (owner.length() > MAX_OWNER_LENGTH || name.isEmpty() || name.length() > MAX_NAME_LENGTH) return false;
	if (owner.startsWith(QLatin1Char('-')) || owner.endsWith(QLatin1Char('-'))) return false;
	for (int i = 0; i < owner.length(); i++) {
		if (!isOwnerChar(owner.at(i))) return false;
	}
	for (int i = 0; i < name.length(); i++) {
		if (!isNameChar(name.at(i))) return false;
	}
	if (name == QLatin1String(".") || name == QLatin1String("..")) return false;
	return true;
}

bool UpdateSettings::setRepository(const QString &repository)
{
	const QString trimmed = repository.trimmed();
	if (trimmed.isEmpty()) {
		m_repository.clear();
		return true;
	}
	if (!isValidRepository(trimmed)) return false;
	m_repository = trimmed;
	return true;
}

QString UpdateSettings::effectiveRepository(const QString &buildDefault) const
{
	return m_repository.isEmpty() ? buildDefault : m_repository;
}

bool UpdateSettings::isVersionSkipped(const Version &version) const
{
	if (m_skippedVersion.isEmpty() || !version.isValid()) return false;
	const Version skipped = Version::fromString(m_skippedVersion);
	if (!skipped.isValid()) return false;
	/* Skipping 0.7.3 must not also silence 0.7.4. */
	return (skipped == version);
}

bool UpdateSettings::isAutomaticCheckDue(const QDateTime &now) const
{
	if (!m_autoCheck) return false;
	if (!m_lastCheck.isValid()) return true;
	if (!now.isValid()) return false;
	if (m_lastCheck > now) return true; /* clock moved back, or a bad stored value */

	const qint64 elapsedSecs = m_lastCheck.secsTo(now);
	return elapsedSecs >= (static_cast<qint64>(m_checkIntervalHours) * 3600);
}

void UpdateSettings::read(QSettings &settings)
{
	settings.beginGroup(QLatin1String(SETTINGS_GROUP));
	m_autoCheck = settings.value(QLatin1String(KEY_AUTO_CHECK), true).toBool();
	m_autoDownload = settings.value(QLatin1String(KEY_AUTO_DOWNLOAD), false).toBool();
	setCheckIntervalHours(settings.value(QLatin1String(KEY_INTERVAL_HOURS),
		DEFAULT_CHECK_INTERVAL_HOURS).toInt());

	const QString lastCheckStr = settings.value(QLatin1String(KEY_LAST_CHECK)).toString();
	m_lastCheck = lastCheckStr.isEmpty()
		? QDateTime()
		: QDateTime::fromString(lastCheckStr, Qt::ISODate);

	const QString skipped = settings.value(QLatin1String(KEY_SKIPPED_VERSION)).toString();
	/* Anything unparseable in the config is dropped rather than trusted. */
	m_skippedVersion = Version::fromString(skipped).isValid() ? skipped : QString();
	const QString repository = settings.value(QLatin1String(KEY_REPOSITORY)).toString();
	m_repository = isValidRepository(repository) ? repository : QString();
	settings.endGroup();
}

void UpdateSettings::write(QSettings &settings) const
{
	settings.beginGroup(QLatin1String(SETTINGS_GROUP));
	settings.setValue(QLatin1String(KEY_AUTO_CHECK), m_autoCheck);
	settings.setValue(QLatin1String(KEY_AUTO_DOWNLOAD), m_autoDownload);
	settings.setValue(QLatin1String(KEY_INTERVAL_HOURS), m_checkIntervalHours);
	settings.setValue(QLatin1String(KEY_LAST_CHECK),
		m_lastCheck.isValid() ? m_lastCheck.toString(Qt::ISODate) : QString());
	settings.setValue(QLatin1String(KEY_SKIPPED_VERSION), m_skippedVersion);
	settings.setValue(QLatin1String(KEY_REPOSITORY), m_repository);
	settings.endGroup();
}

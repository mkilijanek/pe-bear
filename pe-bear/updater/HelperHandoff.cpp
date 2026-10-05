#include "HelperHandoff.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUrl>

namespace pe_bear {
namespace updater {

namespace {

	const char* KEY_VERSION      = "version";
	const char* KEY_RUN_ID       = "runId";
	const char* KEY_PACKAGE      = "packagePath";
	const char* KEY_SHA256       = "packageSha256";
	const char* KEY_SIZE         = "packageSize";
	const char* KEY_TARGET       = "targetDir";
	const char* KEY_EXPECTED     = "expectedVersion";
	const char* KEY_PARENT_PID   = "parentPid";
	const char* KEY_RELAUNCH     = "relaunch";
	const char* KEY_CREATED_AT   = "createdAtUtc";
	const char* KEY_RELEASE_TAG  = "releaseTag";
	const char* KEY_ASSET_NAME   = "assetName";
	const char* KEY_ASSET_URL    = "assetUrl";

	bool isHex(const QString &s, int length)
	{
		if (s.length() != length) return false;
		for (int i = 0; i < s.length(); i++) {
			const QChar c = s.at(i);
			const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
				|| (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
			if (!hex) return false;
		}
		return true;
	}

}; // namespace

QString HelperHandoff::fileName()
{
	return QLatin1String("handoff.json");
}

QString HelperHandoff::generateRunId()
{
	QString id;
	id.reserve(RUN_ID_HEX_LENGTH);
	while (id.length() < RUN_ID_HEX_LENGTH) {
		id += QString::number(QRandomGenerator::global()->generate(), 16)
			.rightJustified(8, QLatin1Char('0'));
	}
	return id.left(RUN_ID_HEX_LENGTH);
}

bool HelperHandoff::isValid() const
{
	if (version != CURRENT_VERSION) return false;
	if (!isHex(runId, RUN_ID_HEX_LENGTH)) return false;
	if (!isHex(packageSha256, 64)) return false;
	if (packageSize <= 0) return false;
	if (packagePath.isEmpty() || targetDir.isEmpty()) return false;
	/* Relative paths would be resolved against the helper's working
	   directory, which is not the one PE-bear was thinking of. */
	if (!QFileInfo(packagePath).isAbsolute()) return false;
	if (!QFileInfo(targetDir).isAbsolute()) return false;
	if (parentPid <= 0) return false;
	if (!expected().isValid()) return false;
	if (createdAtUtc.isEmpty()) return false;

	const QUrl url(assetUrl);
	if (!url.isValid() || url.scheme() != QLatin1String("https")) return false;
	return true;
}

bool HelperHandoff::hasUsableTimestamp() const
{
	return QDateTime::fromString(createdAtUtc, Qt::ISODate).isValid();
}

qint64 HelperHandoff::ageSeconds(const QDateTime &now) const
{
	const QDateTime created = QDateTime::fromString(createdAtUtc, Qt::ISODate);
	if (!created.isValid()) return -1;
	return created.secsTo(now);
}

QByteArray HelperHandoff::toJson() const
{
	QJsonObject o;
	o[QLatin1String(KEY_VERSION)] = version;
	o[QLatin1String(KEY_RUN_ID)] = runId;
	o[QLatin1String(KEY_PACKAGE)] = packagePath;
	o[QLatin1String(KEY_SHA256)] = packageSha256;
	o[QLatin1String(KEY_SIZE)] = static_cast<double>(packageSize);
	o[QLatin1String(KEY_TARGET)] = targetDir;
	o[QLatin1String(KEY_EXPECTED)] = expectedVersion;
	o[QLatin1String(KEY_PARENT_PID)] = static_cast<double>(parentPid);
	o[QLatin1String(KEY_RELAUNCH)] = relaunch;
	o[QLatin1String(KEY_CREATED_AT)] = createdAtUtc;
	o[QLatin1String(KEY_RELEASE_TAG)] = releaseTag;
	o[QLatin1String(KEY_ASSET_NAME)] = assetName;
	o[QLatin1String(KEY_ASSET_URL)] = assetUrl;

	return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

HelperHandoff HelperHandoff::fromJson(const QByteArray &data, bool *ok)
{
	if (ok) *ok = false;
	HelperHandoff h;

	QJsonParseError error;
	const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
	if (error.error != QJsonParseError::NoError || !doc.isObject()) return h;

	const QJsonObject o = doc.object();

	/* Each field is checked for its type before it is taken. A JSON value of
	   the wrong type converts silently -- a string "0" becomes 0, a missing
	   boolean becomes false -- and the fields here decide what gets deleted,
	   so a document that is merely parseable is not good enough. */
	const QJsonValue version = o.value(QLatin1String(KEY_VERSION));
	const QJsonValue size = o.value(QLatin1String(KEY_SIZE));
	const QJsonValue pid = o.value(QLatin1String(KEY_PARENT_PID));
	const QJsonValue relaunch = o.value(QLatin1String(KEY_RELAUNCH));
	if (!version.isDouble() || !size.isDouble() || !pid.isDouble()) return h;
	/* Absent means absent, not false: a handoff that forgot to say whether to
	   relaunch is malformed, because defaulting either way would be a guess
	   about what the user asked for. */
	if (!relaunch.isBool()) return h;

	h.version = version.toInt();
	h.packageSize = static_cast<qint64>(size.toDouble());
	h.parentPid = static_cast<qint64>(pid.toDouble());
	h.relaunch = relaunch.toBool();

	h.runId = o.value(QLatin1String(KEY_RUN_ID)).toString();
	h.packagePath = o.value(QLatin1String(KEY_PACKAGE)).toString();
	h.packageSha256 = o.value(QLatin1String(KEY_SHA256)).toString();
	h.targetDir = o.value(QLatin1String(KEY_TARGET)).toString();
	h.expectedVersion = o.value(QLatin1String(KEY_EXPECTED)).toString();
	h.createdAtUtc = o.value(QLatin1String(KEY_CREATED_AT)).toString();
	h.releaseTag = o.value(QLatin1String(KEY_RELEASE_TAG)).toString();
	h.assetName = o.value(QLatin1String(KEY_ASSET_NAME)).toString();
	h.assetUrl = o.value(QLatin1String(KEY_ASSET_URL)).toString();

	if (ok) *ok = h.isValid();
	return h;
}

}; // namespace updater
}; // namespace pe_bear

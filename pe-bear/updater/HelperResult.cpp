#include "HelperResult.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace pe_bear {
namespace updater {

QString HelperResult::fileName()
{
	return QLatin1String("last-result.json");
}

QString HelperResult::pathIn(const UpdatePaths &paths)
{
	return QDir::cleanPath(paths.root() + QDir::separator() + fileName());
}

bool HelperResult::isValid() const
{
	if (version != CURRENT_VERSION) return false;
	if (runId.isEmpty() || result.isEmpty()) return false;
	if (message.isEmpty()) return false;
	return true;
}

QByteArray HelperResult::toJson() const
{
	QJsonObject o;
	o[QLatin1String("version")] = version;
	o[QLatin1String("runId")] = runId;
	o[QLatin1String("result")] = result;
	o[QLatin1String("exitCode")] = exitCode;
	o[QLatin1String("leftUntouched")] = leftUntouched;
	o[QLatin1String("message")] = message;
	o[QLatin1String("detail")] = detail;
	o[QLatin1String("finishedAtUtc")] = finishedAtUtc;
	return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

HelperResult HelperResult::fromJson(const QByteArray &data, bool *ok)
{
	if (ok) *ok = false;
	HelperResult r;

	QJsonParseError error;
	const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
	if (error.error != QJsonParseError::NoError || !doc.isObject()) return r;
	const QJsonObject o = doc.object();

	/* Typed, like the handoff: a missing boolean reads as false and a string
	   "0" as 0, and leftUntouched is the one field that decides what the user
	   is told about their installation. */
	const QJsonValue version = o.value(QLatin1String("version"));
	const QJsonValue exitCode = o.value(QLatin1String("exitCode"));
	const QJsonValue untouched = o.value(QLatin1String("leftUntouched"));
	if (!version.isDouble() || !exitCode.isDouble() || !untouched.isBool()) return r;

	r.version = version.toInt();
	r.exitCode = exitCode.toInt();
	r.leftUntouched = untouched.toBool();
	r.runId = o.value(QLatin1String("runId")).toString();
	r.result = o.value(QLatin1String("result")).toString();
	r.message = o.value(QLatin1String("message")).toString();
	r.detail = o.value(QLatin1String("detail")).toString();
	r.finishedAtUtc = o.value(QLatin1String("finishedAtUtc")).toString();

	if (ok) *ok = r.isValid();
	return r;
}

bool HelperResult::write(IFileSystem &fs, const UpdatePaths &paths, const HelperResult &result)
{
	if (!result.isValid()) return false;
	/* writeFile restricts what it writes to the owner; that is the right
	   setting for this file, which names paths inside the user's profile. */
	return fs.writeFile(pathIn(paths), result.toJson());
}

HelperResult HelperResult::consume(IFileSystem &fs, const UpdatePaths &paths,
		bool *ok, bool *unreadable)
{
	if (ok) *ok = false;
	if (unreadable) *unreadable = false;

	const QString path = pathIn(paths);
	if (!fs.exists(path)) return HelperResult();

	const QByteArray data = fs.readFile(path);
	/* Removed before it is judged, so that a file this process cannot parse
	   does not get another chance to be unparseable at every start. */
	fs.removeFile(path);

	bool parsed = false;
	const HelperResult r = fromJson(data, &parsed);
	if (!parsed) {
		if (unreadable) *unreadable = true;
		return HelperResult();
	}
	if (ok) *ok = true;
	return r;
}

}; // namespace updater
}; // namespace pe_bear

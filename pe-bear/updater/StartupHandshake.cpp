#include "StartupHandshake.h"
#include "Random.h"

using namespace pe_bear::updater;

namespace {

	const int JOURNAL_FORMAT = 1;
	/* 32 hex characters of randomness. Long enough that guessing is not a
	   consideration, short enough to paste into a command line. */
	const int TOKEN_HEX_CHARS = 32;

	struct VerdictName { StartupHandshake::Verdict v; const char *name; };

	const VerdictName VERDICTS[] = {
		{ StartupHandshake::Accepted,        "Accepted"        },
		{ StartupHandshake::NoResponse,      "NoResponse"      },
		{ StartupHandshake::Malformed,       "Malformed"       },
		{ StartupHandshake::TokenMismatch,   "TokenMismatch"   },
		{ StartupHandshake::VersionMismatch, "VersionMismatch" },
		{ StartupHandshake::ReportedFailure, "ReportedFailure" }
	};
	const size_t VERDICT_COUNT = sizeof(VERDICTS) / sizeof(VERDICTS[0]);

}; // namespace

QString StartupHandshake::verdictToString(Verdict v)
{
	for (size_t i = 0; i < VERDICT_COUNT; i++) {
		if (VERDICTS[i].v == v) return QLatin1String(VERDICTS[i].name);
	}
	return QLatin1String("Invalid");
}

QString StartupHandshake::verdictMessage(Verdict v)
{
	switch (v) {
		case Accepted:
			return QString();
		case NoResponse:
			return QCoreApplication::translate("Updater",
				"The updated PE-bear did not confirm that it started.");
		case Malformed:
			return QCoreApplication::translate("Updater",
				"The updated PE-bear left an unreadable startup report.");
		case TokenMismatch:
			return QCoreApplication::translate("Updater",
				"The startup report belongs to a different update attempt.");
		case VersionMismatch:
			return QCoreApplication::translate("Updater",
				"The installed PE-bear reports a different version than the update contained.");
		case ReportedFailure:
			return QCoreApplication::translate("Updater",
				"The updated PE-bear started but reported that it had not initialised correctly.");
		default:
			return QCoreApplication::translate("Updater", "The update could not be confirmed.");
	}
}

//----------------------------------------------------------------------

QByteArray StartupHandshake::Request::toJson() const
{
	QJsonObject o;
	o.insert(QLatin1String("handshakeVersion"), JOURNAL_FORMAT);
	o.insert(QLatin1String("token"), token);
	o.insert(QLatin1String("expectedVersion"), expectedVersion);
	o.insert(QLatin1String("responsePath"), responsePath);
	return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

StartupHandshake::Request StartupHandshake::Request::fromJson(const QByteArray &data, bool *ok)
{
	if (ok) *ok = false;
	Request r;

	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) return r;
	const QJsonObject o = doc.object();
	if (o.value(QLatin1String("handshakeVersion")).toInt(0) != JOURNAL_FORMAT) return r;

	r.token = o.value(QLatin1String("token")).toString();
	r.expectedVersion = o.value(QLatin1String("expectedVersion")).toString();
	r.responsePath = o.value(QLatin1String("responsePath")).toString();
	if (!r.isValid()) return Request();

	if (ok) *ok = true;
	return r;
}

QByteArray StartupHandshake::Response::toJson() const
{
	QJsonObject o;
	o.insert(QLatin1String("handshakeVersion"), JOURNAL_FORMAT);
	o.insert(QLatin1String("token"), token);
	o.insert(QLatin1String("version"), version);
	o.insert(QLatin1String("started"), started);
	if (!detail.isEmpty()) o.insert(QLatin1String("detail"), detail);
	return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

StartupHandshake::Response StartupHandshake::Response::fromJson(const QByteArray &data, bool *ok)
{
	if (ok) *ok = false;
	Response r;

	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) return r;
	const QJsonObject o = doc.object();
	if (o.value(QLatin1String("handshakeVersion")).toInt(0) != JOURNAL_FORMAT) return r;

	const QJsonValue tokenValue = o.value(QLatin1String("token"));
	const QJsonValue startedValue = o.value(QLatin1String("started"));
	/* Both are mandatory. A response with no "started" field must not default
	   to true, and one with no token must not be treated as matching. */
	if (!tokenValue.isString() || !startedValue.isBool()) return r;

	r.token = tokenValue.toString();
	r.version = o.value(QLatin1String("version")).toString();
	r.started = startedValue.toBool();
	r.detail = o.value(QLatin1String("detail")).toString();
	if (r.token.isEmpty()) return Response();

	if (ok) *ok = true;
	return r;
}

//----------------------------------------------------------------------

StartupHandshake::StartupHandshake(IFileSystem *fs)
	: m_fs(fs)
{
}

bool StartupHandshake::fail(const QString &why) const
{
	m_lastError = why;
	return false;
}

bool StartupHandshake::createRequest(const QString &requestPath, const QString &responsePath,
	const Version &expectedVersion, Request *out)
{
	if (!m_fs) return fail(QLatin1String("no filesystem"));
	if (requestPath.isEmpty() || responsePath.isEmpty()) {
		return fail(QLatin1String("the handshake needs both a request and a response path"));
	}
	if (!expectedVersion.isValid()) {
		return fail(QLatin1String("the expected version is not known"));
	}

	Request r;
	r.token = randomHex(TOKEN_HEX_CHARS);
	r.expectedVersion = expectedVersion.toString();
	r.responsePath = responsePath;
	if (!r.isValid()) return fail(QLatin1String("could not build the request"));

	/* A response left over from an earlier attempt would be read as this
	   attempt's answer if the token happened to be reused; it cannot be, but
	   removing it also keeps a stale file from confusing someone reading the
	   directory afterwards. */
	if (m_fs->exists(responsePath) && !m_fs->removeFile(responsePath)) {
		return fail(QLatin1String("could not clear the previous response: ") + m_fs->lastError());
	}
	if (!m_fs->writeFile(requestPath, r.toJson())) {
		return fail(QLatin1String("could not write the request: ") + m_fs->lastError());
	}
	m_fs->restrictToOwner(requestPath);

	if (out) *out = r;
	return true;
}

StartupHandshake::Verdict StartupHandshake::verifyResponse(const Request &request) const
{
	if (!m_fs) { fail(QLatin1String("no filesystem")); return NoResponse; }
	if (!request.isValid()) { fail(QLatin1String("the request is incomplete")); return Malformed; }

	if (!m_fs->exists(request.responsePath)) return NoResponse;

	const QByteArray data = m_fs->readFile(request.responsePath);
	/* Present but empty is a failure, not an absence: the build got far enough
	   to create the file and no further. */
	if (data.isEmpty()) return Malformed;

	bool ok = false;
	const Response response = Response::fromJson(data, &ok);
	if (!ok) return Malformed;

	/* Checked before anything else it says. A response from another attempt
	   must not be able to influence this one, whatever it claims. */
	if (response.token != request.token) return TokenMismatch;

	if (!response.started) return ReportedFailure;

	/* Compared as parsed versions, not as strings: "0.7.3" and "0.7.3.0" are
	   the same release, and a textual comparison would roll back a perfectly
	   good installation over a formatting difference. */
	const Version reported = Version::fromString(response.version);
	const Version expected = Version::fromString(request.expectedVersion);
	if (!reported.isValid() || !expected.isValid()) return VersionMismatch;
	if (reported != expected) return VersionMismatch;

	return Accepted;
}

void StartupHandshake::cleanUp(const QString &requestPath, const Request &request)
{
	if (!m_fs) return;
	/* Best effort on both: a leftover handshake file is untidy, never harmful,
	   and must not turn a successful update into a failure. */
	if (!requestPath.isEmpty()) m_fs->removeFile(requestPath);
	if (!request.responsePath.isEmpty()) m_fs->removeFile(request.responsePath);
}

bool StartupHandshake::respond(const QString &requestPath, const Version &actualVersion,
	bool initialisedSuccessfully, const QString &detail)
{
	if (!m_fs) return fail(QLatin1String("no filesystem"));
	if (requestPath.isEmpty()) return fail(QLatin1String("no request path"));
	if (!m_fs->exists(requestPath)) return fail(QLatin1String("no handshake request to answer"));

	bool ok = false;
	const Request request = Request::fromJson(m_fs->readFile(requestPath), &ok);
	if (!ok) return fail(QLatin1String("the handshake request is unreadable"));

	Response response;
	/* Echoed, never regenerated: the token is the helper's proof that this
	   answer belongs to the attempt it started. */
	response.token = request.token;
	response.version = actualVersion.toString();
	response.started = initialisedSuccessfully;
	response.detail = detail;

	/* A mismatch is reported rather than withheld. Writing nothing would leave
	   the helper waiting for a timeout and then guessing, when the new build
	   knows exactly what is wrong. */
	if (!m_fs->writeFile(request.responsePath, response.toJson())) {
		return fail(QLatin1String("could not write the response: ") + m_fs->lastError());
	}
	return true;
}

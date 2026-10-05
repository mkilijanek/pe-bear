/*
 * Covers the startup-validation handshake of #20.
 *
 * The requirement being tested: elapsed process time is not a criterion. A
 * build that starts and dies has not validated itself, so the helper must only
 * accept a specific, attempt-bound statement -- and must reject every near
 * miss, including the ones that look like success.
 */
#include <QtTest>
#include "../StartupHandshake.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	const char* REQ  = "/var/pe-bear-updates/transactions/tx-1.request.json";
	const char* RESP = "/var/pe-bear-updates/transactions/tx-1.response.json";

	Version v(const char *s) { return Version::fromString(QLatin1String(s)); }

}; // namespace

class TestStartupHandshake : public QObject
{
	Q_OBJECT

private slots:
	void theHappyPathIsAccepted();
	void tokenIsLongAndDiffersBetweenAttempts();

	void noResponseIsNotSuccess();
	void anEmptyResponseIsMalformedNotAbsent();
	void garbageIsMalformed();
	void aResponseFromAnotherAttemptIsRejected();
	void aStaleResponseIsClearedBeforeTheAttempt();
	void aResponseWithoutAStartedFieldIsNotTreatedAsStarted();
	void aResponseWithoutATokenIsRejected();
	void aWrongFormatVersionIsMalformed();

	void aBuildReportingFailureIsRejected();
	void aWrongVersionIsRejected();
	void equivalentVersionSpellingsAreAccepted();
	void anUnparseableReportedVersionIsRejected();

	void respondEchoesTheTokenRatherThanInventingOne();
	void respondReportsAMismatchInsteadOfStayingSilent();
	void respondRefusesWithoutARequest();
	void createRequestRefusesAnUnknownVersion();
	void cleanUpRemovesBothFiles();
	void everyVerdictHasAMessage();
};

void TestStartupHandshake::theHappyPathIsAccepted()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake helper(&fs);

	StartupHandshake::Request req;
	QVERIFY2(helper.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req),
		qPrintable(helper.lastError()));

	/* the new build's side, as a separate instance -- in reality a separate
	   process, at a different version */
	StartupHandshake app(&fs);
	QVERIFY2(app.respond(QLatin1String(REQ), v("0.7.3"), true), qPrintable(app.lastError()));

	QCOMPARE(helper.verifyResponse(req), StartupHandshake::Accepted);
}

void TestStartupHandshake::tokenIsLongAndDiffersBetweenAttempts()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake h(&fs);

	StartupHandshake::Request a, b;
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &a));
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &b));

	QVERIFY2(a.token.length() >= 32, qPrintable(QString::number(a.token.length())));
	QVERIFY2(a.token != b.token, "two attempts produced the same token");
}

void TestStartupHandshake::noResponseIsNotSuccess()
{
	/* The whole point: nothing written means not validated, however long the
	   process may have lived. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake h(&fs);

	StartupHandshake::Request req;
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));
	QCOMPARE(h.verifyResponse(req), StartupHandshake::NoResponse);
}

void TestStartupHandshake::anEmptyResponseIsMalformedNotAbsent()
{
	/* It got far enough to create the file and no further -- a different fact
	   from never having run, and worth distinguishing in a log. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake h(&fs);

	StartupHandshake::Request req;
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));
	fs.addFile(QLatin1String(RESP), QByteArray());
	QCOMPARE(h.verifyResponse(req), StartupHandshake::Malformed);
}

void TestStartupHandshake::garbageIsMalformed()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake h(&fs);

	StartupHandshake::Request req;
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));
	fs.addFile(QLatin1String(RESP), QByteArray("{ not json"));
	QCOMPARE(h.verifyResponse(req), StartupHandshake::Malformed);
}

void TestStartupHandshake::aResponseFromAnotherAttemptIsRejected()
{
	/* Without the token, a leftover file saying "all fine" would validate an
	   install that never ran. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake h(&fs);

	StartupHandshake::Request req;
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));

	StartupHandshake::Response forged;
	forged.token = QString(32, QLatin1Char('a'));
	forged.version = QLatin1String("0.7.3");
	forged.started = true;
	fs.addFile(QLatin1String(RESP), forged.toJson());

	QCOMPARE(h.verifyResponse(req), StartupHandshake::TokenMismatch);
}

void TestStartupHandshake::aStaleResponseIsClearedBeforeTheAttempt()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	fs.addFile(QLatin1String(RESP), QByteArray("leftover from last time"));

	StartupHandshake h(&fs);
	StartupHandshake::Request req;
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));
	QVERIFY2(!fs.hasFile(QLatin1String(RESP)), "a stale response survived into the new attempt");
	QCOMPARE(h.verifyResponse(req), StartupHandshake::NoResponse);
}

void TestStartupHandshake::aResponseWithoutAStartedFieldIsNotTreatedAsStarted()
{
	/* A missing boolean must not default to true: that would turn a truncated
	   report into a successful validation. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake h(&fs);

	StartupHandshake::Request req;
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));

	const QString json = QString(
		"{\"handshakeVersion\":1,\"token\":\"%1\",\"version\":\"0.7.3\"}").arg(req.token);
	fs.addFile(QLatin1String(RESP), json.toUtf8());
	QCOMPARE(h.verifyResponse(req), StartupHandshake::Malformed);
}

void TestStartupHandshake::aResponseWithoutATokenIsRejected()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake h(&fs);

	StartupHandshake::Request req;
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));
	fs.addFile(QLatin1String(RESP), QByteArray(
		"{\"handshakeVersion\":1,\"version\":\"0.7.3\",\"started\":true}"));
	QCOMPARE(h.verifyResponse(req), StartupHandshake::Malformed);
}

void TestStartupHandshake::aWrongFormatVersionIsMalformed()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake h(&fs);

	StartupHandshake::Request req;
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));
	const QString json = QString(
		"{\"handshakeVersion\":99,\"token\":\"%1\",\"version\":\"0.7.3\",\"started\":true}")
		.arg(req.token);
	fs.addFile(QLatin1String(RESP), json.toUtf8());
	QCOMPARE(h.verifyResponse(req), StartupHandshake::Malformed);
}

void TestStartupHandshake::aBuildReportingFailureIsRejected()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake helper(&fs);

	StartupHandshake::Request req;
	QVERIFY(helper.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));

	StartupHandshake app(&fs);
	QVERIFY(app.respond(QLatin1String(REQ), v("0.7.3"), false,
		QLatin1String("the signature database would not load")));

	QCOMPARE(helper.verifyResponse(req), StartupHandshake::ReportedFailure);
}

void TestStartupHandshake::aWrongVersionIsRejected()
{
	/* Catches installing the wrong package: the bytes verified, the program
	   started, and it is not what the update claimed to contain. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake helper(&fs);

	StartupHandshake::Request req;
	QVERIFY(helper.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));

	StartupHandshake app(&fs);
	QVERIFY(app.respond(QLatin1String(REQ), v("0.7.2"), true));

	QCOMPARE(helper.verifyResponse(req), StartupHandshake::VersionMismatch);
}

void TestStartupHandshake::equivalentVersionSpellingsAreAccepted()
{
	/* "0.7.3" and "0.7.3.0" are the same release. Comparing as text would roll
	   back a perfectly good installation over a formatting difference. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake helper(&fs);

	StartupHandshake::Request req;
	QVERIFY(helper.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));

	StartupHandshake::Response r;
	r.token = req.token;
	r.version = QLatin1String("0.7.3.0");
	r.started = true;
	fs.addFile(QLatin1String(RESP), r.toJson());

	QCOMPARE(helper.verifyResponse(req), StartupHandshake::Accepted);
}

void TestStartupHandshake::anUnparseableReportedVersionIsRejected()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake h(&fs);

	StartupHandshake::Request req;
	QVERIFY(h.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));

	StartupHandshake::Response r;
	r.token = req.token;
	r.version = QLatin1String("not-a-version");
	r.started = true;
	fs.addFile(QLatin1String(RESP), r.toJson());

	QCOMPARE(h.verifyResponse(req), StartupHandshake::VersionMismatch);
}

void TestStartupHandshake::respondEchoesTheTokenRatherThanInventingOne()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake helper(&fs);

	StartupHandshake::Request req;
	QVERIFY(helper.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));

	StartupHandshake app(&fs);
	QVERIFY(app.respond(QLatin1String(REQ), v("0.7.3"), true));

	bool ok = false;
	const StartupHandshake::Response written =
		StartupHandshake::Response::fromJson(fs.contentOf(QLatin1String(RESP)), &ok);
	QVERIFY(ok);
	QCOMPARE(written.token, req.token);
}

void TestStartupHandshake::respondReportsAMismatchInsteadOfStayingSilent()
{
	/* Writing nothing would leave the helper waiting for a timeout and then
	   guessing, when the new build knows exactly what is wrong. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake helper(&fs);

	StartupHandshake::Request req;
	QVERIFY(helper.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));

	StartupHandshake app(&fs);
	QVERIFY2(app.respond(QLatin1String(REQ), v("0.7.2"), true),
		"respond refused to report a version mismatch");
	QVERIFY2(fs.hasFile(QLatin1String(RESP)), "nothing was written for the helper to read");
	QCOMPARE(helper.verifyResponse(req), StartupHandshake::VersionMismatch);
}

void TestStartupHandshake::respondRefusesWithoutARequest()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake app(&fs);
	QVERIFY(!app.respond(QLatin1String(REQ), v("0.7.3"), true));
	QVERIFY(!app.lastError().isEmpty());
}

void TestStartupHandshake::createRequestRefusesAnUnknownVersion()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake h(&fs);
	StartupHandshake::Request req;
	QVERIFY2(!h.createRequest(QLatin1String(REQ), QLatin1String(RESP), Version(), &req),
		"a request was built with no version to expect");
}

void TestStartupHandshake::cleanUpRemovesBothFiles()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/pe-bear-updates/transactions"));
	StartupHandshake helper(&fs);

	StartupHandshake::Request req;
	QVERIFY(helper.createRequest(QLatin1String(REQ), QLatin1String(RESP), v("0.7.3"), &req));
	StartupHandshake app(&fs);
	QVERIFY(app.respond(QLatin1String(REQ), v("0.7.3"), true));
	QVERIFY(fs.hasFile(QLatin1String(REQ)));
	QVERIFY(fs.hasFile(QLatin1String(RESP)));

	helper.cleanUp(QLatin1String(REQ), req);
	QVERIFY(!fs.hasFile(QLatin1String(REQ)));
	QVERIFY(!fs.hasFile(QLatin1String(RESP)));
}

void TestStartupHandshake::everyVerdictHasAMessage()
{
	for (int i = StartupHandshake::NoResponse; i < StartupHandshake::VERDICTS_COUNT; i++) {
		const StartupHandshake::Verdict v = StartupHandshake::Verdict(i);
		QVERIFY2(StartupHandshake::verdictToString(v) != QLatin1String("Invalid"),
			qPrintable(QString::number(i)));
		QVERIFY2(!StartupHandshake::verdictMessage(v).isEmpty(),
			qPrintable(StartupHandshake::verdictToString(v)));
	}
	QVERIFY(StartupHandshake::verdictMessage(StartupHandshake::Accepted).isEmpty());
}

QTEST_GUILESS_MAIN(TestStartupHandshake)
#include "tst_startuphandshake.moc"

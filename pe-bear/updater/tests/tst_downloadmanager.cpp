/*
 * Covers the download path of PR #3 (UT-11, UT-12, SEC-01, SEC-02).
 *
 * Run against a real socket rather than a mock, because the behaviour that
 * matters -- partial files, a server that oversends, a cancelled transfer --
 * only exists at the transport level. The host policy is overridden for the
 * local server and nowhere else.
 */
#include <QtTest>
#include "../DownloadManager.h"
#include "../ReleaseClient.h"
#include "TestHttpServer.h"

using namespace pe_bear::updater;

namespace {

	/**
	 * Accepts the loopback test server in addition to nothing else. The
	 * production policy is inherited untouched for every other URL.
	 */
	class LoopbackDownloadManager : public DownloadManager
	{
	public:
		explicit LoopbackDownloadManager(QObject *parent = NULL) : DownloadManager(parent) {}

	protected:
		virtual bool isAcceptableUrl(const QUrl &url) const
		{
			if (url.host() == QLatin1String("127.0.0.1")) return true;
			return DownloadManager::isAcceptableUrl(url);
		}
	};

	ReleaseAsset assetFor(const QUrl &url, qint64 size)
	{
		ReleaseAsset a;
		a.name = QLatin1String("PE-bear_0.7.3_qt5_x64_linux.tar.xz");
		a.downloadUrl = url;
		a.size = size;
		a.sha256 = QString(64, QLatin1Char('a'));
		return a;
	}

}; // namespace

class TestDownloadManager : public QObject
{
	Q_OBJECT

private slots:
	void downloadsAPackageAndRenamesItIntoPlace();
	void reportsProgressWhileDownloading();
	void removesThePartialFileWhenTheServerOversends();
	void removesThePartialFileWhenTheTransferIsTruncated();
	void removesThePartialFileWhenCancelled();
	void refusesAnUnexpectedHost();
	void refusesAnImplausibleDeclaredSize();
	void refusesAnIncompleteAsset();
	void reportsAnHttpError();
	void refusesToRunTwoDownloadsAtOnce();
	void doesNotResumeALeftoverPartialFile();
};

void TestDownloadManager::downloadsAPackageAndRenamesItIntoPlace()
{
	QByteArray body(64 * 1024 + 7, 'Z');
	TestHttpServer server;
	server.setBody(body);
	QVERIFY(server.listen(QHostAddress::LocalHost));

	QTemporaryDir dir;
	LoopbackDownloadManager manager;
	QSignalSpy finished(&manager, SIGNAL(finished(QString)));
	QSignalSpy failed(&manager, SIGNAL(failed(int, QString)));

	manager.start(assetFor(server.urlFor("/pkg"), body.size()), dir.path());
	QVERIFY(finished.wait(10000));
	QCOMPARE(failed.count(), 0);

	const QString path = finished.first().first().toString();
	QVERIFY(QFile::exists(path));
	QVERIFY2(!path.endsWith(QLatin1String(".part")), "the partial name leaked into the result");
	QVERIFY2(!QFile::exists(path + QLatin1String(".part")), "the .part file was left behind");
	QCOMPARE(QFileInfo(path).size(), qint64(body.size()));
}

void TestDownloadManager::reportsProgressWhileDownloading()
{
	QByteArray body(256 * 1024, 'P');
	TestHttpServer server;
	server.setBody(body);
	QVERIFY(server.listen(QHostAddress::LocalHost));

	QTemporaryDir dir;
	LoopbackDownloadManager manager;
	QSignalSpy progress(&manager, SIGNAL(progress(qint64, qint64)));
	QSignalSpy finished(&manager, SIGNAL(finished(QString)));

	manager.start(assetFor(server.urlFor("/pkg"), body.size()), dir.path());
	QVERIFY(finished.wait(10000));
	QVERIFY2(progress.count() > 0, "no progress was ever reported");
}

void TestDownloadManager::removesThePartialFileWhenTheServerOversends()
{
	/* The release metadata is the authority on how big the package is, not the
	   server's own Content-Length. A server offering 200 KiB where the release
	   published 1 KiB is cut off rather than allowed to fill the disk. */
	QByteArray body(200 * 1024, 'O');
	TestHttpServer server;
	server.setBody(body);
	QVERIFY(server.listen(QHostAddress::LocalHost));

	QTemporaryDir dir;
	LoopbackDownloadManager manager;
	QSignalSpy failed(&manager, SIGNAL(failed(int, QString)));

	manager.start(assetFor(server.urlFor("/pkg"), 1024), dir.path());
	QVERIFY(failed.wait(10000));
	QCOMPARE(failed.first().first().toInt(), int(ErrorSizeMismatch));
	QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 0);
}

void TestDownloadManager::removesThePartialFileWhenTheTransferIsTruncated()
{
	QByteArray body(128 * 1024, 'T');
	TestHttpServer server;
	server.setBody(body);
	server.setBehaviour(TestHttpServer::TruncateAndClose);
	QVERIFY(server.listen(QHostAddress::LocalHost));

	QTemporaryDir dir;
	LoopbackDownloadManager manager;
	QSignalSpy failed(&manager, SIGNAL(failed(int, QString)));

	manager.start(assetFor(server.urlFor("/pkg"), body.size()), dir.path());
	QVERIFY(failed.wait(10000));
	QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 0);
}

void TestDownloadManager::removesThePartialFileWhenCancelled()
{
	QByteArray body(4 * 1024 * 1024, 'C');
	TestHttpServer server;
	server.setBody(body);
	QVERIFY(server.listen(QHostAddress::LocalHost));

	QTemporaryDir dir;
	LoopbackDownloadManager manager;
	QSignalSpy failed(&manager, SIGNAL(failed(int, QString)));

	manager.start(assetFor(server.urlFor("/pkg"), body.size()), dir.path());
	QVERIFY(manager.isBusy());
	manager.cancel();

	QCOMPARE(failed.count(), 1);
	QCOMPARE(failed.first().first().toInt(), int(ErrorCancelled));
	QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 0);
}

void TestDownloadManager::refusesAnUnexpectedHost()
{
	QTemporaryDir dir;
	/* The production policy, not the loopback subclass. */
	DownloadManager manager;
	QSignalSpy failed(&manager, SIGNAL(failed(int, QString)));

	manager.start(assetFor(QUrl(QLatin1String("https://evil.example.com/pkg.zip")), 1024),
		dir.path());

	QCOMPARE(failed.count(), 1);
	QCOMPARE(failed.first().first().toInt(), int(ErrorForbiddenRedirect));
	QVERIFY(!manager.isBusy());
}

void TestDownloadManager::refusesAnImplausibleDeclaredSize()
{
	QTemporaryDir dir;
	LoopbackDownloadManager manager;
	QSignalSpy failed(&manager, SIGNAL(failed(int, QString)));

	manager.start(assetFor(QUrl(QLatin1String("http://127.0.0.1:1/pkg")),
		DownloadManager::ABSOLUTE_MAX_BYTES + 1), dir.path());

	QCOMPARE(failed.count(), 1);
	QCOMPARE(failed.first().first().toInt(), int(ErrorSizeMismatch));
}

void TestDownloadManager::refusesAnIncompleteAsset()
{
	QTemporaryDir dir;
	LoopbackDownloadManager manager;
	QSignalSpy failed(&manager, SIGNAL(failed(int, QString)));

	ReleaseAsset broken;
	broken.name = QLatin1String("x.zip");
	manager.start(broken, dir.path());

	QCOMPARE(failed.count(), 1);
	QCOMPARE(failed.first().first().toInt(), int(ErrorDownloadFailed));
}

void TestDownloadManager::reportsAnHttpError()
{
	TestHttpServer server;
	server.setBehaviour(TestHttpServer::RespondNotFound);
	QVERIFY(server.listen(QHostAddress::LocalHost));

	QTemporaryDir dir;
	LoopbackDownloadManager manager;
	QSignalSpy failed(&manager, SIGNAL(failed(int, QString)));

	manager.start(assetFor(server.urlFor("/missing"), 1024), dir.path());
	QVERIFY(failed.wait(10000));
	QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 0);
}

void TestDownloadManager::refusesToRunTwoDownloadsAtOnce()
{
	QByteArray body(2 * 1024 * 1024, 'M');
	TestHttpServer server;
	server.setBody(body);
	QVERIFY(server.listen(QHostAddress::LocalHost));

	QTemporaryDir dir;
	LoopbackDownloadManager manager;
	QSignalSpy failed(&manager, SIGNAL(failed(int, QString)));

	manager.start(assetFor(server.urlFor("/a"), body.size()), dir.path());
	QVERIFY(manager.isBusy());
	manager.start(assetFor(server.urlFor("/b"), body.size()), dir.path());

	QCOMPARE(failed.count(), 1);
	QCOMPARE(failed.first().first().toInt(), int(ErrorDownloadFailed));
	manager.cancel();
}

void TestDownloadManager::doesNotResumeALeftoverPartialFile()
{
	/* v1 has no Range resume: bytes from an earlier, unverified session are
	   never reused, they are thrown away. */
	QByteArray body(32 * 1024, 'R');
	TestHttpServer server;
	server.setBody(body);
	QVERIFY(server.listen(QHostAddress::LocalHost));

	QTemporaryDir dir;
	const QString stale = QDir(dir.path()).absoluteFilePath(
		"PE-bear_0.7.3_qt5_x64_linux.tar.xz.part");
	QFile f(stale);
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write(QByteArray(1024, 'X'));
	f.close();

	LoopbackDownloadManager manager;
	QSignalSpy finished(&manager, SIGNAL(finished(QString)));
	manager.start(assetFor(server.urlFor("/pkg"), body.size()), dir.path());
	QVERIFY(finished.wait(10000));

	const QString path = finished.first().first().toString();
	QCOMPARE(QFileInfo(path).size(), qint64(body.size()));

	QFile written(path);
	QVERIFY(written.open(QIODevice::ReadOnly));
	QCOMPARE(written.readAll(), body);
}

QTEST_MAIN(TestDownloadManager)
#include "tst_downloadmanager.moc"
